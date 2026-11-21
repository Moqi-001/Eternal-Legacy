#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
