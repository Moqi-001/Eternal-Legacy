/* Immutable bytes are shared; every funopen FILE owns an independent cursor.
 * Disk reads and FILE creation never run under the cache mutex. */
#include "native_io.h"
#include "resource_index.h"
#include "paths.h"
#include "perf.h"
#include "../config.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <so_common/so_logger.h>
#include <so_libs/libc/bionic_stdio.h>
#include <so_libs/libc/bionic_fcntl.h>

#define CACHE_BYTES (16u * 1024u * 1024u)
#define CACHE_FILE_MAX (2u * 1024u * 1024u)
#define CACHE_SLOTS 256
#define CACHE_PATH_MAX 512
typedef struct {
    char path[CACHE_PATH_MAX];
    unsigned char *data;
    size_t size;
    unsigned refs;
    uint64_t touched;
    int state; /* 0 empty, 1 reserved/loading, 2 ready */
    int retired; /* Existing cursors retain bytes; new cursors use native I/O. */
    unsigned warm; /* One-based index into the bounded startup hot set. */
} CacheEntry;
typedef struct { CacheEntry *entry; int64_t position; } MemoryCursor;
static CacheEntry s_cache[CACHE_SLOTS];
static pthread_mutex_t s_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t s_cache_bytes;
static uint64_t s_cache_tick, s_cache_report;
static unsigned s_cache_hits, s_cache_misses, s_cache_negative, s_cache_evicts;
static unsigned s_cache_bypass, s_cache_errors, s_cache_streams;
static unsigned s_cache_used, s_cache_peak, s_evict_slots, s_evict_bytes;
#define WARM_SLOTS 16
#define WARM_BYTES (256u * 1024u)
static struct { char path[CACHE_PATH_MAX]; unsigned state; size_t size; } s_warm[WARM_SLOTS];
static unsigned s_warm_count; /* states: 0=pending, 1=ready, 2=failed */
typedef struct MutablePath {
    struct MutablePath *next;
    int pending;
    char path[CACHE_PATH_MAX];
} MutablePath;
/* Only successful writable opens permanently retire their own resource path.
 * A pending open blocks reuse briefly but is removed again on open failure. */
static MutablePath *s_mutable_paths;
static unsigned s_invalidations, s_mutable_path_count;
#define PREFETCH_SLOTS 16
static char s_prefetch_paths[PREFETCH_SLOTS][CACHE_PATH_MAX], s_prefetch_active[CACHE_PATH_MAX];
static unsigned s_prefetch_head, s_prefetch_count, s_prefetch_submitted, s_prefetch_dropped;
static unsigned s_prefetch_loaded, s_prefetch_reused, s_prefetch_bypass, s_prefetch_errors;
static int s_prefetch_started, s_prefetch_stopping;
static pthread_t s_prefetch_thread;
static pthread_mutex_t s_prefetch_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_prefetch_cond = PTHREAD_COND_INITIALIZER;

static void discard(CacheEntry *entry);

/* The observed readonly modes have identical Vita newlib permissions. Unknown
 * readonly suffixes stay native (e.g. x may add open flags). Newlib accepts any
 * r/w/a first character and '+' anywhere as a potential writable stream. */
static int mode_access(const char *mode) {
    if (!mode || !*mode || !strchr("rwa", mode[0])) return -1;
    if (mode[0] != 'r' || strchr(mode + 1, '+')) return 0;
    return !strcmp(mode, "r") || !strcmp(mode, "rb") || !strcmp(mode, "rt") ? 1 : -1;
}

static int ascii_equal_prefix(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return 1;
}

/* Also stop inferred child negatives when a previously absent parent name was
 * created. Case aliases stay native and cannot leave an exact-case stale hit. */
static int mutable_path_locked(const char *path) {
    size_t length = strlen(path);
    for (MutablePath *entry = s_mutable_paths; entry; entry = entry->next) {
        size_t n = strlen(entry->path);
        if (length >= n && ascii_equal_prefix(path, entry->path, n) &&
            (!path[n] || path[n] == '/')) return 1;
    }
    return 0;
}

static int immutable_name(const char *path) {
    const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
    const char *extension = strrchr(name, '.');
    return extension && (!strcmp(extension, ".dat") || !strcmp(extension, ".ttf") ||
                         !strcmp(extension, ".TTF") || !strcmp(extension, ".tga") ||
                         !strcmp(extension, ".png") || !strcmp(extension, ".mp4"));
}

static int memory_read(void *cookie, char *dst, int count) {
    MemoryCursor *cursor = cookie;
    if (count < 0) { errno = EINVAL; return -1; }
    if ((uint64_t)cursor->position >= cursor->entry->size) return 0;
    size_t available = cursor->entry->size - (size_t)cursor->position;
    size_t n = (size_t)count < available ? (size_t)count : available;
    memcpy(dst, cursor->entry->data + cursor->position, n);
    cursor->position += n;
    return (int)n;
}

static fpos_t memory_seek(void *cookie, fpos_t offset, int whence) {
    MemoryCursor *cursor = cookie;
    int64_t base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = cursor->position;
    else if (whence == SEEK_END) base = cursor->entry->size;
    else { errno = EINVAL; return (fpos_t)-1; }
    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && offset < -base)) { errno = EINVAL; return (fpos_t)-1; }
    int64_t position = base + offset;
    if ((int64_t)(fpos_t)position != position) { errno = EOVERFLOW; return (fpos_t)-1; }
    cursor->position = position;
    return (fpos_t)position;
}

static int memory_close(void *cookie) {
    int saved_errno = errno;
    MemoryCursor *cursor = cookie;
    pthread_mutex_lock(&s_cache_lock);
    --cursor->entry->refs;
    --s_cache_streams;
    cursor->entry->touched = ++s_cache_tick;
    if (cursor->entry->retired && !cursor->entry->refs) discard(cursor->entry);
    pthread_mutex_unlock(&s_cache_lock);
    free(cursor);
    errno = saved_errno;
    return 0;
}

/* Caller already holds one reference. Failure also releases that reference. */
static FILE *memory_file(CacheEntry *entry) {
    MemoryCursor *cursor = malloc(sizeof(*cursor));
    if (!cursor) {
        pthread_mutex_lock(&s_cache_lock);
        --entry->refs;
        if (entry->retired && !entry->refs) discard(entry);
        pthread_mutex_unlock(&s_cache_lock);
        errno = ENOMEM;
        return NULL;
    }
    cursor->entry = entry; cursor->position = 0;
    pthread_mutex_lock(&s_cache_lock);
    ++s_cache_streams;
    pthread_mutex_unlock(&s_cache_lock);
    FILE *file = funopen(cursor, memory_read, NULL, memory_seek, memory_close);
    if (!file) { int error = errno; memory_close(cursor); errno = error; return NULL; }
    setvbuf(file, NULL, _IOFBF, 16 * 1024);
    return file;
}

static void discard(CacheEntry *entry) {
    if (entry->warm) s_warm[entry->warm - 1].state = 2;
    --s_cache_used;
    s_cache_bytes -= entry->size;
    free(entry->data);
    memset(entry, 0, sizeof(*entry));
}

int native_io_immutable_asset(const char *path, size_t expected_size) {
    int saved_errno = errno, valid = 0;
    char normalized[CACHE_PATH_MAX];
    const char *mapped = path ? paths_normalize_resource(path, normalized, sizeof(normalized)) : NULL;
    if (mapped && immutable_name(mapped)) {
        pthread_mutex_lock(&s_cache_lock);
        int mutable = mutable_path_locked(mapped);
        pthread_mutex_unlock(&s_cache_lock);
        size_t size;
        valid = !mutable && resource_index_lookup(mapped, &size) == 1 && size == expected_size;
        pthread_mutex_lock(&s_cache_lock);
        if (mutable_path_locked(mapped)) valid = 0;
        pthread_mutex_unlock(&s_cache_lock);
    }
    errno = saved_errno;
    return valid;
}

/* Byte reuse remains restricted to immutable top-level assets. Nested resource
 * and ZIP-parent absence is independent of file suffix: a missing component in
 * a complete directory proves its remaining children and later parent probes.
 * Unrelated standalone root metadata still uses native stdio. */
static int indexed_resource(const char *path, size_t *size) {
    int indexed = resource_index_lookup(path, size);
    if (indexed >= 0 && immutable_name(path)) return indexed;
    return resource_index_path_type(path) == RESOURCE_TYPE_ABSENT ? 0 : -1;
}

int native_io_known_absent(const char *path) {
    int saved_errno = errno;
    char normalized[CACHE_PATH_MAX];
    const char *mapped = path ? paths_normalize_resource(path, normalized, sizeof(normalized)) : NULL;
    if (!mapped) { errno = saved_errno; return 0; }
    pthread_mutex_lock(&s_cache_lock);
    int mutable = mutable_path_locked(mapped);
    pthread_mutex_unlock(&s_cache_lock);
    int absent = !mutable && indexed_resource(mapped, NULL) == 0;
    pthread_mutex_lock(&s_cache_lock);
    if (mutable_path_locked(mapped)) absent = 0;
    pthread_mutex_unlock(&s_cache_lock);
    errno = saved_errno;
    return absent;
}

/* Returns a pinned immutable entry, or requests native stdio fallback. No FILE
 * allocation occurs here, so background prefetch never touches newlib's pool. */
static CacheEntry *cache_pin(const char *path, const char *mode, int *hit, int *fallback) {
    int saved_errno = errno;
    if (hit) *hit = 0;
    *fallback = 0;
    if (!path || !mode) { errno = EINVAL; return NULL; }
    size_t size = 0;
    int readonly = mode_access(mode) == 1;
    pthread_mutex_lock(&s_cache_lock);
    int mutable = mutable_path_locked(path);
    pthread_mutex_unlock(&s_cache_lock);
    /* Index walks can enumerate a loose directory, so never hold the byte-cache
     * mutex across them. Recheck mutation state before using their evidence. */
    int directory = !mutable && readonly && resource_index_type(path, NULL) == RESOURCE_TYPE_DIRECTORY;
    int indexed = !mutable && readonly ? indexed_resource(path, &size) : -1;
    pthread_mutex_lock(&s_cache_lock);
    if (mutable_path_locked(path)) {
        ++s_cache_bypass;
        pthread_mutex_unlock(&s_cache_lock);
        *fallback = 1;
        return NULL;
    }
    if (directory) {
        pthread_mutex_unlock(&s_cache_lock);
        errno = EISDIR;
        return NULL;
    }
    if (indexed == 0) {
        ++s_cache_negative;
        pthread_mutex_unlock(&s_cache_lock);
        if (hit) *hit = 2;
        errno = ENOENT;
        return NULL;
    }
    if (indexed < 0 || size > CACHE_FILE_MAX || strlen(path) >= CACHE_PATH_MAX) {
        ++s_cache_bypass;
        pthread_mutex_unlock(&s_cache_lock);
        *fallback = 1;
        return NULL;
    }
    CacheEntry *slot = NULL;
    for (unsigned i = 0; i < CACHE_SLOTS; ++i) {
        CacheEntry *entry = &s_cache[i];
        if (!entry->state && !slot) slot = entry;
        if (entry->state && !entry->retired && !strcmp(entry->path, path)) {
            if (entry->state == 2) {
                ++entry->refs; entry->touched = ++s_cache_tick; ++s_cache_hits;
                pthread_mutex_unlock(&s_cache_lock);
                if (hit) *hit = 1;
                errno = saved_errno;
                return entry;
            }
            /* Another thread is loading it: no cache-wide I/O lock or wait. */
            ++s_cache_bypass;
            pthread_mutex_unlock(&s_cache_lock);
            *fallback = 1;
            return NULL;
        }
    }
    while (!slot || size > CACHE_BYTES - s_cache_bytes) {
        int slot_pressure = !slot;
        int byte_pressure = size > CACHE_BYTES - s_cache_bytes;
        CacheEntry *victim = NULL;
        for (unsigned i = 0; i < CACHE_SLOTS; ++i) {
            CacheEntry *entry = &s_cache[i];
            if (entry->state == 2 && !entry->refs && !entry->warm &&
                (!victim || entry->touched < victim->touched)) victim = entry;
        }
        if (!victim) {
            ++s_cache_bypass;
            pthread_mutex_unlock(&s_cache_lock);
            *fallback = 1;
            return NULL;
        }
        discard(victim); ++s_cache_evicts;
        if (slot_pressure) ++s_evict_slots;
        if (byte_pressure) ++s_evict_bytes;
        if (!slot) slot = victim;
    }
    slot->state = 1; slot->size = size;
    ++s_cache_used;
    if (s_cache_used > s_cache_peak) s_cache_peak = s_cache_used;
    for (unsigned i = 0; i < s_warm_count; ++i)
        if (!strcmp(s_warm[i].path, path)) { slot->warm = i + 1; break; }
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    s_cache_bytes += size; ++s_cache_misses;
    pthread_mutex_unlock(&s_cache_lock);

    unsigned char *data = malloc(size ? size : 1);
    int error = data ? 0 : ENOMEM;
    int fd = data ? sceIoOpen(path, SCE_O_RDONLY, 0) : -1;
    if (data && fd < 0) error = EIO;
    size_t done = 0;
    while (fd >= 0 && done < size) {
        size_t amount = size - done;
        if (amount > 64 * 1024) amount = 64 * 1024;
        int n = sceIoRead(fd, data + done, amount);
        if (n <= 0 || (size_t)n > amount) { error = EIO; break; }
        done += n;
    }
    if (fd >= 0 && sceIoClose(fd) < 0 && !error) error = EIO;
    pthread_mutex_lock(&s_cache_lock);
    if (error) {
        free(data); discard(slot); ++s_cache_errors;
        pthread_mutex_unlock(&s_cache_lock);
        /* Preserve native stdio failure/errno when the device reports an error. */
        if (error != ENOMEM) { *fallback = 1; return NULL; }
        errno = ENOMEM; return NULL;
    }
    if (slot->retired || mutable_path_locked(path)) {
        free(data); discard(slot); ++s_cache_bypass;
        pthread_mutex_unlock(&s_cache_lock);
        *fallback = 1;
        return NULL;
    }
    slot->data = data; slot->state = 2; slot->refs = 1;
    if (slot->warm) s_warm[slot->warm - 1].state = 1;
    slot->touched = ++s_cache_tick;
    pthread_mutex_unlock(&s_cache_lock);
    errno = saved_errno;
    return slot;
}

/* Only verified bgmpack streams are sequential. data.pack first walks thousands
 * of short ZIP headers separated by seeks: newlib refills inside fseek, so a
 * 64KiB buffer amplifies this scan ~60x. Keep its native buffer and cache the
 * decompressed entries instead. Fonts also retain their random-access policy. */
static int sequential_stream(const char *path) {
    static const char *const files[] = {
        "file000040.dat", "file000095.dat", "file000118.dat",
        "file000212.dat", "file000241.dat", "file000375.dat", "file001002.dat",
        "file001076.dat", "file001178.dat", "file001196.dat", "file001332.dat",
        "file000959.dat", "file001517.dat", "file001653.dat", "file001668.dat",
        "file001789.dat", "file001810.dat", "file001843.dat", "file001853.dat",
        "file002062.dat", "file002166.dat", "file002256.dat", "file002372.dat",
        "file002468.dat", "file002475.dat", "file002477.dat", "file002515.dat"
    };
    const size_t prefix = sizeof(GAME_DATA_DIR) - 1;
    if (strncmp(path, GAME_DATA_DIR, prefix)) return 0;
    size_t size;
    if (resource_index_lookup(path, &size) != 1 || size <= CACHE_FILE_MAX) return 0;
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        if (!strcmp(path + prefix, files[i])) return 1;
    return 0;
}

/* Write aliases do not become read-cache keys. They only retire an equivalent
 * known key after the native open succeeds (including ux0:/ vs ux0: spelling). */
static int resource_write_key(const char *path, char *key) {
    size_t length = strlen(path), prefix = sizeof(GAME_DATA_DIR) - 1;
    if (length >= CACHE_PATH_MAX) return 0;
    if ((length == prefix - 1 && ascii_equal_prefix(path, GAME_DATA_DIR, length)) ||
        (length >= prefix && ascii_equal_prefix(path, GAME_DATA_DIR, prefix))) {
        strcpy(key, path); return 1;
    }
    const char *colon = strchr(GAME_DATA_DIR, ':');
    size_t device = colon ? (size_t)(colon - GAME_DATA_DIR) + 1 : 0;
    if (device && GAME_DATA_DIR[device] != '/' && length > prefix && path[device] == '/' &&
        ascii_equal_prefix(path, GAME_DATA_DIR, device) &&
        ascii_equal_prefix(path + device + 1, &GAME_DATA_DIR[device], prefix - device)) {
        memcpy(key, path, device); strcpy(key + device, path + device + 1); return 1;
    }
    return 0;
}

/* Resolve /var-style host aliases and device ux0:/ spellings against the same
 * canonical root. Call before remove/rename, while the old source still exists. */
static int resolved_write_key(const char *resolved, char *key) {
    if (resource_write_key(resolved, key)) return 1;
    char *root = realpath(GAME_DATA_DIR, NULL);
    int scoped = 0;
    if (root) {
        size_t n = strlen(root);
        while (n && root[n - 1] == '/') --n;
        size_t length = strlen(resolved);
        if (length >= n && ascii_equal_prefix(resolved, root, n) &&
            (!resolved[n] || resolved[n] == '/')) {
            const char *suffix = resolved + n + (length > n);
            size_t prefix = sizeof(GAME_DATA_DIR) - 1;
            if (prefix + strlen(suffix) < CACHE_PATH_MAX) {
                memcpy(key, GAME_DATA_DIR, prefix); strcpy(key + prefix, suffix); scoped = 1;
            }
        }
    }
    free(root);
    return scoped;
}

static void trim_write_key(char *key) {
    size_t length = strlen(key), root = sizeof(GAME_DATA_DIR) - 2;
    while (length > root && key[length - 1] == '/') key[--length] = 0;
}

static int mutation_begin(const char *path, MutablePath **out) {
    *out = NULL;
    char key[CACHE_PATH_MAX];
    if (!resource_write_key(path, key)) return 0;
    MutablePath *pending = malloc(sizeof(*pending));
    if (!pending) { errno = ENOMEM; return -1; }
    char *resolved = realpath(path, NULL);
    char resolved_key[CACHE_PATH_MAX];
    if (resolved && resolved_write_key(resolved, resolved_key)) strcpy(key, resolved_key);
    free(resolved);
    char canonical[CACHE_PATH_MAX];
    const char *normalized = paths_normalize_resource(key, canonical, sizeof(canonical));
    if (normalized) strcpy(key, normalized);
    trim_write_key(key);
    strcpy(pending->path, key); pending->pending = 1;
    pthread_mutex_lock(&s_cache_lock);
    pending->next = s_mutable_paths; s_mutable_paths = pending;
    pthread_mutex_unlock(&s_cache_lock);
    resource_index_invalidate(pending->path);
    *out = pending;
    return 0;
}

static void mutation_finish(MutablePath *pending, const char *path, int success) {
    if (!pending) return;
    int saved_errno = errno;
    char resolved_key[CACHE_PATH_MAX];
    char *resolved = success ? realpath(path, NULL) : NULL;
    int scoped = !resolved || resolved_write_key(resolved, resolved_key);
    pthread_mutex_lock(&s_cache_lock);
    if (!success || !scoped) {
        MutablePath **link = &s_mutable_paths;
        while (*link != pending) link = &(*link)->next;
        *link = pending->next;
    } else {
        if (resolved) strcpy(pending->path, resolved_key);
        trim_write_key(pending->path);
        path = pending->path;
        pending->pending = 0;
        ++s_mutable_path_count; ++s_invalidations;
        /* Keep one permanent record per path even if saves reopen it often. */
        for (MutablePath *other = s_mutable_paths; other; other = other->next) {
            if (other != pending && !other->pending && strlen(other->path) == strlen(path) &&
                ascii_equal_prefix(other->path, path, strlen(path))) {
                MutablePath **link = &s_mutable_paths;
                while (*link != pending) link = &(*link)->next;
                *link = pending->next; --s_mutable_path_count;
                break;
            }
        }
        for (unsigned i = 0; i < CACHE_SLOTS; ++i) {
            CacheEntry *entry = &s_cache[i];
            size_t n = strlen(path);
            if (entry->state && strlen(entry->path) >= n && ascii_equal_prefix(entry->path, path, n) &&
                (!entry->path[n] || entry->path[n] == '/')) {
                entry->retired = 1;
                if (entry->state == 2 && !entry->refs) discard(entry);
            }
        }
    }
    int keep_pending = 0;
    for (MutablePath *entry = s_mutable_paths; entry; entry = entry->next)
        if (entry == pending) { keep_pending = 1; break; }
    /* Successful paths keep their permanent guard while snapshots are retired;
     * unsuccessful operations only discard prior scans, then release the guard. */
    char invalidated[CACHE_PATH_MAX];
    strcpy(invalidated, pending->path);
    pthread_mutex_unlock(&s_cache_lock);
    resource_index_invalidate(invalidated);
    free(resolved);
    if (!keep_pending) free(pending);
    errno = saved_errno;
}

int native_io_mutation_begin(const char *path, void **token) {
    if (!token) { errno = EINVAL; return -1; }
    *token = NULL;
    if (!path) { errno = EINVAL; return -1; }
    MutablePath *pending;
    if (mutation_begin(path, &pending) < 0) return -1;
    *token = pending;
    return 0;
}

void native_io_mutation_end(void *token, const char *path, int success) {
    mutation_finish(token, path, success);
}

static FILE *writable_file(const char *path, const char *mode) {
    MutablePath *pending;
    if (mutation_begin(path, &pending) < 0) return NULL;
    FILE *file = fopen_soloader(path, mode);
    int saved_errno = errno;
    mutation_finish(pending, path, file != NULL);
    errno = saved_errno;
    return file;
}

FILE *native_io_fopen(const char *path, const char *mode, int *hit) {
    if (hit) *hit = 0;
    if (!path || !mode) { errno = EINVAL; return NULL; }
    char normalized[CACHE_PATH_MAX];
    const char *mapped = paths_normalize_resource(path, normalized, sizeof(normalized));
    if (!mapped) return NULL;
    FILE *file;
    if (mode_access(mode) == 0) {
        pthread_mutex_lock(&s_cache_lock); ++s_cache_bypass; pthread_mutex_unlock(&s_cache_lock);
        file = writable_file(mapped, mode);
    }
    else {
        int fallback;
        CacheEntry *entry = cache_pin(mapped, mode, hit, &fallback);
        file = entry ? memory_file(entry) : fallback ? fopen_soloader(mapped, mode) : NULL;
        if (file && fallback && mode_access(mode) == 1 && sequential_stream(mapped)) {
            int saved_errno = errno;
            (void)setvbuf(file, NULL, _IOFBF, 64 * 1024);
            errno = saved_errno;
        }
    }
    return file;
}

int native_io_open(const char *path, int flags, int mode) {
    char normalized[CACHE_PATH_MAX];
    const char *mapped = path ? paths_normalize_resource(path, normalized, sizeof(normalized)) : NULL;
    if (!mapped) { if (!path) errno = EINVAL; return -1; }
    int writable = flags & (BIONIC_O_WRONLY | BIONIC_O_RDWR | BIONIC_O_CREAT | BIONIC_O_TRUNC | BIONIC_O_APPEND);
    if (!writable && native_io_known_absent(mapped)) { errno = ENOENT; return -1; }
    MutablePath *pending = NULL;
    if (writable && mutation_begin(mapped, &pending) < 0) return -1;
    int fd = open_soloader(mapped, flags, mode), saved_errno = errno;
    if (writable) mutation_finish(pending, mapped, fd >= 0);
    errno = saved_errno;
    return fd;
}

static void *prefetch_worker(void *unused) {
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&s_prefetch_lock);
        while (!s_prefetch_count && !s_prefetch_stopping)
            pthread_cond_wait(&s_prefetch_cond, &s_prefetch_lock);
        if (!s_prefetch_count && s_prefetch_stopping) {
            pthread_mutex_unlock(&s_prefetch_lock);
            return NULL;
        }
        char path[CACHE_PATH_MAX];
        strcpy(path, s_prefetch_paths[s_prefetch_head]);
        strcpy(s_prefetch_active, path);
        s_prefetch_head = (s_prefetch_head + 1) % PREFETCH_SLOTS;
        --s_prefetch_count;
        pthread_mutex_unlock(&s_prefetch_lock);
        int hit, fallback;
        CacheEntry *entry = cache_pin(path, "rb", &hit, &fallback);
        if (entry) {
            pthread_mutex_lock(&s_cache_lock);
            --entry->refs;
            entry->touched = ++s_cache_tick;
            if (entry->retired && !entry->refs) discard(entry);
            pthread_mutex_unlock(&s_cache_lock);
        } else {
            pthread_mutex_lock(&s_cache_lock);
            for (unsigned i = 0; i < s_warm_count; ++i)
                if (!strcmp(s_warm[i].path, path) && s_warm[i].state != 1) s_warm[i].state = 2;
            pthread_mutex_unlock(&s_cache_lock);
        }
        pthread_mutex_lock(&s_prefetch_lock);
        if (entry && hit) ++s_prefetch_reused;
        else if (entry) ++s_prefetch_loaded;
        else if (fallback) ++s_prefetch_bypass;
        else ++s_prefetch_errors;
        s_prefetch_active[0] = 0;
        pthread_mutex_unlock(&s_prefetch_lock);
    }
}

static void warm_failed(const char *path) {
    pthread_mutex_lock(&s_cache_lock);
    for (unsigned i = 0; i < s_warm_count; ++i)
        if (!strcmp(s_warm[i].path, path) && s_warm[i].state != 1) s_warm[i].state = 2;
    pthread_mutex_unlock(&s_cache_lock);
}

void native_io_prefetch(const char *path) {
    int saved_errno = errno;
    char normalized[CACHE_PATH_MAX];
    const char *mapped = path ? paths_normalize_resource(path, normalized, sizeof(normalized)) : NULL;
    size_t size;
    if (!mapped || strlen(mapped) >= CACHE_PATH_MAX || !immutable_name(mapped) || resource_index_lookup(mapped, &size) != 1 ||
        size > CACHE_FILE_MAX) { if (mapped) warm_failed(mapped); errno = saved_errno; return; }
    path = mapped;
    pthread_mutex_lock(&s_cache_lock);
    int mutable = mutable_path_locked(path);
    int cached = mutable;
    for (unsigned i = 0; i < CACHE_SLOTS; ++i)
        if (s_cache[i].state && !s_cache[i].retired && !strcmp(s_cache[i].path, path)) { cached = 1; break; }
    pthread_mutex_unlock(&s_cache_lock);
    if (cached) { if (mutable) warm_failed(path); errno = saved_errno; return; }
    pthread_mutex_lock(&s_prefetch_lock);
    int duplicate = !strcmp(s_prefetch_active, path);
    for (unsigned i = 0; i < s_prefetch_count && !duplicate; ++i)
        duplicate = !strcmp(s_prefetch_paths[(s_prefetch_head + i) % PREFETCH_SLOTS], path);
    if (duplicate || s_prefetch_stopping) {
        int stopped = s_prefetch_stopping;
        pthread_mutex_unlock(&s_prefetch_lock);
        if (stopped) warm_failed(path);
        errno = saved_errno; return;
    }
    if (!s_prefetch_started) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 128 * 1024);
        int result = pthread_create(&s_prefetch_thread, &attr, prefetch_worker, NULL);
        pthread_attr_destroy(&attr);
        if (result) {
            ++s_prefetch_dropped;
            pthread_mutex_unlock(&s_prefetch_lock); warm_failed(path); errno = saved_errno; return;
        }
        s_prefetch_started = 1;
    }
    int dropped = s_prefetch_count == PREFETCH_SLOTS;
    if (dropped) ++s_prefetch_dropped;
    else {
        strcpy(s_prefetch_paths[(s_prefetch_head + s_prefetch_count) % PREFETCH_SLOTS], path);
        ++s_prefetch_count; ++s_prefetch_submitted;
        pthread_cond_signal(&s_prefetch_cond);
    }
    pthread_mutex_unlock(&s_prefetch_lock);
    if (dropped) warm_failed(path);
    errno = saved_errno;
}

int native_io_warm_start(const NativeIoWarmResource *resources, unsigned count) {
    int saved_errno = errno;
    size_t total = 0;
    if (!resources || !count || count > WARM_SLOTS) return -1;
    for (unsigned i = 0; i < count; ++i) {
        if (!resources[i].path || resources[i].expected_size > WARM_BYTES - total) return -1;
        total += resources[i].expected_size;
    }
    pthread_mutex_lock(&s_cache_lock);
    if (s_warm_count) { pthread_mutex_unlock(&s_cache_lock); return -1; }
    /* Register before submitting, so an immediately scheduled worker cannot
     * publish an unprotected entry. Calls originate in patch installation. */
    s_warm_count = count;
    for (unsigned i = 0; i < count; ++i) {
        s_warm[i].state = 2;
        s_warm[i].size = resources[i].expected_size;
    }
    pthread_mutex_unlock(&s_cache_lock);
    for (unsigned i = 0; i < count; ++i) {
        char path[CACHE_PATH_MAX];
        const char *mapped = paths_normalize_resource(resources[i].path, path, sizeof(path));
        if (!mapped || !native_io_immutable_asset(mapped, resources[i].expected_size)) continue;
        pthread_mutex_lock(&s_cache_lock);
        int duplicate = 0;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(s_warm[j].path, mapped)) { duplicate = 1; break; }
        if (duplicate) { pthread_mutex_unlock(&s_cache_lock); continue; }
        strcpy(s_warm[i].path, mapped);
        s_warm[i].state = 0;
        for (unsigned j = 0; j < CACHE_SLOTS; ++j) {
            CacheEntry *entry = &s_cache[j];
            if (entry->state && !entry->retired && !strcmp(entry->path, mapped)) {
                entry->warm = i + 1;
                if (entry->state == 2) s_warm[i].state = 1;
                break;
            }
        }
        pthread_mutex_unlock(&s_cache_lock);
        native_io_prefetch(mapped);
    }
    errno = saved_errno;
    return 0;
}

void native_io_warm_status(NativeIoWarmStatus *status) {
    if (!status) return;
    int saved_errno = errno;
    memset(status, 0, sizeof(*status));
    pthread_mutex_lock(&s_cache_lock);
    status->requested = s_warm_count;
    for (unsigned i = 0; i < s_warm_count; ++i) {
        if (s_warm[i].state == 2 || mutable_path_locked(s_warm[i].path)) {
            ++status->failed;
            status->failed_mask |= 1u << i;
        }
        else if (s_warm[i].state == 1) { ++status->ready; status->ready_bytes += s_warm[i].size; }
        else ++status->pending;
    }
    pthread_mutex_unlock(&s_cache_lock);
    errno = saved_errno;
}

void native_io_prefetch_stop(void) {
    int saved_errno = errno;
    pthread_mutex_lock(&s_prefetch_lock);
    int started = s_prefetch_started;
    s_prefetch_stopping = 1;
    pthread_cond_signal(&s_prefetch_cond);
    pthread_mutex_unlock(&s_prefetch_lock);
    if (started) pthread_join(s_prefetch_thread, NULL);
    pthread_mutex_lock(&s_prefetch_lock);
    s_prefetch_started = 0;
    pthread_mutex_unlock(&s_prefetch_lock);
    errno = saved_errno;
}

void native_io_poll(int force) {
    int saved_errno = errno;
    uint64_t now = perf_now_us();
    pthread_mutex_lock(&s_cache_lock);
    if (!force && now - s_cache_report < 1000000) {
        pthread_mutex_unlock(&s_cache_lock); errno = saved_errno; return;
    }
    s_cache_report = now;
    size_t bytes = s_cache_bytes;
    unsigned hits = s_cache_hits, misses = s_cache_misses, negative = s_cache_negative;
    unsigned evicts = s_cache_evicts, bypass = s_cache_bypass, errors = s_cache_errors, streams = s_cache_streams;
    unsigned mutable_paths = s_mutable_path_count, invalidations = s_invalidations;
    unsigned used = s_cache_used, peak = s_cache_peak, evict_slots = s_evict_slots, evict_bytes = s_evict_bytes;
    s_evict_slots = s_evict_bytes = 0;
    s_cache_hits = s_cache_misses = s_cache_negative = s_cache_evicts = s_cache_bypass = s_cache_errors = 0;
    pthread_mutex_unlock(&s_cache_lock);
    pthread_mutex_lock(&s_prefetch_lock);
    unsigned submitted = s_prefetch_submitted, dropped = s_prefetch_dropped, pending = s_prefetch_count;
    unsigned loaded = s_prefetch_loaded, reused = s_prefetch_reused, prefetch_bypass = s_prefetch_bypass, prefetch_errors = s_prefetch_errors;
    s_prefetch_submitted = s_prefetch_dropped = 0;
    s_prefetch_loaded = s_prefetch_reused = s_prefetch_bypass = s_prefetch_errors = 0;
    pthread_mutex_unlock(&s_prefetch_lock);
    NativeIoWarmStatus warm;
    native_io_warm_status(&warm);
    if (force || hits || misses || negative || evicts || bypass || errors || submitted || dropped || loaded || reused || prefetch_bypass || prefetch_errors)
        so_loga("[IO] cache bytes=%u budget=%u hits=%u misses=%u negative_hits=%u evictions=%u bypass=%u errors=%u streams=%u prefetch_queued=%u prefetch_dropped=%u prefetch_pending=%u prefetch_loaded=%u prefetch_reused=%u prefetch_bypass=%u prefetch_errors=%u mutable_paths=%u invalidations_total=%u slots=%u/%u peak_slots=%u evict_reason_slots=%u evict_reason_bytes=%u warm_ready=%u/%u warm_pending=%u warm_failed=%u warm_failed_mask=%#x warm_bytes=%u",
                (unsigned)bytes, CACHE_BYTES, hits, misses, negative, evicts, bypass, errors, streams,
                submitted, dropped, pending, loaded, reused, prefetch_bypass, prefetch_errors, mutable_paths, invalidations,
                used, CACHE_SLOTS, peak, evict_slots, evict_bytes, warm.ready, warm.requested,
                warm.pending, warm.failed, warm.failed_mask, (unsigned)warm.ready_bytes);
    errno = saved_errno;
}

void native_io_shutdown(void) {
    native_io_prefetch_stop();
    pthread_mutex_lock(&s_cache_lock);
    for (unsigned i = 0; i < CACHE_SLOTS; ++i)
        if (s_cache[i].state == 2 && !s_cache[i].refs) discard(&s_cache[i]);
    pthread_mutex_unlock(&s_cache_lock);
}
