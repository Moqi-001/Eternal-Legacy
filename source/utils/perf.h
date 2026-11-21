#ifndef ETERNAL_PERF_H
#define ETERNAL_PERF_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PERF_RES_LENGTH, PERF_RES_FULL, PERF_RES_BYTES,
    PERF_FOPEN, PERF_OPEN, PERF_FREAD, PERF_READ, PERF_SEEK, PERF_STAT,
    PERF_COMPILE, PERF_LINK, PERF_UPLOAD, PERF_OP_COUNT
} PerfOp;

#if PERF_DIAGNOSTICS
#include <psp2/kernel/processmgr.h>
static inline uint64_t perf_now_us(void) { return sceKernelGetProcessTimeWide(); }
void perf_record(PerfOp op, const char *path, uint64_t start, size_t bytes, int ok, int cache_hit);
void perf_frame(uint64_t input, uint64_t render, uint64_t swap, uint64_t total);
void perf_gpu_ready(void);
void perf_poll(int force);
#else
static inline uint64_t perf_now_us(void) { return 0; }
static inline void perf_record(PerfOp op, const char *p, uint64_t t, size_t n, int ok, int hit) {}
static inline void perf_frame(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {}
static inline void perf_gpu_ready(void) {}
static inline void perf_poll(int force) {}
#endif

#endif
