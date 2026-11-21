#ifndef ETERNAL_NATIVE_IO_H
#define ETERNAL_NATIVE_IO_H
#include <stdio.h>
#include <stdint.h>

/* Mapped Vita paths only. hit: 0=disk/fallback, 1=content reuse, 2=known absent. */
FILE *native_io_fopen(const char *path, const char *mode, int *hit);
/* Same operation with the original game caller retained for bounded routing /
 * first successful writable-open invalidation diagnostics. */
FILE *native_io_fopen_at(const char *path, const char *mode, int *hit, uintptr_t caller);
/* Bionic flags; successful writable descriptor opens retire the same scoped
 * resource key as fopen. Saves/unknown paths retain native filesystem I/O. */
int native_io_open_at(const char *path, int flags, int mode, uintptr_t caller);
/* Bounded background byte loads; never constructs game/audio objects. */
void native_io_prefetch(const char *mapped_path);
void native_io_prefetch_stop(void);
void native_io_poll(int force);
void native_io_shutdown(void);
#endif
