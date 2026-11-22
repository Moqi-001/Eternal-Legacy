#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <so_common/so_logger.h>
#include "../config.h"
#include "resource_index.h"

typedef struct {
    char name[256];
    size_t size;
    int regular;
    int directory;
} ResourceEntry;

static ResourceEntry *s_entries;
static size_t s_count;
/* 0: untouched, 1: building, 2: immutable complete snapshot, 3: unavailable. */
static atomic_int s_state;

static int compare_entries(const void *a, const void *b) {
    const unsigned char *left = (const unsigned char *)((const ResourceEntry *)a)->name;
    const unsigned char *right = (const unsigned char *)((const ResourceEntry *)b)->name;
    for (;;) {
        unsigned x = *left++, y = *right++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

int resource_index_init(void) {
    int expected = 0;
    if (!atomic_compare_exchange_strong(&s_state, &expected, 1))
        return atomic_load_explicit(&s_state, memory_order_acquire) == 2 ? 0 : -1;
    uint64_t start = sceKernelGetProcessTimeWide();
    so_loga("[RESOURCE_INDEX] begin dir=%s", GAME_DATA_DIR);
    ResourceEntry *entries = NULL;
    size_t count = 0, capacity = 0;
    int status = -1;
    SceUID directory = sceIoDopen(GAME_DATA_DIR);
    if (directory < 0) goto finished;
    for (;;) {
        SceIoDirent entry;
        memset(&entry, 0, sizeof(entry));
        int read = sceIoDread(directory, &entry);
        if (read < 0) goto close_directory;
        if (!read) { status = 0; break; }
        if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, "..")) continue;
        size_t length = strnlen(entry.d_name, sizeof(entry.d_name));
        if (!length || length >= sizeof(entry.d_name) ||
            strchr(entry.d_name, '/') || strchr(entry.d_name, '\\')) goto close_directory;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 128;
            if (next < capacity || next > SIZE_MAX / sizeof(*entries)) goto close_directory;
            ResourceEntry *grown = realloc(entries, next * sizeof(*entries));
            if (!grown) goto close_directory;
            entries = grown;
            capacity = next;
        }
        ResourceEntry *out = &entries[count++];
        memcpy(out->name, entry.d_name, length + 1);
        out->regular = SCE_S_ISREG(entry.d_stat.st_mode) && entry.d_stat.st_size >= 0 &&
                       (uint64_t)entry.d_stat.st_size <= SIZE_MAX;
        out->directory = SCE_S_ISDIR(entry.d_stat.st_mode);
        out->size = out->regular ? (size_t)entry.d_stat.st_size : 0;
    }
close_directory:
    if (sceIoDclose(directory) < 0) status = -1;
finished:
    if (!status) {
        qsort(entries, count, sizeof(*entries), compare_entries);
        /* Duplicate names or an inconsistent directory must never yield negatives. */
        for (size_t i = 1; i < count; ++i)
            if (!compare_entries(&entries[i - 1], &entries[i])) { status = -1; break; }
    }
    if (!status) {
        s_entries = entries;
        s_count = count;
        atomic_store_explicit(&s_state, 2, memory_order_release);
    } else {
        free(entries);
        atomic_store_explicit(&s_state, 3, memory_order_release);
    }
    so_loga("[RESOURCE_INDEX] init status=%d entries=%u us=%llu",
            status, (unsigned)count, (unsigned long long)(sceKernelGetProcessTimeWide() - start));
    return status;
}

int resource_index_type(const char *mapped_path, size_t *size) {
    if (atomic_load_explicit(&s_state, memory_order_acquire) != 2 || !mapped_path) return -1;
    const size_t prefix = sizeof(GAME_DATA_DIR) - 1;
    if (strncmp(mapped_path, GAME_DATA_DIR, prefix)) return -1;
    const char *name = mapped_path + prefix;
    if (!*name || !strcmp(name, ".")) return RESOURCE_TYPE_DIRECTORY;
    size_t length = strnlen(name, sizeof(s_entries[0].name));
    if (length >= sizeof(s_entries[0].name)) return RESOURCE_TYPE_UNKNOWN;
    int trailing_slash = length && name[length - 1] == '/';
    if (trailing_slash) --length;
    if (!length || length >= sizeof(s_entries[0].name) ||
        (length == 2 && !strncmp(name, "..", length)) ||
        memchr(name, '/', length) || memchr(name, '\\', length) || memchr(name, ':', length)) return -1;
    ResourceEntry key;
    memcpy(key.name, name, length); key.name[length] = 0;
    const ResourceEntry *found = bsearch(&key, s_entries, s_count, sizeof(*s_entries), compare_entries);
    if (!found) return trailing_slash ? RESOURCE_TYPE_UNKNOWN : RESOURCE_TYPE_ABSENT;
    /* Case aliases may be accepted by the device filesystem; keep original I/O
     * responsible for them instead of incorrectly returning a negative hit. */
    if (strcmp(found->name, key.name)) return RESOURCE_TYPE_UNKNOWN;
    if (found->directory) return RESOURCE_TYPE_DIRECTORY;
    if (!found->regular || trailing_slash) return RESOURCE_TYPE_UNKNOWN;
    if (size) *size = found->size;
    return 1;
}

int resource_index_lookup(const char *mapped_path, size_t *size) {
    int type = resource_index_type(mapped_path, size);
    return type == RESOURCE_TYPE_DIRECTORY ? -1 : type;
}

/* Root enumeration is paid once at startup. Loose subdirectories are enumerated
 * only when a nested request actually reaches them. This index stores names and
 * types, never mutable metadata bytes. Directory contents are valid until a game
 * filesystem mutation, paired with native_io's scoped root-snapshot bypass. */
#define DIRECTORY_SLOTS 4
#define DIRECTORY_ENTRIES 512
#define ANCESTOR_SLOTS 64
#define INDEX_PATH_MAX 512
typedef struct {
    char path[INDEX_PATH_MAX];
    ResourceEntry *entries;
    size_t count;
    uint64_t touched, generation;
    int state; /* 0 empty, 1 building, 2 complete, 3 unavailable */
} DirectorySnapshot;
static DirectorySnapshot s_directories[DIRECTORY_SLOTS];
static char s_ancestors[ANCESTOR_SLOTS][INDEX_PATH_MAX];
static unsigned s_ancestor_next;
static uint64_t s_directory_tick, s_directory_generation, s_directory_mutation;
static pthread_mutex_t s_directory_lock = PTHREAD_MUTEX_INITIALIZER;

static int same_or_child(const char *path, const char *parent) {
    size_t n = strlen(parent);
    return !strncmp(path, parent, n) && (!path[n] || path[n] == '/');
}

/* Validate the WHOLE request before consulting any ancestor proof. In particular,
 * a missing prefix cannot justify negatives for ../, empty components or aliases. */
static int canonical_nested(const char *path) {
    size_t prefix = sizeof(GAME_DATA_DIR) - 1;
    if (!path || strncmp(path, GAME_DATA_DIR, prefix) || strlen(path) >= INDEX_PATH_MAX) return 0;
    const char *name = path + prefix;
    if (!*name || strchr(name, '\\') || strchr(name, ':')) return 0;
    for (const char *part = name; *part;) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (!n || n >= sizeof(s_entries[0].name) ||
            (n == 1 && *part == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return 0;
        if (!end) return 1;
        part = end + 1;
        if (!*part) return 0;
    }
    return 0;
}

static int snapshot_entry_type(const ResourceEntry *entries, size_t count, const char *name) {
    ResourceEntry key;
    strcpy(key.name, name);
    const ResourceEntry *found = bsearch(&key, entries, count, sizeof(*entries), compare_entries);
    if (!found) return RESOURCE_TYPE_ABSENT;
    if (strcmp(found->name, name)) return RESOURCE_TYPE_UNKNOWN;
    return found->directory ? RESOURCE_TYPE_DIRECTORY : found->regular ? RESOURCE_TYPE_REGULAR : RESOURCE_TYPE_UNKNOWN;
}

static int enumerate_directory(const char *path, ResourceEntry **out, size_t *out_count) {
    ResourceEntry *entries = NULL;
    size_t count = 0, capacity = 0;
    int status = -1;
    SceUID fd = sceIoDopen(path);
    if (fd < 0) return -1;
    for (;;) {
        SceIoDirent entry;
        memset(&entry, 0, sizeof(entry));
        int n = sceIoDread(fd, &entry);
        if (n < 0) break;
        if (!n) { status = 0; break; }
        if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, "..")) continue;
        size_t length = strnlen(entry.d_name, sizeof(entry.d_name));
        if (!length || length >= sizeof(entry.d_name) || strchr(entry.d_name, '/') ||
            strchr(entry.d_name, '\\') || strchr(entry.d_name, ':') || count == DIRECTORY_ENTRIES) break;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 32;
            ResourceEntry *grown = realloc(entries, next * sizeof(*entries));
            if (!grown) break;
            entries = grown; capacity = next;
        }
        ResourceEntry *item = &entries[count++];
        memcpy(item->name, entry.d_name, length + 1);
        item->directory = SCE_S_ISDIR(entry.d_stat.st_mode);
        item->regular = SCE_S_ISREG(entry.d_stat.st_mode);
        item->size = 0;
    }
    if (sceIoDclose(fd) < 0) status = -1;
    if (!status) {
        qsort(entries, count, sizeof(*entries), compare_entries);
        for (size_t i = 1; i < count; ++i)
            if (!compare_entries(&entries[i - 1], &entries[i])) { status = -1; break; }
    }
    if (status) { free(entries); return -1; }
    *out = entries; *out_count = count;
    return 0;
}

static int directory_child_type(const char *directory, const char *name) {
    pthread_mutex_lock(&s_directory_lock);
    DirectorySnapshot *slot = NULL;
    for (unsigned i = 0; i < DIRECTORY_SLOTS; ++i) {
        DirectorySnapshot *entry = &s_directories[i];
        if (entry->state && !strcmp(entry->path, directory)) {
            entry->touched = ++s_directory_tick;
            int result = entry->state == 2 ? snapshot_entry_type(entry->entries, entry->count, name) : RESOURCE_TYPE_UNKNOWN;
            pthread_mutex_unlock(&s_directory_lock);
            return result;
        }
        if (entry->state != 1 && (!slot || entry->touched < slot->touched)) slot = entry;
    }
    if (!slot) { pthread_mutex_unlock(&s_directory_lock); return RESOURCE_TYPE_UNKNOWN; }
    free(slot->entries);
    memset(slot, 0, sizeof(*slot));
    strcpy(slot->path, directory);
    slot->state = 1; slot->touched = ++s_directory_tick;
    uint64_t generation = slot->generation = ++s_directory_generation;
    pthread_mutex_unlock(&s_directory_lock);

    ResourceEntry *entries = NULL;
    size_t count = 0;
    int status = enumerate_directory(directory, &entries, &count);
    pthread_mutex_lock(&s_directory_lock);
    int result = RESOURCE_TYPE_UNKNOWN;
    if (slot->state == 1 && slot->generation == generation) {
        slot->entries = entries; slot->count = count; slot->state = status ? 3 : 2;
        if (!status) result = snapshot_entry_type(entries, count, name);
    } else free(entries); /* Mutation/eviction won; never publish the old scan. */
    pthread_mutex_unlock(&s_directory_lock);
    return result;
}

int resource_index_path_type(const char *path) {
    if (atomic_load_explicit(&s_state, memory_order_acquire) != 2 || !canonical_nested(path)) return RESOURCE_TYPE_UNKNOWN;
    pthread_mutex_lock(&s_directory_lock);
    for (unsigned i = 0; i < ANCESTOR_SLOTS; ++i)
        if (s_ancestors[i][0] && same_or_child(path, s_ancestors[i])) {
            pthread_mutex_unlock(&s_directory_lock); return RESOURCE_TYPE_ABSENT;
        }
    /* A mutation anywhere in the walk invalidates its proof, even if the
     * individual immutable root / directory lookups happened to succeed. */
    uint64_t generation = s_directory_mutation;
    pthread_mutex_unlock(&s_directory_lock);
    const size_t prefix = sizeof(GAME_DATA_DIR) - 1;
    const char *part = path + prefix;
    if (!strchr(part, '/')) return RESOURCE_TYPE_UNKNOWN;
    char parent[INDEX_PATH_MAX], current[INDEX_PATH_MAX], component[256];
    parent[0] = 0;
    int type = RESOURCE_TYPE_UNKNOWN;
    for (;;) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        memcpy(component, part, n); component[n] = 0;
        size_t length = (size_t)(part - path) + n;
        memcpy(current, path, length); current[length] = 0;
        type = parent[0] ? directory_child_type(parent, component) : resource_index_type(current, NULL);
        if (type == RESOURCE_TYPE_ABSENT) {
            pthread_mutex_lock(&s_directory_lock);
            if (generation != s_directory_mutation) type = RESOURCE_TYPE_UNKNOWN;
            else if (end) {
                strcpy(s_ancestors[s_ancestor_next++ % ANCESTOR_SLOTS], current);
            }
            pthread_mutex_unlock(&s_directory_lock);
            return type;
        }
        if (!end || type != RESOURCE_TYPE_DIRECTORY) return type == RESOURCE_TYPE_DIRECTORY ? type : RESOURCE_TYPE_UNKNOWN;
        strcpy(parent, current); part = end + 1;
    }
}

void resource_index_invalidate(const char *path) {
    if (!path) return;
    /* Case-fold comparisons also discard evidence reached through a write alias.
     * Invalidation is deliberately broader than read lookup, which stays exact. */
    pthread_mutex_lock(&s_directory_lock);
    ++s_directory_mutation;
    size_t prefix = sizeof(GAME_DATA_DIR) - 1;
    size_t length = strlen(path);
    const char *slash = strrchr(path, '/');
    size_t parent_length = slash && (size_t)(slash - path) >= prefix ? (size_t)(slash - path) : prefix - 1;
    for (unsigned i = 0; i < DIRECTORY_SLOTS; ++i) {
        DirectorySnapshot *entry = &s_directories[i];
        size_t n = strlen(entry->path);
        if (entry->state && ((n == parent_length && !strncasecmp(entry->path, path, n)) ||
            (n >= length && !strncasecmp(entry->path, path, length) && (!entry->path[length] || entry->path[length] == '/')))) {
            free(entry->entries); memset(entry, 0, sizeof(*entry));
        }
    }
    for (unsigned i = 0; i < ANCESTOR_SLOTS; ++i) {
        size_t n = strlen(s_ancestors[i]);
        size_t common = n < length ? n : length;
        if (n && !strncasecmp(s_ancestors[i], path, common) &&
            (n == length || (n < length ? path[n] : s_ancestors[i][length]) == '/')) s_ancestors[i][0] = 0;
    }
    pthread_mutex_unlock(&s_directory_lock);
}
