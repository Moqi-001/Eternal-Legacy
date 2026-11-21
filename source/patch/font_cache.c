/* Known immutable fonts share bytes; FreeType faces/sizes remain independent.
 * Original SO c3555dd862c78d0da3c6ef2505e98a0aca64473d686441b3f75bfb2fe4526212.
 * Hook destroy_face, not just FT_Done_Face: FT_Done_Library destroys faces
 * directly through that internal callback. Release the pin AFTER destruction. */
#include "font_cache.h"
#include "../config.h"
#include "../utils/paths.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <so_common/so_logger.h>

#define FONT_BUDGET (12u * 1024u * 1024u)
#define FACE_SLOTS 128
#define DETAIL_BUDGET 8
#define CALLER ((uintptr_t)__builtin_return_address(0))
typedef struct {
    const char *path;
    size_t expected_size;
    unsigned char *bytes;
    unsigned pins;
    int state; /* 0 empty, 1 loading, 2 ready */
} FontBlob;
static FontBlob s_blobs[] = {{GAME_DATA_DIR "chinese.ttf", 10808952, NULL, 0, 0},
                             {GAME_DATA_DIR "Vera.ttf", 122332, NULL, 0, 0}};
_Static_assert(10808952u + 122332u <= FONT_BUDGET, "Known font blobs exceed budget");
typedef struct {
    void *face;
    int state, blob; /* 0 unused, 1 constructing, 2 live, 3 destructing */
} FaceSlot;
static FaceSlot s_faces[FACE_SLOTS];
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;
static int s_stopping;
static so_module *s_module;
static intptr_t s_bias;
static size_t s_module_size;
static so_hook_ref s_new_hook, s_destroy_hook, s_char_hook, s_glyph_hook;
static int (*s_memory_new)(void *, const unsigned char *, long, long, void **);
typedef struct {
    uint64_t calls, failures, us, max_us;
} Span;
static struct {
    unsigned active, created, fallback, blob_loads, blob_hits;
    Span chars, glyphs;
    uint64_t last_report, detail_window;
    unsigned details;
    int dirty;
} s_stats;
static uint64_t now_us(void) {
    return sceKernelGetProcessTimeWide();
}
static uintptr_t caller_elf(uintptr_t pc) {
    pc &= ~(uintptr_t)1;
    return pc >= (uintptr_t)s_bias && pc - (uintptr_t)s_bias < s_module_size
               ? pc - (uintptr_t)s_bias
               : 0;
}
static int detail_allowed_locked(uint64_t now) {
    if (now - s_stats.detail_window >= 1000000) {
        s_stats.detail_window = now;
        s_stats.details = 0;
    }
    if (s_stats.details >= DETAIL_BUDGET)
        return 0;
    ++s_stats.details;
    return 1;
}
static int known_font(const char *path) {
    if (!path)
        return -1;
    char mapped[PATH_MAPPED_MAX];
    const char *p = paths_from_android(path, mapped, sizeof(mapped));
    if (!p)
        return -1;
    for (unsigned i = 0; i < sizeof(s_blobs) / sizeof(s_blobs[0]); i++)
        if (!strcmp(p, s_blobs[i].path))
            return (int)i;
    return -1;
}
static void blob_release_locked(int index) {
    FontBlob *b = &s_blobs[index];
    if (b->pins)
        --b->pins;
    if (s_stopping && !b->pins && b->state == 2) {
        free(b->bytes);
        b->bytes = NULL;
        b->state = 0;
    }
}
static unsigned char *read_blob(FontBlob *blob) {
    uint64_t start = now_us();
    SceUID fd = sceIoOpen(blob->path, SCE_O_RDONLY, 0);
    unsigned char *bytes = NULL;
    SceOff size = -1;
    size_t done = 0;
    int ok = 0;
    if (fd >= 0) {
        size = sceIoLseek(fd, 0, SCE_SEEK_END);
        /* Unknown replacement fonts retain original file semantics. */
        if (size > 0 && (uint64_t)size == blob->expected_size && (uint64_t)size <= FONT_BUDGET &&
            sceIoLseek(fd, 0, SCE_SEEK_SET) == 0) {
            bytes = malloc((size_t)size);
            while (bytes && done < (size_t)size) {
                unsigned amount =
                    (size_t)size - done > 128 * 1024 ? 128 * 1024 : (unsigned)((size_t)size - done);
                int n = sceIoRead(fd, bytes + done, amount);
                if (n <= 0 || (unsigned)n > amount)
                    break;
                done += (unsigned)n;
            }
            ok = bytes && done == (size_t)size && bytes[0] == 0 && bytes[1] == 1 && bytes[2] == 0 &&
                 bytes[3] == 0;
        }
        sceIoClose(fd);
    }
    if (!ok) {
        free(bytes);
        bytes = NULL;
    }
    so_loga("[FONT] event=blob_load tid=%d font=%s bytes=%u expected=%u ok=%d fontload_us=%llu",
            sceKernelGetThreadId(), blob->path, (unsigned)done, (unsigned)blob->expected_size, ok,
            (unsigned long long)(now_us() - start));
    return bytes;
}
static int blob_pin(int index) {
    pthread_mutex_lock(&s_lock);
    FontBlob *b = &s_blobs[index];
    while (b->state == 1 && !s_stopping)
        pthread_cond_wait(&s_changed, &s_lock);
    if (s_stopping) {
        pthread_mutex_unlock(&s_lock);
        return 0;
    }
    if (b->state == 2) {
        ++b->pins;
        ++s_stats.blob_hits;
        s_stats.dirty = 1;
        pthread_mutex_unlock(&s_lock);
        return 1;
    }
    b->state = 1;
    pthread_mutex_unlock(&s_lock);
    unsigned char *bytes = read_blob(b);
    pthread_mutex_lock(&s_lock);
    if (bytes && !s_stopping) {
        b->bytes = bytes;
        b->pins = 1;
        b->state = 2;
        ++s_stats.blob_loads;
        s_stats.dirty = 1;
    } else {
        free(bytes);
        b->state = 0;
    }
    int ok = b->state == 2;
    pthread_cond_broadcast(&s_changed);
    pthread_mutex_unlock(&s_lock);
    return ok;
}
static int reserve_face(void) {
    pthread_mutex_lock(&s_lock);
    int index = -1;
    if (!s_stopping)
        for (unsigned i = 0; i < FACE_SLOTS; i++)
            if (!s_faces[i].state) {
                index = (int)i;
                s_faces[i] = (FaceSlot){.state = 1, .blob = -1};
                break;
            }
    pthread_mutex_unlock(&s_lock);
    return index;
}
static int original_file(void *library, const char *path, long index, void **out, int error) {
    pthread_mutex_lock(&s_lock);
    ++s_stats.fallback;
    s_stats.dirty = 1;
    pthread_mutex_unlock(&s_lock);
    errno = error;
    return SO_CONTINUE(int, &s_new_hook, library, path, index, out);
}
static int font_new_face(void *library, const char *path, long index, void **out) {
    int saved_errno = errno, blob = known_font(path), slot = -1;
    if (!out || !library || blob < 0 || (slot = reserve_face()) < 0)
        return original_file(library, path, index, out, saved_errno);
    if (!blob_pin(blob)) {
        pthread_mutex_lock(&s_lock);
        s_faces[slot].state = 0;
        pthread_mutex_unlock(&s_lock);
        return original_file(library, path, index, out, saved_errno);
    }
    void *face = NULL;
    int result =
        s_memory_new(library, s_blobs[blob].bytes, (long)s_blobs[blob].expected_size, index, &face);
    pthread_mutex_lock(&s_lock);
    if (!result && face) {
        s_faces[slot] = (FaceSlot){.face = face, .state = 2, .blob = blob};
        ++s_stats.active;
        ++s_stats.created;
    } else {
        s_faces[slot].state = 0;
        blob_release_locked(blob);
    }
    s_stats.dirty = 1;
    pthread_mutex_unlock(&s_lock);
    if (result || !face)
        return original_file(library, path, index, out, saved_errno);
    *out = face;
    errno = saved_errno;
    return 0;
}
static void font_destroy_face(void *memory, void *face, void *driver) {
    int slot = -1, blob = -1;
    pthread_mutex_lock(&s_lock);
    for (unsigned i = 0; i < FACE_SLOTS; i++)
        if (s_faces[i].state == 2 && s_faces[i].face == face) {
            slot = (int)i;
            blob = s_faces[i].blob;
            s_faces[i].state = 3;
            s_faces[i].face = NULL;
            break;
        }
    pthread_mutex_unlock(&s_lock);
    SO_CONTINUE(void, &s_destroy_hook, memory, face, driver);
    int saved_errno = errno;
    if (slot < 0)
        return;
    pthread_mutex_lock(&s_lock);
    s_faces[slot].state = 0;
    --s_stats.active;
    blob_release_locked(blob);
    s_stats.dirty = 1;
    pthread_mutex_unlock(&s_lock);
    errno = saved_errno;
}
static void record_span(int chars, void *face, unsigned long code, int flags, int result,
                        uint64_t start, uintptr_t pc) {
    int saved_errno = errno;
    uint64_t duration = now_us() - start;
    pthread_mutex_lock(&s_lock);
    Span *span = chars ? &s_stats.chars : &s_stats.glyphs;
    ++span->calls;
    span->failures += result != 0;
    s_stats.dirty = 1;
    span->us += duration;
    if (duration > span->max_us)
        span->max_us = duration;
    int log =
        (result != 0 || duration >= 20000) && detail_allowed_locked(now_us());
    pthread_mutex_unlock(&s_lock);
    if (log)
        so_loga("[FONT] event=%s tid=%d caller_elf=%#lx face=%p %s=%lu "
                "flags=%#x result=%d us=%llu",
                chars ? "load_char" : "load_glyph", sceKernelGetThreadId(), (unsigned long)caller_elf(pc),
                face, chars ? "codepoint" : "glyph_index", code, flags, result,
                (unsigned long long)duration);
    font_cache_poll(0);
    errno = saved_errno;
}
static int font_load_char(void *face, unsigned long code, int flags) {
    uint64_t t = now_us();
    int result = SO_CONTINUE(int, &s_char_hook, face, code, flags);
    record_span(1, face, code, flags, result, t, CALLER);
    return result;
}
static int font_load_glyph(void *face, unsigned code, int flags) {
    uint64_t t = now_us();
    int result = SO_CONTINUE(int, &s_glyph_hook, face, code, flags);
    record_span(0, face, code, flags, result, t, CALLER);
    return result;
}
void font_cache_poll(int force) {
    int saved_errno = errno;
    uint64_t now = now_us();
    pthread_mutex_lock(&s_lock);
    if (!force && (!s_stats.dirty || now - s_stats.last_report < 1000000)) {
        pthread_mutex_unlock(&s_lock);
        errno = saved_errno;
        return;
    }
    s_stats.last_report = now;
    s_stats.dirty = 0;
    unsigned active = s_stats.active, created = s_stats.created, fallback = s_stats.fallback,
             loads = s_stats.blob_loads, hits = s_stats.blob_hits,
             pins = 0, bytes = 0;
    for (unsigned i = 0; i < 2; i++) {
        pins += s_blobs[i].pins;
        if (s_blobs[i].state == 2)
            bytes += (unsigned)s_blobs[i].expected_size;
    }
    Span chars = s_stats.chars, glyphs = s_stats.glyphs;
    pthread_mutex_unlock(&s_lock);
    so_loga("[FONT] totals active=%u pins=%u bytes=%u budget=%u created=%u fallback=%u "
            "blob_loads=%u blob_hits=%u char_calls=%llu char_fail=%llu char_us=%llu "
            "char_max_us=%llu glyph_calls=%llu glyph_fail=%llu glyph_us=%llu glyph_max_us=%llu spans_nested=1",
            active, pins, bytes, FONT_BUDGET, created, fallback, loads, hits,
            (unsigned long long)chars.calls, (unsigned long long)chars.failures, (unsigned long long)chars.us,
            (unsigned long long)chars.max_us, (unsigned long long)glyphs.calls,
            (unsigned long long)glyphs.failures, (unsigned long long)glyphs.us, (unsigned long long)glyphs.max_us);
    errno = saved_errno;
}
void font_cache_shutdown(void) {
    pthread_mutex_lock(&s_lock);
    s_stopping = 1;
    for (unsigned i = 0; i < 2; i++)
        if (s_blobs[i].state == 2 && !s_blobs[i].pins) {
            free(s_blobs[i].bytes);
            s_blobs[i].bytes = NULL;
            s_blobs[i].state = 0;
        }
    pthread_cond_broadcast(&s_changed);
    pthread_mutex_unlock(&s_lock);
    font_cache_poll(1);
}
int soloader_patch_font_cache(so_module *mod) {
    if (!mod)
        return -1;
    if (s_module)
        return mod == s_module ? 0 : -1;
    intptr_t bias = so_module_get_load_bias(mod);
    const struct {
        const char *name;
        unsigned offset;
        unsigned char bytes[16];
        void *replacement;
        so_hook_ref *hook;
    } guards[] = {
        {"FT_New_Face",
         0x4c2098,
         {0x30, 0xb5, 0x89, 0xb0, 0x06, 0x24, 0x00, 0x29, 0x06, 0xd0, 0x03, 0x91, 0x04, 0x25, 0x69,
          0x46},
         font_new_face,
         &s_new_hook},
        {"destroy_face",
         0x4bfa88,
         {0xf8, 0xb5, 0x8b, 0x6f, 0x05, 0x1c, 0x0c, 0x1c, 0x16, 0x1c, 0x57, 0x69, 0x00, 0x2b, 0x04,
          0xd0},
         font_destroy_face,
         &s_destroy_hook},
        {"FT_Load_Char",
         0x4c0ea4,
         {0x70, 0xb5, 0x04, 0x1c, 0x15, 0x1c, 0x23, 0x20, 0x00, 0x2c, 0x0c, 0xd0, 0xe2, 0x6d, 0x0b,
          0x1c},
         font_load_char,
         &s_char_hook},
        {"FT_Load_Glyph",
         0x4c0ba8,
         {0xf0, 0xb5, 0x5f, 0x46, 0x56, 0x46, 0x4d, 0x46, 0x44, 0x46, 0xf0, 0xb4, 0x83, 0xb0, 0x06,
          0x1c},
         font_load_glyph,
         &s_glyph_hook},
        {"FT_New_Memory_Face",
         0x4c2074,
         {0x30, 0xb5, 0x89, 0xb0, 0x06, 0x24, 0x00, 0x29, 0x09, 0xd0, 0x01, 0x91, 0x02, 0x92, 0x01,
          0x25},
         NULL,
         NULL},
    };
    if (so_module_get_load_size(mod) < 0x4c20b8) {
        so_loga("[FONT] disabled reason=unsupported_so_size");
        return 0;
    }
    for (unsigned i = 0; i < sizeof(guards) / sizeof(*guards); i++) {
        if ((strcmp(guards[i].name, "destroy_face") &&
             (uintptr_t)so_module_find_export(mod, guards[i].name) !=
                 (uintptr_t)bias + guards[i].offset + 1) ||
            memcmp((const void *)(bias + guards[i].offset), guards[i].bytes, 16)) {
            so_loga("[FONT] disabled reason=unsupported_so_target offset=%#x", guards[i].offset);
            return 0;
        }
    }
    s_bias = bias;
    s_module_size = so_module_get_load_size(mod);
    s_memory_new = (int (*)(void *, const unsigned char *, long, long, void **))(bias + 0x4c2075);
    unsigned installed = 0;
    so_hook_ref *installed_hooks[sizeof(guards) / sizeof(*guards)];
    for (unsigned i = 0; i < sizeof(guards) / sizeof(*guards); i++) {
        if (!guards[i].replacement)
            continue;
        int result = so_module_hook_address(mod, guards[i].offset | 1, guards[i].replacement,
                                            guards[i].hook);
        if (result < 0 || !guards[i].hook->orig_func) {
            if (result == 0)
                so_unhook(guards[i].hook);
            while (installed)
                so_unhook(installed_hooks[--installed]);
            so_loga("[FONT] install_failed target=%s offset=%#x result=%d",
                    guards[i].name, guards[i].offset, result);
            s_memory_new = NULL;
            return -1;
        }
        installed_hooks[installed++] = guards[i].hook;
    }
    s_module = mod;
    so_loga("[FONT] installed supported_so=c3555dd862c78d0d byte_budget=%u face_slots=%u hooks=4",
            FONT_BUDGET, FACE_SLOTS);
    return 0;
}
