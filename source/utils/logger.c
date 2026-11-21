/* Project console logger: never opens, writes or removes a log file. */
#include <stdarg.h>
#include <stdatomic.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <so_common/so_logger.h>

static SceKernelLwMutexWork sMutex;
static atomic_int sOnce;
static char sMessage[2048];

void so_log_printf(int type, const char *fmt, ...) {
    if (atomic_load(&sOnce) != 2) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&sOnce, &expected, 1)) {
            sceKernelCreateLwMutex(&sMutex, "console_log", 0, 0, NULL);
            atomic_store(&sOnce, 2);
        } else {
            while (atomic_load(&sOnce) != 2) sceKernelDelayThread(1000);
        }
    }
    sceKernelLockLwMutex(&sMutex, 1, NULL);
    va_list ap;
    va_start(ap, fmt);
    sceClibVsnprintf(sMessage, sizeof(sMessage), fmt, ap);
    va_end(ap);
    const char *prefix = type == SO_LT_ERROR ? "[ERROR]" : type == SO_LT_FATAL ? "[FATAL]" :
                         type == SO_LT_WARN ? "[WARN]" : type == SO_LT_DEBUG ? "[DEBUG]" :
                         type == SO_LT_INFO ? "[INFO]" : type == SO_LT_VERBOSE ? "[VERBOSE]" : "[LOG]";
    sceClibPrintf("%s %s\n", prefix, sMessage);
    sceKernelUnlockLwMutex(&sMutex, 1);
    if (type == SO_LT_FATAL) {
        sceKernelDelayThread(100000);
        sceKernelExitProcess(0);
    }
}
