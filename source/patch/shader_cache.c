/* Persistent vitaGL program binaries. Cache data is never a log file.
 * Only game GL imports are wrapped; all compiler/GXM work stays on GLThread.
 * The format is pinned to the bundled vitaGL/shark ABI by the build hash. */
#include "shader_cache.h"
#include "config.h"
#include "utils/perf.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <so_common/so_logger.h>
#include <so_libs/libc/bionic_md5.h>
#include <vitaGL.h>
#include <psp2/gxm.h>

#ifndef ETERNAL_SHADER_CACHE_ABI
#define ETERNAL_SHADER_CACHE_ABI "unconfigured"
#endif
#define SHADERS 2048
#define PROGRAMS 1024
#define DISK_FILES 256
#define DISK_BYTES (32u * 1024u * 1024u)
#define BINARY_MAX (512u * 1024u)
#define SOURCE_MAX (1024u * 1024u)
#define BINDINGS_MAX 64
/* Bundled vitaGL public GXP shader types; glCreateShader clears translation. */
#define VERTEX_GXP GL_CG_VERTEX_SHADER_EXT
#define FRAGMENT_GXP GL_CG_FRAGMENT_SHADER_EXT
#define CACHE_DIRECTORY SO_CACHE_DIR "shaders-02/"
typedef struct { unsigned char digest[16]; GLenum type; int valid, ever_attached; } ShaderInfo;
typedef struct Binding { struct Binding *next; GLuint index; char name[]; } Binding;
typedef struct {
    MD5_CTX bindings;
    Binding *first_binding, *last_binding;
    unsigned binding_count;
    GLuint sources[2], holder;
    int initialized, key_valid, pristine, finished, cached, pending;
} ProgramInfo;
typedef struct { char name[37]; uint32_t bytes; uint64_t age; } DiskInfo;
typedef struct {
    char magic[8];
    uint32_t version, bytes;
    unsigned char key[16], abi[16];
    uint32_t payload_crc, header_crc, format, reserved;
} BinaryHeader;
_Static_assert(sizeof(BinaryHeader) == 64, "binary cache header ABI");
static ShaderInfo s_shader[SHADERS];
static ProgramInfo s_program[PROGRAMS];
static DiskInfo s_disk[DISK_FILES];
static unsigned char s_abi[16];
static unsigned s_pending, s_hits, s_misses, s_rejected, s_saved, s_errors, s_bypasses, s_details;
static uint64_t s_tick, s_last_report, s_load_us, s_save_us, s_detail_window;
static uint32_t s_disk_bytes;
static int s_ready;
static so_hook_ref s_link_hook;
static so_module *s_module;

static void hash_part(MD5_CTX *hash, const void *data, size_t bytes) {
    uint32_t n = (uint32_t)bytes;
    MD5_Update(hash, &n, sizeof(n));
    if (bytes) MD5_Update(hash, data, bytes);
}
static void finish_hash(MD5_CTX *hash, unsigned char out[16]) {
    uint32_t aligned[4]; MD5_Final(aligned, hash); memcpy(out, aligned, 16);
}
static uint32_t word(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void name_for(const unsigned char key[16], char name[37]) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; ++i) { name[2*i] = hex[key[i] >> 4]; name[2*i+1] = hex[key[i] & 15]; }
    memcpy(name+32, ".bin", 5);
}
static int owned_name(const char *s) {
    if (strlen(s) != 36 || strcmp(s+32, ".bin")) return 0;
    for (unsigned i = 0; i < 32; ++i) if (!strchr("0123456789abcdef", s[i])) return 0;
    return 1;
}
static void full_path(char *out, size_t capacity, const char *name) {
    snprintf(out, capacity, "%s%s", CACHE_DIRECTORY, name);
}
static DiskInfo *find_disk(const char *name) {
    for (unsigned i = 0; i < DISK_FILES; ++i) if (s_disk[i].name[0] && !strcmp(s_disk[i].name, name)) return &s_disk[i];
    return NULL;
}
static int drop_disk(DiskInfo *entry) {
    char path[256]; full_path(path, sizeof(path), entry->name);
    if (sceIoRemove(path) < 0) return -1;
    s_disk_bytes -= entry->bytes; memset(entry, 0, sizeof(*entry)); return 0;
}
static int read_all(int fd, void *data, size_t bytes) {
    size_t done = 0;
    while (done < bytes) {
        size_t request = bytes-done; if (request > 64*1024) request = 64*1024;
        int n = sceIoRead(fd, (char *)data+done, (SceSize)request);
        if (n <= 0 || (size_t)n > request) return -1;
        done += n;
    }
    return 0;
}
static int write_all(int fd, const void *data, size_t bytes) {
    size_t done = 0;
    while (done < bytes) {
        size_t request = bytes-done; if (request > 64*1024) request = 64*1024;
        int n = sceIoWrite(fd, (const char *)data+done, (SceSize)request);
        if (n <= 0 || (size_t)n > request) return -1;
        done += n;
    }
    return 0;
}

/* glProgramBinary ignores its length/format arguments. Validate the pinned
 * serialize_shader layout BEFORE invoking it. CRC+ABI bind this to our own
 * generated data; never give a truncated or stale blob to the driver. */
static int valid_shader(const unsigned char *p, size_t bytes) {
    if (bytes < 4) return 0;
    uint32_t samplers = word(p);
    if (samplers > 4096 || (size_t)samplers > (bytes-4)/4) return 0;
    size_t prefix = 4 + (size_t)samplers*4;
    if (bytes-prefix < 80) return 0;
    const unsigned char *gxp = p+prefix;
    size_t length = bytes-prefix;
    if (memcmp(gxp, "GXP\0", 4) || word(gxp+8) != length) return 0;
    uint32_t params = word(gxp+36), offset = word(gxp+40);
    if (params > 4096 || samplers > params) return 0;
    if (params && ((size_t)offset > length-40 || (size_t)params > (length-40-offset)/16)) return 0;
    for (uint32_t i = 0; i < samplers; ++i) if (word(p+4+4*i) >= params) return 0;
    return sceGxmProgramCheck((const SceGxmProgram *)gxp) == 0;
}
static int valid_binary(const unsigned char *p, size_t bytes) {
    if (bytes < 140 || word(p) > 16) return 0;
    uint32_t vertex = word(p+132);
    if (vertex > bytes-140 || !valid_shader(p+136, vertex)) return 0;
    uint32_t fragment = word(p+136+vertex);
    return (size_t)fragment == bytes-140-vertex && valid_shader(p+140+vertex, fragment);
}
static unsigned char *load_binary(const unsigned char key[16], uint32_t *length, GLenum *format) {
    uint64_t start = perf_now_us();
    char name[37], path[256]; name_for(key, name); full_path(path, sizeof(path), name);
    /* Complete private-directory inventory avoids a cold SD lookup per miss. */
    if (!find_disk(name)) { s_load_us += perf_now_us()-start; return NULL; }
    int fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) { s_load_us += perf_now_us()-start; return NULL; }
    BinaryHeader h, checked;
    unsigned char *data = NULL;
    int ok = !read_all(fd, &h, sizeof(h));
    if (ok) {
        checked = h; checked.header_crc = 0;
        ok = !memcmp(h.magic, "ELGXP02", 8) && h.version == 2 && !h.reserved &&
             h.bytes >= 140 && h.bytes <= BINARY_MAX && !memcmp(h.key, key, 16) &&
             !memcmp(h.abi, s_abi, 16) && h.header_crc == (uint32_t)crc32(0, (const Bytef *)&checked, sizeof(checked));
    }
    if (ok) { data = malloc(h.bytes); ok = data && !read_all(fd, data, h.bytes); }
    unsigned char extra;
    if (ok) ok = sceIoRead(fd, &extra, 1) == 0;
    if (sceIoClose(fd) < 0) ok = 0;
    if (ok) ok = h.payload_crc == (uint32_t)crc32(0, data, h.bytes) && valid_binary(data, h.bytes);
    s_load_us += perf_now_us()-start;
    if (!ok) {
        ++s_rejected; free(data); DiskInfo *old = find_disk(name);
        if (old) (void)drop_disk(old); else (void)sceIoRemove(path);
        return NULL;
    }
    DiskInfo *entry = find_disk(name); if (entry) entry->age = ++s_tick;
    *length = h.bytes; *format = h.format; return data;
}
static void save_binary(GLuint program, const unsigned char key[16]) {
    uint64_t start = perf_now_us();
    GLint capacity = 0; glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &capacity);
    if (capacity < 140 || capacity > (GLint)BINARY_MAX) { ++s_bypasses; return; }
    unsigned char *data = malloc((size_t)capacity);
    if (!data) { ++s_errors; return; }
    GLsizei length = 0; GLenum format = 0;
    glGetProgramBinary(program, capacity, &length, &format, data);
    if (length < 140 || length > capacity || !valid_binary(data, length)) { free(data); ++s_rejected; return; }
    char name[37], path[256], temporary[272]; name_for(key, name); full_path(path, sizeof(path), name);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    DiskInfo *previous = find_disk(name);
    if (previous && drop_disk(previous) < 0) { free(data); ++s_errors; return; }
    uint32_t needed = sizeof(BinaryHeader) + (uint32_t)length;
    DiskInfo *slot = NULL;
    for (;;) {
        for (unsigned i = 0; i < DISK_FILES; ++i) if (!s_disk[i].name[0]) { slot = &s_disk[i]; break; }
        if (slot && s_disk_bytes <= DISK_BYTES && needed <= DISK_BYTES-s_disk_bytes) break;
        DiskInfo *old = NULL;
        for (unsigned i = 0; i < DISK_FILES; ++i)
            if (s_disk[i].name[0] && (!old || s_disk[i].age < old->age)) old = &s_disk[i];
        if (!old || drop_disk(old) < 0) { free(data); ++s_errors; return; }
    }
    BinaryHeader h = { .magic = "ELGXP02", .version = 2, .bytes = (uint32_t)length, .format = format };
    memcpy(h.key, key, 16); memcpy(h.abi, s_abi, 16);
    h.payload_crc = (uint32_t)crc32(0, data, length);
    h.header_crc = (uint32_t)crc32(0, (const Bytef *)&h, sizeof(h));
    int fd = sceIoOpen(temporary, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    int ok = fd >= 0;
    if (ok) ok = !write_all(fd, &h, sizeof(h)) && !write_all(fd, data, length);
    if (fd >= 0 && sceIoClose(fd) < 0) ok = 0;
    if (ok) ok = sceIoRename(temporary, path) >= 0;
    if (ok) { strcpy(slot->name, name); slot->bytes = needed; slot->age = ++s_tick; s_disk_bytes += needed; ++s_saved; }
    else { (void)sceIoRemove(temporary); ++s_errors; }
    free(data); s_save_us += perf_now_us()-start;
}

static ProgramInfo *program_info(GLuint id) {
    return id && id <= PROGRAMS && s_program[id-1].initialized ? &s_program[id-1] : NULL;
}
static void free_bindings(ProgramInfo *info) {
    Binding *binding = info->first_binding;
    while (binding) { Binding *next = binding->next; free(binding); binding = next; }
    info->first_binding = info->last_binding = NULL; info->binding_count = 0;
}
static void release_info(ProgramInfo *info) {
    free_bindings(info);
    if (info->holder) glDeleteProgram(info->holder);
    if (info->pending) --s_pending;
    memset(info, 0, sizeof(*info));
}
static void reap_pending(void) {
    if (s_pending) for (GLuint id = 1; id <= PROGRAMS; ++id)
        if (s_program[id-1].pending && !glIsProgram(id)) release_info(&s_program[id-1]);
}
static GLuint cache_create_shader(GLenum type) {
    GLuint id = glCreateShader(type);
    if (id && id <= SHADERS) { memset(&s_shader[id-1], 0, sizeof(ShaderInfo)); s_shader[id-1].type = type; }
    return id;
}
static void cache_source(GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths) {
    glShaderSource(shader, count, strings, lengths);
    if (!shader || shader > SHADERS) return;
    ShaderInfo *info = &s_shader[shader-1]; info->valid = 0;
    if (count <= 0 || count > 1024 || !strings || !info->type) return;
    MD5_CTX hash; MD5_Init(&hash); MD5_Update(&hash, &info->type, sizeof(info->type));
    size_t total = 0;
    for (GLsizei i = 0; i < count; ++i) {
        if (!strings[i]) return;
        size_t n = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strnlen(strings[i], SOURCE_MAX+1);
        if (n > SOURCE_MAX-total) return;
        MD5_Update(&hash, strings[i], n); total += n;
    }
    hash_part(&hash, &total, sizeof(total)); finish_hash(&hash, info->digest); info->valid = 1;
}
static GLuint cache_create_program(void) {
    reap_pending();
    GLuint id = glCreateProgram();
    if (id && id <= PROGRAMS) {
        ProgramInfo *info = &s_program[id-1];
        if (info->initialized) release_info(info);
        memset(info, 0, sizeof(*info)); info->initialized = info->key_valid = info->pristine = 1; MD5_Init(&info->bindings);
    }
    return id;
}
static void cache_attach(GLuint program, GLuint shader) {
    ProgramInfo *info = program_info(program);
    if (!shader || shader > SHADERS || !s_shader[shader-1].type) {
        if (info) info->pristine = 0;
    } else {
        ShaderInfo *stage = &s_shader[shader-1];
        if (stage->ever_attached && info) info->pristine = 0;
        stage->ever_attached = 1;
    }
    glAttachShader(program, shader);
}
static void cache_shader_binary(GLsizei count, const GLuint *shaders, GLenum format,
                                 const void *data, GLsizei length) {
    /* Bundled implementation only consumes shaders[0], even for bad count. */
    if (shaders && shaders[0] && shaders[0] <= SHADERS) {
        s_shader[shaders[0]-1].valid = 0; s_shader[shaders[0]-1].ever_attached = 1;
    }
    glShaderBinary(count, shaders, format, data, length);
}
static void cache_bind_attribute(GLuint program, GLuint index, const GLchar *name) {
    glBindAttribLocation(program, index, name);
    ProgramInfo *info = program_info(program);
    if (!info) return;
    if (!name || strnlen(name, 1025) > 1024 || index >= 16 || info->finished) { info->key_valid = 0; return; }
    size_t bytes = strlen(name);
    if (info->binding_count == BINDINGS_MAX) { info->key_valid = 0; free_bindings(info); return; }
    Binding *binding = malloc(sizeof(*binding)+bytes+1);
    if (!binding) { info->key_valid = 0; free_bindings(info); return; }
    binding->next = NULL; binding->index = index; memcpy(binding->name, name, bytes+1);
    if (info->last_binding) info->last_binding->next = binding; else info->first_binding = binding;
    info->last_binding = binding; ++info->binding_count;
    hash_part(&info->bindings, &index, sizeof(index)); hash_part(&info->bindings, name, bytes);
}
static int program_key(GLuint program, ProgramInfo *info, unsigned char key[16]) {
    GLsizei count = 0; GLuint shaders[2];
    glGetAttachedShaders(program, 2, &count, shaders);
    if (count != 2) return 0;
    GLuint vertex = 0, fragment = 0;
    for (unsigned i = 0; i < 2; ++i) {
        GLuint id = shaders[i];
        if (!id || id > SHADERS || !s_shader[id-1].valid) return 0;
        if (s_shader[id-1].type == GL_VERTEX_SHADER) vertex = id;
        else if (s_shader[id-1].type == GL_FRAGMENT_SHADER) fragment = id;
    }
    if (!vertex || !fragment) return 0;
    MD5_CTX hash; MD5_Init(&hash);
    hash_part(&hash, s_abi, 16); hash_part(&hash, s_shader[vertex-1].digest, 16);
    hash_part(&hash, s_shader[fragment-1].digest, 16);
    MD5_CTX bindings = info->bindings; unsigned char digest[16]; finish_hash(&bindings, digest);
    hash_part(&hash, digest, 16); finish_hash(&hash, key);
    info->sources[0] = vertex; info->sources[1] = fragment; return 1;
}
static void detach_current(GLuint program) {
    GLuint shaders[2]; GLsizei count = 0; glGetAttachedShaders(program, 2, &count, shaders);
    for (GLsizei i = 0; i < count; ++i) glDetachShader(program, shaders[i]);
}
static int restore_binary(GLuint program, ProgramInfo *info, const unsigned char *binary, uint32_t length, GLenum format) {
    (void)length;
    reap_pending();
    GLuint holder = glCreateProgram();
    if (!holder) return 0;
    GLuint vertex = glCreateShader(VERTEX_GXP), fragment = glCreateShader(FRAGMENT_GXP);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        glDeleteProgram(holder); return 0;
    }
    /* glProgramBinary preloads the saved attribute count and its native attach
     * appends attributes again (9 become18, overflowing the16-slot table).
     * Restore each serialized shader through public glShaderBinary instead,
     * then attach to the empty program and replay the original named bindings.
     * count MUST be1: this bundled driver only deserializes the first shader. */
    uint32_t vertex_bytes = word(binary+132);
    uint32_t fragment_bytes = word(binary+136+vertex_bytes);
    glShaderBinary(1, &vertex, format, binary+136, vertex_bytes);
    glShaderBinary(1, &fragment, format, binary+140+vertex_bytes, fragment_bytes);
    GLint vertex_ok = 0, fragment_ok = 0;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &vertex_ok);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &fragment_ok);
    if (!vertex_ok || !fragment_ok) {
        glDeleteShader(vertex); glDeleteShader(fragment); glDeleteProgram(holder); return 0;
    }
    /* Original public shaders may already be delete-pending. Hold their native
     * references until the corresponding executable is actually destroyed. */
    glAttachShader(holder, info->sources[0]); glAttachShader(holder, info->sources[1]);
    detach_current(program);
    glAttachShader(program, vertex); glAttachShader(program, fragment);
    for (Binding *binding = info->first_binding; binding; binding = binding->next)
        glBindAttribLocation(program, binding->index, binding->name);
    glLinkProgram(program);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok) { info->holder = holder; info->cached = 1; return 1; }
    /* Native attach has committed attribute reflection. Detach cannot reset
     * that table: compiling sources here would append a second copy. Preserve
     * the native link failure for the caller, with ownership still balanced. */
    info->holder = holder; info->cached = 1; return -1;
}
static void link_detail(GLuint program, const unsigned char key[16], const char *status,
                        uint64_t start, unsigned bytes) {
    uint64_t now = perf_now_us();
    if (now-s_detail_window >= 1000000) { s_detail_window = now; s_details = 0; }
    if (s_details++ < 4) {
        char name[37]; name_for(key, name);
        so_loga("[SHADER_CACHE] program=%u key=%s status=%s bytes=%u us=%llu",
                program, name, status, bytes, (unsigned long long)(perf_now_us()-start));
    }
}
static void cache_link(GLuint program) {
    ProgramInfo *info = program_info(program);
    unsigned char key[16];
    if (!s_ready || !info || !info->key_valid || !info->pristine || info->finished || !program_key(program, info, key)) {
        /* An attached precompiled VS has already populated vitaGL attributes.
         * Native detach does not clear that table. Reused/unknown sources must
         * keep their original link path, never attach a second reflected VS. */
        ++s_bypasses; ((void (*)(GLuint))s_link_hook.orig_func)(program);
        if (info) { info->finished = 1; free_bindings(info); }
        return;
    }
    uint64_t start = perf_now_us();
    uint32_t length = 0; GLenum format = 0;
    unsigned char *binary = load_binary(key, &length, &format);
    if (binary) {
        int restored = restore_binary(program, info, binary, length, format); free(binary);
        if (restored) {
            info->finished = 1; free_bindings(info);
            if (restored > 0) ++s_hits; else ++s_errors;
            link_detail(program, key, restored > 0 ? "hit" : "native_link_error", start, length);
            perf_record(PERF_LINK, NULL, start, length, restored > 0, restored > 0);
            return;
        }
        ++s_rejected;
    }
    ++s_misses;
    /* A failed source link may still have changed native reflection state.
     * Only the very first pristine link attempt may restore a binary. */
    info->pristine = 0;
    ((void (*)(GLuint))s_link_hook.orig_func)(program);
    GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked) { info->finished = 1; free_bindings(info); save_binary(program, key); }
    else ++s_errors;
    link_detail(program, key, linked ? "cold" : "source_link_error", start, 0);
}
static void cache_get_attached(GLuint program, GLsizei capacity, GLsizei *count, GLuint *shaders) {
    ProgramInfo *info = program_info(program);
    if (!info || !info->cached || capacity < 0 || (capacity && !shaders)) {
        glGetAttachedShaders(program, capacity, count, shaders); return;
    }
    GLsizei n = capacity < 2 ? capacity : 2;
    if (count) *count = n;
    for (GLsizei i = 0; i < n; ++i) shaders[i] = info->sources[i];
}
static void cache_delete_program(GLuint program) {
    glDeleteProgram(program);
    ProgramInfo *info = program_info(program);
    if (!info) return;
    if (!glIsProgram(program)) release_info(info);
    else if (!info->pending) { info->pending = 1; ++s_pending; }
}
static void cache_use_program(GLuint program) { glUseProgram(program); reap_pending(); }

int shader_cache_gl_init(void) {
    if (s_ready) return 0;
    if (!strcmp(ETERNAL_SHADER_CACHE_ABI, "unconfigured")) {
        so_loga("[SHADER_CACHE] disabled reason=missing_build_abi"); return 0;
    }
    MD5_CTX hash; MD5_Init(&hash);
    hash_part(&hash, ETERNAL_SHADER_CACHE_ABI, strlen(ETERNAL_SHADER_CACHE_ABI));
    GLenum names[] = { GL_VENDOR, GL_RENDERER, GL_VERSION };
    for (unsigned i = 0; i < 3; ++i) {
        const char *value = (const char *)glGetString(names[i]);
        if (!value) { so_loga("[SHADER_CACHE] disabled reason=no_gl_identity"); return 0; }
        hash_part(&hash, value, strnlen(value, 1024));
    }
    const char *compiler_paths[] = {"ur0:/data/libshacccg.suprx", "ur0:/data/external/libshacccg.suprx"};
    unsigned compilers = 0;
    unsigned char *chunk = malloc(16*1024);
    if (!chunk) return 0;
    for (unsigned i = 0; i < 2; ++i) {
        SceIoStat stat; memset(&stat, 0, sizeof(stat));
        if (sceIoGetstat(compiler_paths[i], &stat) < 0) continue;
        int fd = sceIoOpen(compiler_paths[i], SCE_O_RDONLY, 0);
        int ok = fd >= 0 && stat.st_size > 0 && stat.st_size <= 16*1024*1024;
        hash_part(&hash, compiler_paths[i], strlen(compiler_paths[i]));
        hash_part(&hash, &stat.st_size, sizeof(stat.st_size));
        uint64_t remaining = ok ? (uint64_t)stat.st_size : 0;
        while (ok && remaining) {
            unsigned n = remaining > 16*1024 ? 16*1024 : (unsigned)remaining;
            ok = !read_all(fd, chunk, n);
            if (ok) { MD5_Update(&hash, chunk, n); remaining -= n; }
        }
        if (fd >= 0 && sceIoClose(fd) < 0) ok = 0;
        if (!ok) { free(chunk); so_loga("[SHADER_CACHE] disabled reason=compiler_identity"); return 0; }
        ++compilers;
    }
    free(chunk);
    if (!compilers) { so_loga("[SHADER_CACHE] disabled reason=no_compiler_identity"); return 0; }
    finish_hash(&hash, s_abi);
    (void)sceIoMkdir(SO_CACHE_DIR, 0777); (void)sceIoMkdir(CACHE_DIRECTORY, 0777);
    int directory = sceIoDopen(CACHE_DIRECTORY);
    if (directory < 0) { so_loga("[SHADER_CACHE] disabled reason=cache_directory path=%s", CACHE_DIRECTORY); return 0; }
    unsigned used = 0; int complete = 1;
    for (;;) {
        SceIoDirent item; memset(&item, 0, sizeof(item));
        int n = sceIoDread(directory, &item);
        if (n < 0) { complete = 0; break; }
        if (!n) break;
        if (strlen(item.d_name) == 40 && !strcmp(item.d_name+36, ".tmp")) {
            char base[37]; memcpy(base, item.d_name, 36); base[36] = 0;
            if (owned_name(base)) {
                char path[256]; full_path(path, sizeof(path), item.d_name);
                if (sceIoRemove(path) < 0) { complete = 0; break; }
            }
            continue;
        }
        if (!owned_name(item.d_name)) continue;
        if (item.d_stat.st_size < 64 || item.d_stat.st_size > BINARY_MAX+64 || used == DISK_FILES ||
            (uint64_t)s_disk_bytes+(uint64_t)item.d_stat.st_size > DISK_BYTES) {
            char path[256]; full_path(path, sizeof(path), item.d_name);
            if (sceIoRemove(path) < 0) { complete = 0; break; }
            continue;
        }
        DiskInfo *entry = &s_disk[used++]; strcpy(entry->name, item.d_name);
        entry->bytes = (uint32_t)item.d_stat.st_size; entry->age = ++s_tick;
        s_disk_bytes += entry->bytes;
    }
    if (sceIoDclose(directory) < 0) complete = 0;
    if (!complete) {
        memset(s_disk, 0, sizeof(s_disk)); s_disk_bytes = 0;
        so_loga("[SHADER_CACHE] disabled reason=incomplete_inventory"); return 0;
    }
    s_ready = 1;
    so_loga("[SHADER_CACHE] enabled files=%u disk_bytes=%u max_files=%u max_bytes=%u path=%s cold_compile=original",
            used, s_disk_bytes, DISK_FILES, DISK_BYTES, CACHE_DIRECTORY);
    return 0;
}
void shader_cache_poll(int force) {
    int saved_errno = errno; uint64_t now = perf_now_us();
    if (!force && now-s_last_report < 1000000) return;
    s_last_report = now;
    if (force || s_hits || s_misses || s_saved || s_rejected || s_errors || s_bypasses)
        so_loga("[SHADER_CACHE] hits=%u misses=%u saved=%u rejected=%u errors=%u bypass=%u load_us=%llu save_us=%llu disk_bytes=%u pending_programs=%u",
                s_hits, s_misses, s_saved, s_rejected, s_errors, s_bypasses,
                (unsigned long long)s_load_us, (unsigned long long)s_save_us, s_disk_bytes, s_pending);
    s_hits=s_misses=s_saved=s_rejected=s_errors=s_bypasses=0; s_load_us=s_save_us=0;
    errno = saved_errno;
}
void shader_cache_shutdown(void) {
    shader_cache_poll(1);
    for (unsigned i = 0; i < PROGRAMS; ++i) if (s_program[i].initialized) release_info(&s_program[i]);
    memset(s_shader, 0, sizeof(s_shader)); memset(s_disk, 0, sizeof(s_disk)); s_disk_bytes = 0; s_ready = 0;
}
int soloader_patch_shader_cache(so_module *mod) {
    if (!mod) return -1;
    if (s_module) return s_module == mod ? 0 : -1;
    const struct { const char *name; void *fn; so_hook_ref *ref; } hooks[] = {
        {"glCreateShader", cache_create_shader, NULL}, {"glShaderSource", cache_source, NULL},
        {"glShaderBinary", cache_shader_binary, NULL},
        {"glCreateProgram", cache_create_program, NULL}, {"glAttachShader", cache_attach, NULL},
        {"glBindAttribLocation", cache_bind_attribute, NULL}, {"glLinkProgram", cache_link, &s_link_hook},
        {"glGetAttachedShaders", cache_get_attached, NULL}, {"glDeleteProgram", cache_delete_program, NULL},
        {"glUseProgram", cache_use_program, NULL},
    };
    for (unsigned i = 0; i < sizeof(hooks)/sizeof(hooks[0]); ++i)
        if (so_module_find_import(mod, hooks[i].name) && so_module_hook_import(mod, hooks[i].name, hooks[i].fn, hooks[i].ref) < 0) return -1;
    if (!s_link_hook.orig_func) return -1;
    s_module = mod; return 0;
}
