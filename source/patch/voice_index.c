/* Replace only the synchronous voice-existence pass, preserving native STL objects.
 * Verified original SO SHA256: c3555dd862c78d0da3c6ef2505e98a0aca64473d686441b3f75bfb2fe4526212.
 * See analysis/sound/FINDINGS.md and original ARM loop regression evidence.
 */
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <psp2/kernel/processmgr.h>
#include <so_common/so_logger.h>
#include "voice_index.h"
#include "../config.h"
#include "../utils/paths.h"
#include "../utils/resource_index.h"
#include "../utils/native_io.h"

typedef struct {
    void *cells;
    int rows, scalar_columns, string_columns, stride;
} NativeXml;
typedef struct {
    void *(*xml_get)(void *, const char *);
    void (*xml_free)(void *, void *);
    char *(*xml_string)(void *, void *, int, int);
    char *(*filename)(const char *);
    void *(*lower_bound)(const void *, char *const *);
    void *(*map_item)(void *, const unsigned *);
    void *(*string_assign)(void *, const char *, const char *);
    void *obfuscation;
    const unsigned char *application;
} NativeApi;
static NativeApi s_api;
static void (*s_original)(void *);
static so_hook_ref s_hook;
static so_module *s_module;
typedef struct {
    unsigned rows;
    char *paths[];
} VoiceSnapshot;
static _Atomic(VoiceSnapshot *) s_snapshot;
static void snapshot_free(VoiceSnapshot *snapshot) {
    if (!snapshot) return;
    for (unsigned i = 0; i < snapshot->rows; ++i) free(snapshot->paths[i]);
    free(snapshot);
}
void voice_index_prefetch_following(const char *mapped_path) {
    const VoiceSnapshot *snapshot = atomic_load_explicit(&s_snapshot, memory_order_acquire);
    if (!snapshot || !mapped_path) return;
    for (unsigned row = 0; row < snapshot->rows; ++row) {
        if (!snapshot->paths[row] || strcmp(snapshot->paths[row], mapped_path)) continue;
        unsigned queued = 0;
        for (unsigned ahead = 1; ahead <= 8 && row + ahead < snapshot->rows && queued < 3; ++ahead) {
            const char *next = snapshot->paths[row + ahead];
            if (next) { native_io_prefetch(next); ++queued; }
        }
        return;
    }
}

static uint32_t read_u32(const void *base, size_t offset) {
    uint32_t value;
    memcpy(&value, (const char *)base + offset, sizeof(value));
    return value;
}
static const char *read_pointer(const void *base, size_t offset) {
    const char *value;
    memcpy(&value, (const char *)base + offset, sizeof(value));
    return value;
}
/* This deliberately covers only the canonical relative paths used by TEXT_VALUE.
 * Other path syntax falls back to the complete original stage before any map edit. */
static int normalize_key(const char *path, char *key, size_t capacity) {
    if (!path || path[0] == '/' || strchr(path, ':')) return -1;
    while (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) path += 2;
    size_t length = strnlen(path, capacity);
    if (!length || length >= capacity) return -1;
    for (size_t i = 0; i <= length; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        key[i] = (char)c;
    }
    if (strstr(key, "//") || !strcmp(key, ".") || !strcmp(key, "..") ||
        strstr(key, "/./") || strstr(key, "/../")) return -1;
    return 0;
}

static int copy_present_path(int present, const char *physical, char *out, size_t capacity) {
    if (present != 1 || !out) return present;
    size_t length = strlen(physical);
    if (length >= capacity) return -1;
    memcpy(out, physical, length + 1);
    return present;
}
static int indexed_presence(const char *path, char *out, size_t capacity) {
    char key[PATH_MAPPED_MAX], mapped[PATH_MAPPED_MAX];
    if (normalize_key(path, key, sizeof(key)) < 0) return -1;
    char *query = key;
    void *node = s_api.lower_bound(s_api.obfuscation, &query);
    const char *target = path;
    if (node && node != s_api.obfuscation) {
        const char *candidate = read_pointer(node, 36);
        if (!candidate) return -1;
        if (!strcmp(candidate, key)) {
            target = read_pointer(node, 60);
            if (!target || strnlen(target, sizeof(mapped)) >= sizeof(mapped)) return -1;
            const char *physical = paths_from_android(target, mapped, sizeof(mapped));
            return physical ? copy_present_path(resource_index_lookup(physical, NULL), physical, out, capacity) : -1;
        }
    }
    const char *physical = paths_from_android(target, mapped, sizeof(mapped));
    if (!physical) return -1;
    int present = resource_index_lookup(physical, NULL);
    if (present >= 0) return copy_present_path(present, physical, out, capacity);
    /* A complete top-level inventory also proves a loose subtree impossible when
     * its first component is absent. Existing directories remain uncovered. */
    const size_t prefix = sizeof(GAME_DATA_DIR) - 1;
    if (strncmp(physical, GAME_DATA_DIR, prefix)) return -1;
    const char *first = physical + prefix;
    const char *slash = strchr(first, '/');
    if (!slash || slash == first || (size_t)(slash - first) + prefix >= sizeof(mapped)) return -1;
    char parent[PATH_MAPPED_MAX];
    size_t parent_length = (size_t)(slash - physical);
    memcpy(parent, physical, parent_length);
    parent[parent_length] = 0;
    return resource_index_lookup(parent, NULL) == 0 ? 0 : -1;
}

static void voice_update(void *game) {
    if (read_u32(game, 0xc8) != 14) { s_original(game); return; }
    uint64_t begin = sceKernelGetProcessTimeWide();
    if (!s_api.application[196] ||
        resource_index_lookup(GAME_DATA_DIR "file000000.dat", NULL) != 1 ||
        !read_u32(s_api.obfuscation, 16)) {
        so_loga("[VOICE_INDEX] fallback reason=index_unavailable rows=0 us=%llu",
                (unsigned long long)(sceKernelGetProcessTimeWide() - begin));
        s_original(game);
        return;
    }
    /* KResource::getInstance returns zero; its methods do not consume this. The
     * original loop passes addresses of one-byte KResource facade objects. */
    unsigned char resource = 0;
    NativeXml *xml = s_api.xml_get(&resource, "data/odsbinary/TEXT_VALUE.she");
    uint64_t loaded = sceKernelGetProcessTimeWide();
    unsigned present = 0, missing = 0, rows = 0;
    unsigned char *plan = NULL;
    const char **paths = NULL;
    VoiceSnapshot *snapshot = NULL;
    const char *reason = "table_unavailable";
    if (!xml) goto fallback;
    if (!xml->cells || xml->rows < 0 || xml->rows > 65536 ||
        xml->scalar_columns != 1 || xml->string_columns != 3 || xml->stride != 4) {
        reason = "table_shape";
        goto fallback;
    }
    rows = (unsigned)xml->rows;
    plan = malloc(rows ? rows : 1);
    paths = malloc((rows ? rows : 1) * sizeof(*paths));
    snapshot = malloc(sizeof(*snapshot) + rows * sizeof(snapshot->paths[0]));
    if (snapshot) { snapshot->rows = rows; memset(snapshot->paths, 0, rows * sizeof(snapshot->paths[0])); }
    if (!plan || !paths || !snapshot) { reason = "allocation"; goto fallback; }
    for (unsigned row = 0; row < rows; ++row) {
        const char *raw = s_api.xml_string(&resource, xml, (int)row, 0);
        size_t length = raw ? strnlen(raw, PATH_MAPPED_MAX) : 0;
        if (length < 3 || length >= PATH_MAPPED_MAX) { reason = "path_shape"; goto fallback; }
        paths[row] = s_api.filename(raw);
        char physical[PATH_MAPPED_MAX];
        int exists = indexed_presence(paths[row], physical, sizeof(physical));
        if (exists < 0) { reason = "path_uncovered"; goto fallback; }
        plan[row] = (unsigned char)exists;
        if (exists) {
            ++present;
            size_t length = strlen(physical) + 1;
            snapshot->paths[row] = malloc(length);
            if (!snapshot->paths[row]) { reason = "allocation"; goto fallback; }
            memcpy(snapshot->paths[row], physical, length);
        } else ++missing;
    }
    uint64_t resolved = sceKernelGetProcessTimeWide();
    void *map = (char *)game + 0xb28;
    for (unsigned row = 0; row < rows; ++row) {
        if (!plan[row]) continue;
        void *value = s_api.map_item(map, &row);
        s_api.string_assign(value, paths[row], paths[row] + strlen(paths[row]));
    }
    ((unsigned char *)game)[0xb58] = 1;
    uint32_t next_state = 15;
    memcpy((char *)game + 0xc8, &next_state, sizeof(next_state));
    VoiceSnapshot *previous = NULL;
    if (!atomic_compare_exchange_strong_explicit(&s_snapshot, &previous, snapshot,
                                                memory_order_release, memory_order_relaxed))
        snapshot_free(snapshot);
    snapshot = NULL;
    uint64_t built = sceKernelGetProcessTimeWide();
    s_api.xml_free(&resource, xml);
    free(paths);
    free(plan);
    uint64_t finished = sceKernelGetProcessTimeWide();
    so_loga("[VOICE_INDEX] rows=%u present=%u missing=%u map_items=%u load_us=%llu resolve_us=%llu build_us=%llu free_us=%llu total_us=%llu",
            rows, present, missing, read_u32(map, 16),
            (unsigned long long)(loaded - begin), (unsigned long long)(resolved - loaded),
            (unsigned long long)(built - resolved), (unsigned long long)(finished - built),
            (unsigned long long)(finished - begin));
    return;
fallback:
    snapshot_free(snapshot);
    if (xml) s_api.xml_free(&resource, xml);
    free(paths);
    free(plan);
    so_loga("[VOICE_INDEX] fallback reason=%s rows=%u present=%u missing=%u load_us=%llu total_us=%llu",
            reason, rows, present, missing, (unsigned long long)(loaded - begin),
            (unsigned long long)(sceKernelGetProcessTimeWide() - begin));
    s_original(game);
}

int soloader_patch_voice_index(so_module *mod) {
    if (!mod) return -1;
    if (s_module) return s_module == mod ? 0 : -1;
    intptr_t bias = so_module_get_load_bias(mod);
    /* Symbol addresses plus guarded instructions pin the known ELF/ABI layout.
     * No native object offsets or calls are used for an unsupported library. */
    if (so_module_get_load_size(mod) < 0x606fa0 ||
        (uintptr_t)so_module_find_export(mod, "_ZN9GameState21Update_Global_LoadingEv") != (uintptr_t)bias + 0x2467f1 ||
        (uintptr_t)so_module_find_export(mod, "_ZN6glitch2io11CFileSystem14ObfuscationMapE") != (uintptr_t)bias + 0x606f8c) {
        so_loga("[VOICE_INDEX] disabled reason=unsupported_so_symbols");
        return 0;
    }
    static const struct { uint32_t offset; unsigned char bytes[16]; unsigned length; } guards[] = {
        {0x2467f0,{0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0xc4,0x4d,0x04,0x1c},16},
        {0x246c2c,{0x03,0x98,0x04,0x99,0xf9,0xf7,0x10,0xff,0xb0,0x42,0x03,0xd0},12},
        {0x246d84,{0x58,0x0b,0,0},4}, {0x246d8c,{0x28,0x0b,0,0},4},
        {0x38d18c,{0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x29,0x4a,0x2a,0x4b},16},
        {0x240a54,{0xf0,0xb5,0x4f,0x46,0x46,0x46,0xc0,0xb4,0x29,0x4d,0x2a,0x4a,0x91,0xb0,0x7d,0x44},16},
        {0x240bf4,{0xf8,0xb5,0x03,0x69,0x04,0x1c,0x40,0x69,0x16,0x1c,0x57,0x1a},12},
        {0x303c98,{0x10,0xb5,0x0c,0x69,0x20,0x1c,0x50,0x43,0x8a,0x68,0xc3,0x18,0x9b,0x18,0x0a,0x68},16},
        {0x3044b8,{0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0xc5,0x4a,0x93,0xb0},16},
        {0x3047f4,{0x70,0xb5,0x4a,0x68,0x0c,0x1c,0,0x2a,0x17,0xdd,0xcb,0x68,0,0x26,0,0x25},16},
        {0x30e17c,{0x70,0xb5,0x82,0xb0,0,0x23,0x04,0x1c,0x01,0x93},10},
    };
    for (unsigned i = 0; i < sizeof(guards) / sizeof(guards[0]); ++i)
        if (memcmp((const void *)(bias + guards[i].offset), guards[i].bytes, guards[i].length)) {
            so_loga("[VOICE_INDEX] disabled reason=target_bytes offset=%08x", guards[i].offset);
            return 0;
        }
    s_api.xml_get = (void *(*)(void *,const char *))(bias + 0x3044b9);
    s_api.xml_free = (void (*)(void *,void *))(bias + 0x3047f5);
    s_api.xml_string = (char *(*)(void *,void *,int,int))(bias + 0x303c99);
    s_api.filename = (char *(*)(const char *))(bias + 0x30e17d);
    s_api.lower_bound = (void *(*)(const void *,char *const *))(bias + 0x38d18d);
    s_api.map_item = (void *(*)(void *,const unsigned *))(bias + 0x240a55);
    s_api.string_assign = (void *(*)(void *,const char *,const char *))(bias + 0x240bf5);
    s_api.obfuscation = (void *)(bias + 0x606f8c);
    s_api.application = (const unsigned char *)(bias + 0x5d0a20);
    if (so_module_hook_address(mod, 0x2467f1, voice_update, &s_hook) < 0) return -1;
    if (!s_hook.orig_func) {
        int rollback = so_unhook(&s_hook);
        memset(&s_api, 0, sizeof(s_api));
        so_loga("[VOICE_INDEX] disabled reason=missing_original rollback=%d", rollback);
        return -1;
    }
    s_original = (void (*)(void *))s_hook.orig_func;
    s_module = mod;
    so_loga("[VOICE_INDEX] installed supported_so=c3555dd862c78d0d state=14");
    return 0;
}
