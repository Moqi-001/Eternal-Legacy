/* Project console logger: never opens, writes or removes a log file. */
#include <stdarg.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <so_common/so_logger.h>
#include "console_logger.h"
#include "patch/frame_profile.h"

static SceKernelLwMutexWork sMutex;
static atomic_int sOnce;
static char sMessage[2048];
enum { LOG_SLOTS = 64, LOG_BYTES = 2048, RAW_CONSOLE = -1 };
typedef struct { int type; char text[LOG_BYTES]; } Message;
static Message sQueue[LOG_SLOTS];
static pthread_mutex_t sQueueLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sReady = PTHREAD_COND_INITIALIZER;
static pthread_t sWorker;
static unsigned sHead, sCount;
/* 0 synchronous boot, 1 worker running, 2 stopping, 3 worker finished. */
static atomic_int sAsync;
static atomic_uint sEnqueued, sPrinted, sDropped, sTruncated, sQueued, sPeak;
static atomic_uint sFormatUs, sPrintUs, sMaxFormat, sMaxPrint;
static uint64_t sReport;

static void init_console(void) {
    if (atomic_load(&sOnce) != 2) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&sOnce, &expected, 1)) {
            sceKernelCreateLwMutex(&sMutex, "console_log", 0, 0, NULL);
            atomic_store(&sOnce, 2);
        } else {
            while (atomic_load(&sOnce) != 2) sceKernelDelayThread(1000);
        }
    }
}
static void maximum(atomic_uint *counter, unsigned value) {
    unsigned old = atomic_load_explicit(counter, memory_order_relaxed);
    while (value > old && !atomic_compare_exchange_weak_explicit(counter, &old, value,
            memory_order_relaxed, memory_order_relaxed)) {}
}
static const char *prefix(int type) {
    const char *prefix = type == SO_LT_ERROR ? "[ERROR]" : type == SO_LT_FATAL ? "[FATAL]" :
                         type == SO_LT_WARN ? "[WARN]" : type == SO_LT_DEBUG ? "[DEBUG]" :
                         type == SO_LT_INFO ? "[INFO]" : type == SO_LT_VERBOSE ? "[VERBOSE]" : "[LOG]";
    return prefix;
}
static void print_message(int type, const char *text) {
    init_console();
    sceKernelLockLwMutex(&sMutex, 1, NULL);
    if (type == RAW_CONSOLE) sceClibPrintf("%s", text);
    else sceClibPrintf("%s %s\n", prefix(type), text);
    sceKernelUnlockLwMutex(&sMutex, 1);
}
static int enqueue(int type, const char *fmt, va_list ap, int raw) {
    uint64_t start = sceKernelGetProcessTimeWide();
    /* Never block a producer behind a slow console or another formatter. */
    if (pthread_mutex_trylock(&sQueueLock)) {
        atomic_fetch_add(&sDropped, 1);
        return raw ? vsnprintf(NULL, 0, fmt, ap) : 0;
    }
    if (sCount == LOG_SLOTS || atomic_load(&sAsync) != 1) {
        pthread_mutex_unlock(&sQueueLock);
        atomic_fetch_add(&sDropped, 1);
        return raw ? vsnprintf(NULL, 0, fmt, ap) : 0;
    }
    Message *message = &sQueue[(sHead + sCount) % LOG_SLOTS];
    message->type = type;
    int length = raw ? vsnprintf(message->text, LOG_BYTES, fmt, ap) :
                       sceClibVsnprintf(message->text, LOG_BYTES, fmt, ap);
    message->text[LOG_BYTES - 1] = 0;
    if (length < 0) {
        pthread_mutex_unlock(&sQueueLock);
        return length;
    }
    if (length >= LOG_BYTES) atomic_fetch_add(&sTruncated, 1);
    ++sCount;
    atomic_store(&sQueued, sCount);
    maximum(&sPeak, sCount);
    atomic_fetch_add(&sEnqueued, 1);
    pthread_cond_signal(&sReady);
    pthread_mutex_unlock(&sQueueLock);
    unsigned elapsed = (unsigned)(sceKernelGetProcessTimeWide() - start);
    atomic_fetch_add(&sFormatUs, elapsed); maximum(&sMaxFormat, elapsed);
    return length;
}
static void *console_worker(void *unused) {
    (void)unused;
    for (;;) {
        Message message;
        pthread_mutex_lock(&sQueueLock);
        while (!sCount && atomic_load(&sAsync) == 1) pthread_cond_wait(&sReady, &sQueueLock);
        if (!sCount) { pthread_mutex_unlock(&sQueueLock); break; }
        message = sQueue[sHead]; sHead = (sHead + 1) % LOG_SLOTS; --sCount;
        atomic_store(&sQueued, sCount);
        pthread_mutex_unlock(&sQueueLock);
        uint64_t start = sceKernelGetProcessTimeWide();
        print_message(message.type, message.text);
        unsigned elapsed = (unsigned)(sceKernelGetProcessTimeWide() - start);
        atomic_fetch_add(&sPrinted, 1); atomic_fetch_add(&sPrintUs, elapsed); maximum(&sMaxPrint, elapsed);
    }
    atomic_store(&sAsync, 3);
    return NULL;
}
int console_logger_start(void) {
    int saved_errno = errno;
    if (atomic_load(&sAsync) == 1) return 0;
    init_console();
    atomic_store(&sAsync, 1);
    int error = pthread_create(&sWorker, NULL, console_worker, NULL);
    if (error) atomic_store(&sAsync, 0);
    errno = saved_errno;
    return error ? -1 : 0;
}
void so_log_printf(int type, const char *fmt, ...) {
    int saved_errno = errno;
    uint64_t start = sceKernelGetProcessTimeWide();
    va_list ap; va_start(ap, fmt);
    if (type != SO_LT_FATAL && atomic_load(&sAsync) == 1) {
        enqueue(type, fmt, ap, 0);
    } else {
        init_console();
        sceKernelLockLwMutex(&sMutex, 1, NULL);
        sceClibVsnprintf(sMessage, sizeof(sMessage), fmt, ap);
        sceClibPrintf("%s %s\n", prefix(type), sMessage);
        sceKernelUnlockLwMutex(&sMutex, 1);
    }
    va_end(ap);
    frame_profile_log_cost(sceKernelGetProcessTimeWide() - start);
    errno = saved_errno;
    if (type == SO_LT_FATAL) {
        sceKernelDelayThread(100000);
        sceKernelExitProcess(0);
    }
}
int console_game_printf(const char *format, ...) {
    int saved_errno = errno;
    uint64_t start = sceKernelGetProcessTimeWide();
    va_list ap; va_start(ap, format);
    int result = atomic_load(&sAsync) == 1 ? enqueue(RAW_CONSOLE, format, ap, 1) : vprintf(format, ap);
    va_end(ap);
    frame_profile_log_cost(sceKernelGetProcessTimeWide() - start);
    errno = saved_errno;
    return result;
}
int console_game_puts(const char *text) { return console_game_printf("%s\n", text); }
typedef struct {
    unsigned enqueued, printed, dropped, truncated, queued, peak_queued;
    uint64_t format_us, print_us;
    unsigned max_format_us, max_print_us;
} ConsoleLoggerStats;
static ConsoleLoggerStats console_snapshot(void) {
    return (ConsoleLoggerStats){
        .enqueued=atomic_load(&sEnqueued), .printed=atomic_load(&sPrinted),
        .dropped=atomic_load(&sDropped), .truncated=atomic_load(&sTruncated),
        .queued=atomic_load(&sQueued), .peak_queued=atomic_load(&sPeak),
        .format_us=atomic_load(&sFormatUs), .print_us=atomic_load(&sPrintUs),
        .max_format_us=atomic_load(&sMaxFormat), .max_print_us=atomic_load(&sMaxPrint)
    };
}
void console_logger_poll(int force) {
    int saved_errno = errno;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (!force && now - sReport < 5000000) return;
    sReport = now;
    ConsoleLoggerStats stats = console_snapshot();
    so_loga("[CONSOLE] async=%d queued=%u peak=%u enqueued=%u printed=%u dropped=%u truncated=%u format_us=%llu print_us=%llu max_format_us=%u max_print_us=%u",
            atomic_load(&sAsync)==1, stats.queued, stats.peak_queued, stats.enqueued, stats.printed,
            stats.dropped, stats.truncated, (unsigned long long)stats.format_us,
            (unsigned long long)stats.print_us, stats.max_format_us, stats.max_print_us);
    errno = saved_errno;
}
void console_logger_shutdown(void) {
    int saved_errno = errno;
    if (atomic_load(&sAsync) != 1) return;
    pthread_mutex_lock(&sQueueLock);
    atomic_store(&sAsync, 2); pthread_cond_signal(&sReady);
    pthread_mutex_unlock(&sQueueLock);
    /* A disconnected debug sink must not prevent the application from exiting.
     * Queue storage is static and remains valid until the process terminates. */
    uint64_t start = sceKernelGetProcessTimeWide();
    while (atomic_load(&sAsync) != 3 && sceKernelGetProcessTimeWide()-start < 200000) sceKernelDelayThread(1000);
    if (atomic_load(&sAsync) == 3) pthread_join(sWorker, NULL); else pthread_detach(sWorker);
    errno = saved_errno;
}
