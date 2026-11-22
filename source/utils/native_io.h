#ifndef ETERNAL_NATIVE_IO_H
#define ETERNAL_NATIVE_IO_H
#include <stdio.h>

/* Mapped Vita paths only. hit: 0=disk/fallback, 1=content reuse, 2=known absent. */
FILE *native_io_fopen(const char *path, const char *mode, int *hit);
/* Bionic flags; successful writable descriptor opens retire the same scoped
 * resource key as fopen. Saves/unknown paths retain native filesystem I/O. */
int native_io_open(const char *path, int flags, int mode);
/* Paired hooks for mkdir/rename/unlink/remove/rmdir. Begin blocks scoped reads
 * during the native operation; end preserves errno and retires only successful
 * mutations. Rename begins/ends both source and destination. NULL tokens denote
 * paths outside the resource bundle. */
int native_io_mutation_begin(const char *mapped_path, void **token);
void native_io_mutation_end(void *token, const char *mapped_path, int success);
/* Negative evidence only; positive/unknown paths retain the OS operation. */
int native_io_known_absent(const char *mapped_path);
/* Bounded background byte loads; never constructs game/audio objects. */
void native_io_prefetch(const char *mapped_path);
/* Exact complete-root immutable identity, including assets above the byte-cache
 * size limit. Mutation guards are rechecked after the index lookup. */
int native_io_immutable_asset(const char *mapped_path, size_t expected_size);
typedef struct { const char *path; size_t expected_size; } NativeIoWarmResource;
typedef struct {
    unsigned requested, ready, pending, failed;
    unsigned failed_mask;
    size_t ready_bytes;
} NativeIoWarmStatus;
/* One bounded startup set: <=16 existing assets, <=256KiB in total, still part
 * of the 16MiB cache. Only bytes are warmed; ready entries resist LRU eviction.
 * No waiting here. Invalid/mutated assets fail individually and stay native. */
int native_io_warm_start(const NativeIoWarmResource *resources, unsigned count);
void native_io_warm_status(NativeIoWarmStatus *status);
void native_io_prefetch_stop(void);
void native_io_poll(int force);
void native_io_shutdown(void);
#endif
