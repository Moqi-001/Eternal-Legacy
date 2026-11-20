#include <malloc.h>
#include <stdlib.h>

#include <so_common/so_logger.h>

#include "bionic_malloc.h"

void *malloc_soloader(size_t size) {
    void *result = malloc(size);
    if (!result && size >= 4096) {
        // 失败时把 mallinfo 一起打出来：区分「真没内存」与「碎片凑不出连续块」
        struct mallinfo mi = mallinfo();
        so_loge("malloc(%u) FAILED (arena=%uKB fordblks=%uKB uordblks=%uKB, caller 0x%08X)",
                (unsigned)size, (unsigned)(mi.arena / 1024),
                (unsigned)(mi.fordblks / 1024), (unsigned)(mi.uordblks / 1024),
                (unsigned)__builtin_return_address(0));
    }
    return result;
}

void *calloc_soloader(size_t num, size_t size) {
    void *result = calloc(num, size);
    if (!result && num * size >= 4096) {
        so_loge("calloc(%u,%u) FAILED (caller 0x%08X)", (unsigned)num, (unsigned)size,
                (unsigned)__builtin_return_address(0));
    }
    return result;
}

void *realloc_soloader(void *ptr, size_t size) {
    void *result = realloc(ptr, size);
    if (!result && size >= 4096) {
        so_loge("realloc(0x%08X,%u) FAILED", (unsigned)ptr, (unsigned)size);
    }
    return result;
}

void *valloc_soloader(size_t size) {
    return valloc(size);
}

void free_soloader(void *ptr) {
    free(ptr);
}

// 引擎用 mallinfo() 判断还能分配多少：uordblks=已用、fordblks=剩余、ordblks=空闲块数
struct mallinfo mallinfo_soloader(void) {
    struct mallinfo mi = mallinfo();

    so_logd("mallinfo(): arena=%u uordblks=%u fordblks=%u ordblks=%u",
            (unsigned)mi.arena, (unsigned)mi.uordblks, (unsigned)mi.fordblks, (unsigned)mi.ordblks);
    return mi;
}
