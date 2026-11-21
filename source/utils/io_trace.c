/* No logger file I/O; bounded console details plus independent kernel counters. */
#include "io_trace.h"
#include "perf.h"
#if PERF_DIAGNOSTICS
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <so_common/so_logger.h>

#define TRACE_SLOTS 256
enum { T_KOPEN, T_KSTAT, T_KREAD, T_KPREAD, T_KSEEK, T_KCLOSE, T_COUNT };
static const char *s_trace_names[T_COUNT] = {
    "kernel_open", "kernel_stat",
    "kernel_read", "kernel_pread", "kernel_seek", "kernel_close"
};
typedef struct { uint64_t us, bytes, requested, peak; unsigned calls, failures, details; } TraceCounter;
typedef struct { int live, uid; unsigned generation; int64_t offset; char path[512]; } KernelFile;
typedef struct { unsigned generation; int64_t offset; char path[512]; } KernelSnapshot;
static KernelFile s_kernel_files[TRACE_SLOTS];
static TraceCounter s_trace_counts[T_COUNT];
static pthread_mutex_t s_io_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned s_generation;

static uint64_t s_trace_last;
static uintptr_t s_so_start, s_so_end;
static intptr_t s_so_bias;

void io_trace_init(so_module *mod) {
    s_so_start = (uintptr_t)so_module_get_load_base(mod);
    s_so_end = s_so_start + so_module_get_load_size(mod);
    s_so_bias = so_module_get_load_bias(mod);
}

static void record_trace(unsigned op, const KernelSnapshot *s, int uid, int64_t arg,
                         int whence, size_t requested, size_t returned, int ok,
                         int result, uint64_t us, uintptr_t caller) {
    int saved_errno = errno;
    pthread_mutex_lock(&s_io_lock);
    TraceCounter *counter = &s_trace_counts[op];
    ++counter->calls; counter->failures += !ok;
    counter->bytes += returned; counter->requested += requested; counter->us += us;
    if (us > counter->peak) counter->peak = us;
    int detail = 0;
    if ((!ok || us >= 20000) && counter->details++ < 4) detail = 1;
    pthread_mutex_unlock(&s_io_lock);
    if (detail) {
        uintptr_t elf = caller >= s_so_start && caller < s_so_end ? caller - s_so_bias : 0;
        so_loga("[IO] detail op=%s thread=%d uid=%d gen=%u offset=%lld arg=%lld whence=%d requested=%u returned=%u ok=%d result=%d errno=%d us=%llu caller=%p elf=0x%x path=%s",
                s_trace_names[op], sceKernelGetThreadId(), uid, s ? s->generation : 0,
                (long long)(s ? s->offset : -1), (long long)arg, whence,
                (unsigned)requested, (unsigned)returned, ok, result, ok ? 0 : saved_errno,
                (unsigned long long)us, (void *)caller, (unsigned)elf,
                s ? s->path : "<untracked>");
    }
    io_trace_poll(0);
    errno = saved_errno;
}

void io_trace_poll(int force) {
    int saved_errno = errno;
    uint64_t now = perf_now_us();
    if (pthread_mutex_trylock(&s_io_lock)) return;
    if (!force && now - s_trace_last < 1000000) { pthread_mutex_unlock(&s_io_lock); errno = saved_errno; return; }
    s_trace_last = now;
    TraceCounter counters[T_COUNT]; memcpy(counters, s_trace_counts, sizeof(counters));
    memset(s_trace_counts, 0, sizeof(s_trace_counts));
    pthread_mutex_unlock(&s_io_lock);
    for (unsigned i = 0; i < T_COUNT; ++i) if (counters[i].calls)
        so_loga("[IO] op=%s calls=%u fail=%u requested=%llu bytes=%llu total_us=%llu max_us=%llu",
                s_trace_names[i], counters[i].calls, counters[i].failures,
                (unsigned long long)counters[i].requested, (unsigned long long)counters[i].bytes,
                (unsigned long long)counters[i].us, (unsigned long long)counters[i].peak);
    errno = saved_errno;
}

extern SceUID __real_sceIoOpen(const char *, int, int);
extern int __real_sceIoClose(SceUID);
extern int __real_sceIoRead(SceUID, void *, SceSize);
extern int __real_sceIoPread(SceUID, void *, SceSize, SceOff);
extern SceOff __real_sceIoLseek(SceUID, SceOff, int);
extern long __real_sceIoLseek32(SceUID, long, int);
extern int __real_sceIoGetstat(const char *, SceIoStat *);

static KernelFile *find_kernel(int uid) {
    for (unsigned i = 0; i < TRACE_SLOTS; ++i)
        if (s_kernel_files[i].live && s_kernel_files[i].uid == uid) return &s_kernel_files[i];
    return NULL;
}
static void kernel_snapshot(int uid, KernelSnapshot *s) {
    memset(s, 0, sizeof(*s)); s->offset = -1;
    pthread_mutex_lock(&s_io_lock);
    KernelFile *file = find_kernel(uid);
    if (file) { s->generation = file->generation; s->offset = file->offset; strcpy(s->path, file->path); }
    else strcpy(s->path, "<untracked>");
    pthread_mutex_unlock(&s_io_lock);
}

SceUID __wrap_sceIoOpen(const char *path, int flags, int mode) {
    uint64_t start = perf_now_us();
    int uid = __real_sceIoOpen(path, flags, mode), saved_errno = errno;
    KernelSnapshot s = { .offset = 0 };
    snprintf(s.path, sizeof(s.path), "%s", path ? path : "<null>");
    if (uid >= 0) {
        pthread_mutex_lock(&s_io_lock);
        KernelFile *file = find_kernel(uid);
        if (!file) for (unsigned i = 0; i < TRACE_SLOTS; ++i) if (!s_kernel_files[i].live) { file = &s_kernel_files[i]; break; }
        if (file) {
            file->live = 1; file->uid = uid; file->offset = 0; file->generation = ++s_generation;
            strcpy(file->path, s.path); s.generation = file->generation;
        }
        pthread_mutex_unlock(&s_io_lock);
    }
    errno = saved_errno;
    record_trace(T_KOPEN, &s, uid, flags, 0, 0, 0, uid >= 0, uid, perf_now_us() - start, (uintptr_t)__builtin_return_address(0));
    return uid;
}
int __wrap_sceIoClose(SceUID uid) {
    KernelSnapshot s; kernel_snapshot(uid, &s);
    uint64_t start = perf_now_us(); int result = __real_sceIoClose(uid), saved_errno = errno;
    if (result >= 0) {
        pthread_mutex_lock(&s_io_lock);
        KernelFile *file = find_kernel(uid);
        if (file && file->generation == s.generation) { memset(file, 0, sizeof(*file)); }
        pthread_mutex_unlock(&s_io_lock);
    }
    errno = saved_errno;
    record_trace(T_KCLOSE, &s, uid, 0, 0, 0, 0, result >= 0, result, perf_now_us() - start, (uintptr_t)__builtin_return_address(0));
    return result;
}
int __wrap_sceIoRead(SceUID uid, void *buffer, SceSize count) {
    KernelSnapshot s; kernel_snapshot(uid, &s);
    uint64_t start = perf_now_us(); int n = __real_sceIoRead(uid, buffer, count), saved_errno = errno;
    if (n > 0) {
        pthread_mutex_lock(&s_io_lock);
        KernelFile *file = find_kernel(uid);
        if (file && file->generation == s.generation && file->offset >= 0) file->offset += n;
        pthread_mutex_unlock(&s_io_lock);
    }
    errno = saved_errno;
    record_trace(T_KREAD, &s, uid, 0, 0, count, n > 0 ? n : 0, n >= 0, n, perf_now_us() - start, (uintptr_t)__builtin_return_address(0));
    return n;
}
int __wrap_sceIoPread(SceUID uid, void *buffer, SceSize count, SceOff offset) {
    KernelSnapshot s; kernel_snapshot(uid, &s);
    uint64_t start = perf_now_us(); int n = __real_sceIoPread(uid, buffer, count, offset), saved_errno = errno;
    errno = saved_errno;
    record_trace(T_KPREAD, &s, uid, offset, SEEK_SET, count, n > 0 ? n : 0, n >= 0, n, perf_now_us() - start, (uintptr_t)__builtin_return_address(0));
    return n;
}
SceOff __wrap_sceIoLseek(SceUID uid, SceOff offset, int whence) {
    KernelSnapshot s; kernel_snapshot(uid, &s);
    uint64_t start = perf_now_us(); SceOff result = __real_sceIoLseek(uid, offset, whence); int saved_errno = errno;
    if (result >= 0) {
        pthread_mutex_lock(&s_io_lock);
        KernelFile *file = find_kernel(uid);
        if (file && file->generation == s.generation) file->offset = result;
        pthread_mutex_unlock(&s_io_lock);
    }
    errno = saved_errno;
    record_trace(T_KSEEK, &s, uid, offset, whence, 0, 0, result >= 0, result >= 0 ? 0 : (int)result, perf_now_us() - start, (uintptr_t)__builtin_return_address(0));
    return result;
}
long __wrap_sceIoLseek32(SceUID uid, long offset, int whence) {
    KernelSnapshot s; kernel_snapshot(uid, &s);
    uint64_t start = perf_now_us(); long result = __real_sceIoLseek32(uid, offset, whence); int saved_errno = errno;
    if (result >= 0) {
        pthread_mutex_lock(&s_io_lock);
        KernelFile *file = find_kernel(uid);
        if (file && file->generation == s.generation) file->offset = result;
        pthread_mutex_unlock(&s_io_lock);
    }
    errno = saved_errno;
    record_trace(T_KSEEK, &s, uid, offset, whence, 0, 0, result >= 0, result >= 0 ? 0 : result,
                 perf_now_us() - start, (uintptr_t)__builtin_return_address(0));
    return result;
}
int __wrap_sceIoGetstat(const char *path, SceIoStat *stat) {
    uint64_t start = perf_now_us(); int result = __real_sceIoGetstat(path, stat), saved_errno = errno;
    KernelSnapshot s = { .offset = -1 };
    snprintf(s.path, sizeof(s.path), "%s", path ? path : "<null>");
    errno = saved_errno;
    record_trace(T_KSTAT, &s, -1, 0, 0, 0, 0, result >= 0, result, perf_now_us() - start, (uintptr_t)__builtin_return_address(0));
    return result;
}
#endif
