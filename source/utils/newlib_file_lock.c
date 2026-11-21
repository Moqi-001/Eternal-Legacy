/* SDK newlib 4.1.0 compiles its FILE-pool locks to no-ops. Protect allocation,
 * initialization and the existing release hooks with ONE recursive mutex.
 *
 * --wrap=__sfp and --wrap=__sinit are essential: their internal no-op lock
 * calls were optimized away in findfp.o, so wrapping the lock names alone
 * cannot protect them. No pthread/newlib allocation is used to create this
 * lock: stdio can first be used during CRT or pthread TLS initialization.
 */
#include <stdatomic.h>
#include <stdio.h>
#include <sys/reent.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

static SceKernelLwMutexWork s_file_pool_lock;
static atomic_uint s_file_pool_ready;

static _Noreturn void file_lock_failed(int error) {
    /* Kernel console output does not construct a FILE or allocate newlib state. */
    sceClibPrintf("[FATAL] newlib FILE-pool lock failed: 0x%x\n", (unsigned)error);
    sceKernelExitProcess(1);
    for (;;) sceKernelDelayThread(1000000);
}

static void file_pool_acquire(void) {
    if (atomic_load_explicit(&s_file_pool_ready, memory_order_acquire) != 2) {
        unsigned expected = 0;
        if (atomic_compare_exchange_strong_explicit(&s_file_pool_ready, &expected, 1,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            int result = sceKernelCreateLwMutex(&s_file_pool_lock, "newlib_FILE_pool",
                                               SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
            if (result < 0) file_lock_failed(result);
            atomic_store_explicit(&s_file_pool_ready, 2, memory_order_release);
        } else {
            while (atomic_load_explicit(&s_file_pool_ready, memory_order_acquire) != 2)
                sceKernelDelayThread(1000);
        }
    }
    int result = sceKernelLockLwMutex(&s_file_pool_lock, 1, NULL);
    if (result < 0) file_lock_failed(result);
}

static void file_pool_release(void) {
    int result = sceKernelUnlockLwMutex(&s_file_pool_lock, 1);
    if (result < 0) file_lock_failed(result);
}

extern FILE *__real___sfp(struct _reent *);
extern void __real___sinit(struct _reent *);

FILE *__wrap___sfp(struct _reent *reent) {
    file_pool_acquire();
    FILE *file = __real___sfp(reent);
    file_pool_release();
    return file;
}

void __wrap___sinit(struct _reent *reent) {
    file_pool_acquire();
    __real___sinit(reent);
    file_pool_release();
}

void __wrap___sfp_lock_acquire(void) { file_pool_acquire(); }
void __wrap___sfp_lock_release(void) { file_pool_release(); }
void __wrap___sinit_lock_acquire(void) { file_pool_acquire(); }
void __wrap___sinit_lock_release(void) { file_pool_release(); }
