/* Bounded counters, reported once per second through the console logger only. */
#include "perf.h"

#if PERF_DIAGNOSTICS
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>
#include <so_common/so_logger.h>
#include <vitaGL.h>

typedef struct {
    atomic_uint calls, failures, bytes, us, max_us, hits, details;
} Counter;
static Counter s_ops[PERF_OP_COUNT];
static const char *s_names[] = {
    "res_length", "res_full", "res_bytes", "fopen", "open", "fread", "read", "seek", "stat",
    "shader_compile", "program_link", "texture_upload"
};
static atomic_uint s_frames, s_input, s_render, s_swap, s_frame_us, s_frame_max;
static atomic_uint s_histogram[5], s_last_ms, s_gpu_ready;
static unsigned s_memory_last_ms;
static int s_memory_reported;
static pthread_mutex_t s_report_lock = PTHREAD_MUTEX_INITIALIZER;
static void update_max(atomic_uint *target, unsigned value) {
    unsigned old = atomic_load_explicit(target, memory_order_relaxed);
    while (value > old && !atomic_compare_exchange_weak_explicit(
            target, &old, value, memory_order_relaxed, memory_order_relaxed)) {}
}

void perf_record(PerfOp op, const char *path, uint64_t start, size_t bytes, int ok, int hit) {
    int saved_errno = errno;
    uint64_t duration = perf_now_us() - start;
    unsigned us = duration > UINT_MAX ? UINT_MAX : (unsigned)duration;
    Counter *c = &s_ops[op];
    atomic_fetch_add_explicit(&c->calls, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->failures, !ok, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->bytes, (unsigned)bytes, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->us, us, memory_order_relaxed);
    atomic_fetch_add_explicit(&c->hits, hit, memory_order_relaxed);
    update_max(&c->max_us, us);
    // Limit error/slow details to four per operation per reporting interval.
    if ((us >= 20000 || (!ok && (op >= PERF_COMPILE || (saved_errno != ENOENT && saved_errno != EISDIR)))) && atomic_fetch_add_explicit(&c->details, 1, memory_order_relaxed) < 4)
        so_loga("[PERF] detail op=%s ok=%d us=%u bytes=%u errno=%d path=%s",
                s_names[op], ok, us, (unsigned)bytes, ok ? 0 : saved_errno, path ? path : "-");
    /* Reports belong to the GL frame tail. In particular, a fast failed probe
     * must not synchronously print a whole performance window inside Update. */
    errno = saved_errno;
}

void perf_gpu_ready(void) { atomic_store(&s_gpu_ready, 1); }

void perf_frame(uint64_t input, uint64_t render, uint64_t swap, uint64_t total) {
    atomic_fetch_add(&s_frames, 1);
    atomic_fetch_add(&s_input, (unsigned)input);
    atomic_fetch_add(&s_render, (unsigned)render);
    atomic_fetch_add(&s_swap, (unsigned)swap);
    atomic_fetch_add(&s_frame_us, (unsigned)total);
    update_max(&s_frame_max, (unsigned)total);
    unsigned bucket = total <= 17000 ? 0 : total <= 25000 ? 1 : total <= 34000 ? 2 : total <= 50000 ? 3 : 4;
    atomic_fetch_add(&s_histogram[bucket], 1);
}

void perf_poll(int force) {
    int saved_errno = errno;
    unsigned now = (unsigned)(perf_now_us() / 1000);
    if (!force && now - atomic_load(&s_last_ms) < 1000) return;
    if (pthread_mutex_trylock(&s_report_lock)) return;
    // Another reporter may have advanced the window while this thread paused.
    now = (unsigned)(perf_now_us() / 1000);
    unsigned interval = now - atomic_load(&s_last_ms);
    if (!force && interval < 1000) { pthread_mutex_unlock(&s_report_lock); return; }
    atomic_store(&s_last_ms, now);
    unsigned frames = atomic_exchange(&s_frames, 0);
    unsigned input = atomic_exchange(&s_input, 0), render = atomic_exchange(&s_render, 0);
    unsigned swap = atomic_exchange(&s_swap, 0), total = atomic_exchange(&s_frame_us, 0);
    unsigned max = atomic_exchange(&s_frame_max, 0), h[5];
    for (int i = 0; i < 5; ++i) h[i] = atomic_exchange(&s_histogram[i], 0);
    so_loga("[PERF] frame t_ms=%u window_ms=%u frames=%u fps_x100=%u avg_us=%u max_us=%u input_us=%u render_us=%u swap_us=%u hist_17_25_34_50_over=%u/%u/%u/%u/%u",
            now, interval, frames, interval ? frames * 100000 / interval : 0,
            frames ? total / frames : 0, max, frames ? input / frames : 0,
            frames ? render / frames : 0, frames ? swap / frames : 0,
            h[0], h[1], h[2], h[3], h[4]);
    for (int i = 0; i < PERF_OP_COUNT; ++i) {
        Counter *c = &s_ops[i];
        unsigned calls = atomic_exchange(&c->calls, 0), failures = atomic_exchange(&c->failures, 0);
        unsigned bytes = atomic_exchange(&c->bytes, 0), us = atomic_exchange(&c->us, 0);
        unsigned peak = atomic_exchange(&c->max_us, 0), hits = atomic_exchange(&c->hits, 0);
        atomic_store(&c->details, 0);
        if (calls || failures || bytes)
            so_loga("[PERF] op=%s calls=%u fail=%u bytes=%u total_us=%u max_us=%u cache_hits=%u",
                    s_names[i], calls, failures, bytes, us, peak, hits);
    }
    if (force || !s_memory_reported || now-s_memory_last_ms >= 5000) {
        s_memory_last_ms = now; s_memory_reported = 1;
        struct mallinfo mi = mallinfo();
        so_loga("[PERF] heap used_kb=%u free_kb=%u arena_kb=%u free_blocks=%u",
                (unsigned)mi.uordblks / 1024, (unsigned)mi.fordblks / 1024,
                (unsigned)mi.arena / 1024, (unsigned)mi.ordblks);
        if (atomic_load(&s_gpu_ready))
            so_loga("[PERF] gpu_free_kb ram=%u cdram=%u phycont=%u cdialog=%u",
                    (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024), (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024),
                    (unsigned)(vglMemFree(VGL_MEM_SLOW) / 1024), (unsigned)(vglMemFree(VGL_MEM_BUDGET) / 1024));
    }
    pthread_mutex_unlock(&s_report_lock);
    errno = saved_errno;
}
#endif
