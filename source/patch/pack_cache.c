/* Known original SO c3555dd862c78d0da3c6ef2505e98a0aca64473d686441b3f75bfb2fe4526212.
 * Cache private copies of decompressed immutable entries. Never share a mutable
 * native getBuffer() with another reader: each cache hit receives its own native
 * allocation and original CMemoryReadFile owning that allocation. Native first
 * loads, inflate, strings, memory-reader construction and destruction retain ABI.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <so_common/so_logger.h>
#include "pack_cache.h"
#include "../utils/paths.h"
#include "../utils/resource_index.h"

#define PACK_BYTES (8u * 1024u * 1024u)
#define PACK_SLOTS 64
typedef struct {
    void *archive;
    uint32_t index, offset, compressed, size, crc;
    uint64_t generation;
} EntryKey;
typedef struct { void *archive; uint64_t generation; } ArchiveLife;
typedef struct { EntryKey key; unsigned char *bytes; uint64_t touched; } Entry;
typedef struct {
    uint64_t total, read, inflate, create, copy;
    unsigned calls, hits, misses, bypass, evictions, errors, signature_short;
} Counts;
typedef struct { uint64_t read, inflate, create; unsigned depth; } NativeSpan;
static _Thread_local NativeSpan s_span;
static Entry s_entries[PACK_SLOTS];
static ArchiveLife s_archives[128];
static uint64_t s_generation;
static Counts s_counts;
static size_t s_bytes;
static uint64_t s_tick, s_report;
static unsigned s_details;
static int s_stopped;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static so_module *s_module;
static so_hook_ref s_hooks[7];
static void *(*s_open)(void *, int);
static unsigned (*s_read)(void *, void *, unsigned);
static int (*s_inflate)(void *, int);
static void *(*s_create)(void *, int32_t, const char *, int);
static void *(*s_alloc)(unsigned);
static void (*s_delete)(void *);
static void *(*s_destruct[2])(void *);
static uint32_t (*s_crc)(uint32_t,const void *,unsigned);
static uintptr_t s_memory_vtable, s_disk_vtable;

static uint32_t word(const void *p, unsigned at) {
    uint32_t v; memcpy(&v, (const char *)p + at, 4); return v;
}
static uint16_t half(const void *p, unsigned at) {
    uint16_t v; memcpy(&v, (const char *)p + at, 2); return v;
}
static void *pointer(const void *p, unsigned at) { return (void *)(uintptr_t)word(p, at); }
static int same_key(const EntryKey *a, const EntryKey *b) {
    return a->archive == b->archive && a->index == b->index && a->offset == b->offset &&
           a->compressed == b->compressed && a->size == b->size && a->crc == b->crc &&
           a->generation == b->generation;
}
static int metadata(void *archive, int index, EntryKey *key, const char **name) {
    if (!archive || index < 0) return 0;
    uintptr_t begin = word(archive, 20), end = word(archive, 24);
    if (!begin || end < begin || (end - begin) % 108 || (end - begin) / 108 > 65536 ||
        (unsigned)index >= (end - begin) / 108) return 0;
    const void *entry = (const void *)(begin + (unsigned)index * 108u);
    /* Only DEFLATE entries: stored/unknown/encrypted entries preserve original. */
    if (half(entry, 84) != 8 || (half(entry, 82) & 1)) return 0;
    void *disk = pointer(archive, 8);
    if (!disk || word(disk, 0) != s_disk_vtable || !word(disk, 12)) return 0;
    const char *path = pointer(pointer(disk, 12), 28);
    char mapped[PATH_MAPPED_MAX];
    const char *physical = path ? paths_from_android(path, mapped, sizeof(mapped)) : NULL;
    size_t file_size;
    if (!physical || resource_index_lookup(physical, &file_size) != 1 ||
        file_size != word(disk, 16)) return 0;
    key->archive = archive; key->index = index; key->offset = word(entry, 72);
    key->compressed = word(entry, 94); key->size = word(entry, 98); key->crc = word(entry, 90);
    if (!key->size || key->size > PACK_BYTES || key->offset > file_size ||
        key->compressed > file_size - key->offset) return 0;
    *name = pointer(entry, 20);
    if (!*name || strnlen(*name, 512) >= 512) return 0;
    pthread_mutex_lock(&s_lock);
    ArchiveLife *empty = NULL; key->generation = 0;
    for (unsigned i = 0; i < 128; ++i) {
        if (s_archives[i].archive == archive) { key->generation = s_archives[i].generation; break; }
        if (!s_archives[i].archive) empty = &s_archives[i];
    }
    if (!key->generation && empty) {
        empty->archive = archive; empty->generation = ++s_generation;
        if (!empty->generation) empty->generation = ++s_generation;
        key->generation = empty->generation;
    }
    pthread_mutex_unlock(&s_lock);
    return key->generation != 0;
}

/* Holding the lock pins immutable bytes through each copy. Returned native
 * readers own independent allocations, so cache eviction cannot invalidate them.
 * Both native non-deleting archive destructors clear the generation and entries
 * before native destruction; deleting D0 delegates to D1. No native STL or
 * intrusive refcount is edited, and archive addresses may be reused safely. */
static void *hit_reader(const EntryKey *key, const char *name, NativeSpan *span) {
    pthread_mutex_lock(&s_lock);
    for (unsigned i = 0; i < PACK_SLOTS && !s_stopped; ++i) {
        Entry *entry = &s_entries[i];
        if (!entry->bytes || !same_key(&entry->key, key)) continue;
        void *data = s_alloc(key->size);
        if (!data) { ++s_counts.errors; pthread_mutex_unlock(&s_lock); return NULL; }
        uint64_t begin = sceKernelGetProcessTimeWide();
        memcpy(data, entry->bytes, key->size);
        s_counts.copy += sceKernelGetProcessTimeWide() - begin;
        entry->touched = ++s_tick;
        pthread_mutex_unlock(&s_lock);
        /* Original native CMemoryReadFile takes ownership using its own boost
         * shared_ptr<char> and native operator delete (0x38e034). */
        begin = sceKernelGetProcessTimeWide();
        void *reader = s_create(data, key->size, name, 1);
        span->create = sceKernelGetProcessTimeWide() - begin;
        pthread_mutex_lock(&s_lock);
        s_counts.create += span->create;
        if (reader) ++s_counts.hits; else ++s_counts.errors;
        pthread_mutex_unlock(&s_lock);
        if (!reader) s_delete(data);
        return reader;
    }
    pthread_mutex_unlock(&s_lock);
    return NULL;
}
static void store_reader(const EntryKey *key, void *reader) {
    if (!reader || word(reader, 0) != s_memory_vtable || word(reader, 24) != key->size ||
        word(reader, 28) || !word(reader, 12)) return;
    uint64_t begin = sceKernelGetProcessTimeWide();
    const void *source = pointer(reader, 12);
    if (s_crc(0, source, key->size) != key->crc) {
        pthread_mutex_lock(&s_lock); ++s_counts.errors; pthread_mutex_unlock(&s_lock); return;
    }
    pthread_mutex_lock(&s_lock);
    if (s_stopped) { pthread_mutex_unlock(&s_lock); return; }
    Entry *slot = NULL;
    for (unsigned i = 0; i < PACK_SLOTS; ++i) {
        if (s_entries[i].bytes && same_key(&s_entries[i].key, key)) {
            pthread_mutex_unlock(&s_lock); return;
        }
        if (!s_entries[i].bytes) slot = &s_entries[i];
    }
    while (!slot || key->size > PACK_BYTES - s_bytes) {
        Entry *oldest = NULL;
        for (unsigned i = 0; i < PACK_SLOTS; ++i)
            if (s_entries[i].bytes && (!oldest || s_entries[i].touched < oldest->touched)) oldest = &s_entries[i];
        if (!oldest) break;
        s_bytes -= oldest->key.size; free(oldest->bytes); memset(oldest, 0, sizeof(*oldest));
        ++s_counts.evictions; if (!slot) slot = oldest;
    }
    /* Evict before allocating: resident cache plus an unpublished copy also
     * stays within 8 MiB. Original/caller-owned native buffers are separate. */
    unsigned char *copy = slot ? malloc(key->size) : NULL;
    if (copy) {
        memcpy(copy, source, key->size);
        slot->key = *key; slot->bytes = copy;
        slot->touched = ++s_tick; s_bytes += key->size;
    } else {
        ++s_counts.errors;
    }
    s_counts.copy += sceKernelGetProcessTimeWide() - begin;
    pthread_mutex_unlock(&s_lock);
}
static void forget_archive(void *archive) {
    pthread_mutex_lock(&s_lock);
    for (unsigned i = 0; i < PACK_SLOTS; ++i) if (s_entries[i].bytes && s_entries[i].key.archive == archive) {
        s_bytes -= s_entries[i].key.size; free(s_entries[i].bytes); memset(&s_entries[i],0,sizeof(Entry));
    }
    for (unsigned i = 0; i < 128; ++i)
        if (s_archives[i].archive == archive) memset(&s_archives[i],0,sizeof(ArchiveLife));
    pthread_mutex_unlock(&s_lock);
}
static void *destruct_one(void *archive) { forget_archive(archive); return s_destruct[0](archive); }
static void *destruct_two(void *archive) { forget_archive(archive); return s_destruct[1](archive); }
static void *open_entry(void *archive, int index) {
    uint64_t begin = sceKernelGetProcessTimeWide(); EntryKey key = {0}; const char *name = NULL;
    NativeSpan measured = {0};
    pthread_mutex_lock(&s_lock); int stopped = s_stopped; pthread_mutex_unlock(&s_lock);
    int covered = !stopped && metadata(archive, index, &key, &name);
    void *reader = covered ? hit_reader(&key, name, &measured) : NULL;
    int cache_hit = reader != NULL;
    if (!reader) {
        NativeSpan saved = s_span; memset(&s_span, 0, sizeof(s_span)); s_span.depth = 1;
        reader = s_open(archive, index);
        measured = s_span; s_span = saved;
        pthread_mutex_lock(&s_lock);
        s_counts.read += measured.read; s_counts.inflate += measured.inflate; s_counts.create += measured.create;
        if (covered) ++s_counts.misses; else ++s_counts.bypass;
        pthread_mutex_unlock(&s_lock);
        if (covered) store_reader(&key, reader);
    }
    uint64_t elapsed = sceKernelGetProcessTimeWide() - begin;
    pthread_mutex_lock(&s_lock); ++s_counts.calls; s_counts.total += elapsed;
    int detail = covered && elapsed >= 20000 && s_details++ < 2;
    pthread_mutex_unlock(&s_lock);
    if (detail)
        so_loga("[PACK_CACHE] detail thread=%d archive=%p generation=%llu entry=%d crc=%08x hit=%d ok=%d compressed=%u size=%u total_us=%llu read_us=%llu inflate_us=%llu create_us=%llu path=%s",
            sceKernelGetThreadId(),archive,(unsigned long long)key.generation,index,key.crc,cache_hit,reader != NULL,
            key.compressed,key.size,(unsigned long long)elapsed,(unsigned long long)measured.read,
            (unsigned long long)measured.inflate,(unsigned long long)measured.create,name);
    pack_cache_poll(0); return reader;
}
static unsigned traced_read(void *file, void *buffer, unsigned count) {
    uint64_t begin = s_span.depth ? sceKernelGetProcessTimeWide() : 0;
    unsigned result = s_read(file, buffer, count);
    if (s_span.depth) s_span.read += sceKernelGetProcessTimeWide() - begin;
    return result;
}
static int traced_inflate(void *stream, int flush) {
    uint64_t begin = s_span.depth ? sceKernelGetProcessTimeWide() : 0;
    int result = s_inflate(stream, flush);
    if (s_span.depth) s_span.inflate += sceKernelGetProcessTimeWide() - begin;
    return result;
}
static void *traced_create(void *data, int32_t size, const char *name, int own) {
    uint64_t begin = s_span.depth ? sceKernelGetProcessTimeWide() : 0;
    void *result = s_create(data, size, name, own);
    if (s_span.depth) s_span.create += sceKernelGetProcessTimeWide() - begin;
    return result;
}
static int valid_zip(void *file) {
    if (!file) return 0;
    const uint32_t *vtable = pointer(file, 0);
    int32_t (*position)(void *) = (void *)(uintptr_t)vtable[9];
    int (*seek)(void *,int32_t,int) = (void *)(uintptr_t)vtable[6];
    unsigned (*read)(void *,void *,unsigned) = (void *)(uintptr_t)vtable[3];
    int32_t old = position(file); uint32_t signature = 0;
    if (old < 0) return 0;
    int moved = seek(file, 0, 0);
    unsigned count = moved ? read(file, &signature, 4) : 0;
    int restored = seek(file, old, 0);
    if (count != 4) { pthread_mutex_lock(&s_lock); ++s_counts.signature_short; pthread_mutex_unlock(&s_lock); }
    return moved && restored && count == 4 && signature == 0x04034b50;
}
void pack_cache_poll(int force) {
    uint64_t now = sceKernelGetProcessTimeWide(); pthread_mutex_lock(&s_lock);
    if (!force && now - s_report < 1000000) { pthread_mutex_unlock(&s_lock); return; }
    s_report = now; Counts c = s_counts; memset(&s_counts, 0, sizeof(s_counts)); s_details = 0; size_t bytes = s_bytes;
    pthread_mutex_unlock(&s_lock);
    if (c.calls || c.signature_short || force)
        so_loga("[PACK_CACHE] bytes=%u budget=%u calls=%u hits=%u misses=%u bypass=%u evictions=%u errors=%u signature_short=%u total_us=%llu read_us=%llu inflate_us=%llu create_us=%llu copy_crc_us=%llu",
            (unsigned)bytes, PACK_BYTES, c.calls, c.hits, c.misses, c.bypass, c.evictions, c.errors, c.signature_short,
            (unsigned long long)c.total,(unsigned long long)c.read,(unsigned long long)c.inflate,(unsigned long long)c.create,(unsigned long long)c.copy);
}
void pack_cache_shutdown(void) {
    pthread_mutex_lock(&s_lock); s_stopped = 1;
    for (unsigned i = 0; i < PACK_SLOTS; ++i) if (s_entries[i].bytes) {
        free(s_entries[i].bytes); memset(&s_entries[i],0,sizeof(Entry));
    }
    s_bytes = 0; memset(s_archives,0,sizeof(s_archives)); pthread_mutex_unlock(&s_lock);
    pack_cache_poll(1);
}
int soloader_patch_pack_cache(so_module *mod) {
    if (!mod) return -1;
    if (s_module) return s_module == mod ? 0 : -1;
    intptr_t bias = so_module_get_load_bias(mod);
    if (so_module_get_load_size(mod) < 0x606fa0 ||
        (uintptr_t)so_module_find_export(mod,"_ZN6glitch2io10CZipReader8openFileEi") != (uintptr_t)bias + 0x392705 ||
        (uintptr_t)so_module_find_export(mod,"_ZTVN6glitch2io15CMemoryReadFileE") != (uintptr_t)bias + 0x5889e8 ||
        (uintptr_t)so_module_find_export(mod,"crc32") != (uintptr_t)bias + 0x48fb29) {
        so_loga("[PACK_CACHE] disabled reason=unsupported_so_symbols"); return 0;
    }
    static const struct { uint32_t at; unsigned char bytes[16]; } guards[] = {
        {0x392704,{0xf0,0xb5,0x57,0x46,0x4e,0x46,0x45,0x46,0xe0,0xb4,0x6c,0x23,0x1d,0x1c,0x4d,0x43}},
        {0x3925dc,{0x30,0xb5,0x11,0x4b,0x83,0xb0,0x04,0x1c,0x7b,0x44,0x1b,0x68,0x01,0x93,0x03,0x68}},
        {0x38eee4,{0xf8,0xb5,0x03,0x68,0x04,0x1c,0x0e,0x1c,0xdb,0x6b,0x15,0x1c,0x98,0x47,0x03,0x1c}},
        {0x417974,{0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x9d,0xb0,0x05,0x1c}},
        {0x38e220,{0xf0,0xb5,0x47,0x46,0x80,0xb4,0x82,0xb0,0x05,0x1c,0x38,0x20,0x0f,0x1c,0x90,0x46}},
        {0x38e1d0,{0xf0,0xb5,0x83,0xb0,0x0d,0x1c,0x08,0xa9,0x0e,0x78,0x0f,0x4f,0x01,0x21,0x41,0x60}},
        {0x392a48,{0x10,0xb5,0x09,0x4b,0x09,0x4a,0x04,0x1c,0x7b,0x44,0x9a,0x58,0x08,0x32,0x02,0x60}},
        {0x392a8c,{0x10,0xb5,0x09,0x4b,0x09,0x4a,0x04,0x1c,0x7b,0x44,0x9a,0x58,0x08,0x32,0x02,0x60}},
        {0x233398,{0x10,0xb5,0x43,0x68,0x04,0x1c,0x01,0x3b,0x43,0x60,0x00,0x20,0x00,0x2b,0x08,0xd1}}
    };
    for (unsigned i = 0; i < sizeof(guards)/sizeof(guards[0]); ++i)
        if (memcmp((void *)(bias + guards[i].at), guards[i].bytes, 16)) {
            so_loga("[PACK_CACHE] disabled reason=target_bytes offset=%08x",guards[i].at); return 0;
        }
    void *replacements[] = {(void *)open_entry,(void *)valid_zip,(void *)traced_read,(void *)traced_inflate,(void *)traced_create,(void *)destruct_one,(void *)destruct_two};
    const uintptr_t addresses[] = {0x392705,0x3925dd,0x38eee5,0x417975,0x38e221,0x392a49,0x392a8d};
    unsigned installed = 0;
    for (; installed < 7; ++installed) {
        if (so_module_hook_address(mod, addresses[installed], replacements[installed], &s_hooks[installed]) < 0) goto failed;
        if (!s_hooks[installed].orig_func) { ++installed; goto failed; }
    }
    s_open = s_hooks[0].orig_func; s_read = s_hooks[2].orig_func;
    s_inflate = s_hooks[3].orig_func; s_create = s_hooks[4].orig_func;
    s_destruct[0] = s_hooks[5].orig_func; s_destruct[1] = s_hooks[6].orig_func;
    s_alloc = (void *)(bias + 0x3857ad); s_delete = (void *)(bias + 0x385769); s_crc = (void *)(bias + 0x48fb29);
    s_memory_vtable = bias + 0x5889f0; s_disk_vtable = bias + 0x588a50;
    s_stopped = 0; s_module = mod;
    so_loga("[PACK_CACHE] installed supported_so=c3555dd862c78d0d budget=%u slots=%u private_readers=1",PACK_BYTES,PACK_SLOTS);
    return 0;
failed:
    while (installed) so_unhook(&s_hooks[--installed]);
    so_loga("[PACK_CACHE] disabled reason=hook_failure"); return -1;
}
