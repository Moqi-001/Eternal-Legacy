/*
 * Vita 的 pthread-embedded 把 pthread_key 的存储交给 pte_osTls*。VitaSDK 自带
 * 的实现有两个问题：
 *   1. pte_osTlsAlloc 用随机 32 位数当句柄、pte_osTlsFree 是空实现，键删除后各
 *      线程链表里的条目永不回收，长命线程反复 create/delete 会一直增长；
 *   2. 句柄无法校验，一旦槽位复用，持旧句柄的调用方实际操作的是新键的存储。
 *
 * 这里改成「句柄 = 槽号 + 代数」：句柄是纯数值，不指向任何对象；删除时槽位回到
 * 空闲、代数继续递增，旧句柄在任何时刻都校验不过，读写都拿不到东西。
 * 键的分配状态是进程级的（KeyTable，带锁），值按线程各存一份（TlsTable，挂在
 * VitaSDK 的 TLS 块里，线程退出由 SDK 顺着链表释放）。
 */
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/threadmgr.h>
#include <vitasdk/utils.h>

// 句柄布局：低 TLS_SLOT_BITS 位是槽号，其余是代数。代数取 1..TLS_GEN_MAX，
// 0 保留给"空闲/无效"。
#define TLS_SLOT_BITS 10
#define TLS_SLOT_MAX  (1u << TLS_SLOT_BITS)
#define TLS_SLOT_MASK (TLS_SLOT_MAX - 1u)
#define TLS_GEN_MAX   (UINT32_MAX >> TLS_SLOT_BITS)

enum { TLS_OK           = 0,
       TLS_NO_RESOURCES = 1 };

typedef struct {
    unsigned int handle;  // 该槽位当前的句柄（0 = 空闲）
    void        *value;
    int          next_free;
} TlsValue;

typedef struct TlsTable {
    // VitaSDK 的线程清理会顺着第一个指针逐块 free，所以它必须排在最前
    struct TlsTable *next;
    unsigned int     capacity;
    unsigned int     count;
    int              free_head;
    TlsValue         values[];
} TlsTable;

typedef struct KeyValue {
    unsigned int handle;  // 该槽位当前的句柄（0 = 空闲）
    int          next_free;
} KeyValue;

typedef struct KeyTable {
    unsigned int capacity;
    unsigned int count;
    unsigned int generation;
    int          free_head;
    KeyValue     keys[];
} KeyTable;

static atomic_flag s_key_lock  = ATOMIC_FLAG_INIT;
static KeyTable   *s_key_table = NULL;

static inline void lock_keys(void) {
    while (atomic_flag_test_and_set_explicit(&s_key_lock, memory_order_acquire)) {
        sceKernelDelayThread(1);
    }
}

static inline void unlock_keys(void) {
    atomic_flag_clear_explicit(&s_key_lock, memory_order_release);
}

// 句柄是否仍然有效（槽位在范围内且记录的句柄完全一致）
static int get_key_index(KeyTable *table, unsigned int handle) {
    if (!table || handle == 0) {
        return -1;
    }

    unsigned int index = handle & TLS_SLOT_MASK;
    if (index >= table->count || table->keys[index].handle != handle) {
        return -1;
    }

    return (int)index;
}

// 从本线程的空闲链表取一个槽位；没有则返回 -1
static int take_free_slot(TlsTable *table) {
    if (table->free_head < 0) {
        return -1;
    }

    int index        = table->free_head;
    table->free_head = table->values[index].next_free;
    return index;
}

// 懒清除：把本线程里已经失效（键被删或复用）的条目收回空闲链表。
// 只在"需要新槽位又没有空闲槽"时调用——有槽可用就不必扫。
static void sweep_invalid_slots(TlsTable *table) {
    lock_keys();
    for (unsigned int i = 0; i < table->count; i++) {
        if (table->values[i].handle != 0 &&
            get_key_index(s_key_table, table->values[i].handle) < 0) {
            table->values[i].handle    = 0;
            table->values[i].next_free = table->free_head;
            table->free_head           = (int)i;
        }
    }
    unlock_keys();
}

int __wrap_pte_osTlsAlloc(unsigned int *pKey) {
    if (!pKey) {
        return TLS_NO_RESOURCES;
    }

    lock_keys();

    KeyTable *table = s_key_table;
    if (!table) {
        table = calloc(1, sizeof(KeyTable) + 32 * sizeof(KeyValue));
        if (!table) {
            *pKey = 0;
            unlock_keys();
            return TLS_NO_RESOURCES;
        }
        table->capacity   = 32;
        table->count      = 0;
        table->generation = 0;
        table->free_head  = -1;

        s_key_table = table;
    }

    unsigned int index;

    if (table->free_head >= 0) {
        index            = (unsigned int)table->free_head;
        table->free_head = table->keys[index].next_free;
    } else {
        if (table->count >= TLS_SLOT_MAX) {  // 超过最大容量限制
            *pKey = 0;
            unlock_keys();
            return TLS_NO_RESOURCES;
        }
        if (table->count >= table->capacity) {  // 需要扩容
            unsigned int old_capacity = table->capacity;
            unsigned int new_capacity = old_capacity * 2;

            table = realloc(table, sizeof(KeyTable) + new_capacity * sizeof(KeyValue));
            if (!table) {
                *pKey = 0;
                unlock_keys();
                return TLS_NO_RESOURCES;
            }

            table->capacity = new_capacity;
            memset(table->keys + old_capacity, 0, (new_capacity - old_capacity) * sizeof(KeyValue));
            s_key_table = table;
        }

        index = table->count++;
    }

    // 代数每次分配递增，回绕时回到 1（0 表示无效）
    unsigned int generation = table->generation + 1;
    if (generation > TLS_GEN_MAX) {
        generation = 1;
    }

    unsigned int handle       = (generation << TLS_SLOT_BITS) | index;
    table->generation         = generation;
    table->keys[index].handle = handle;
    *pKey                     = handle;

    unlock_keys();
    return TLS_OK;
}

int __wrap_pte_osTlsFree(unsigned int handle) {
    lock_keys();

    KeyTable *table = s_key_table;
    int       index = get_key_index(table, handle);
    if (index < 0) {
        unlock_keys();
        return TLS_NO_RESOURCES;
    }

    table->keys[index].handle    = 0;
    table->keys[index].next_free = table->free_head;
    table->free_head             = index;

    unlock_keys();
    return TLS_OK;
}

int __wrap_pte_osTlsSetValue(unsigned int handle, void *value) {
    // 键必须仍然有效：旧句柄在这里就被挡掉，写不进任何线程的存储
    lock_keys();
    int valid = (get_key_index(s_key_table, handle) >= 0);
    unlock_keys();
    if (!valid) {
        return TLS_NO_RESOURCES;
    }

    int index = -1;

    TlsTable *table = *(TlsTable **)vitasdk_get_tls_data(0);
    if (!table) {
        table = calloc(1, sizeof(TlsTable) + 32 * sizeof(TlsValue));
        if (!table) {
            return TLS_NO_RESOURCES;
        }
        table->next      = NULL;
        table->capacity  = 32;
        table->count     = 0;
        table->free_head = -1;

        *(TlsTable **)vitasdk_get_tls_data(0) = table;

        index                       = (int)table->count++;
        table->values[index].handle = handle;
    } else {
        // 表里已有内容才需要查找
        for (unsigned int i = 0; i < table->count; i++) {
            if (table->values[i].handle == handle) {
                index = (int)i;
                break;
            }
        }

        if (index < 0) {
            index = take_free_slot(table);
            if (index < 0) {
                // 没有回收槽，才主动清一次失效条目
                sweep_invalid_slots(table);
                index = take_free_slot(table);
            }

            if (index < 0) {
                if (table->count >= table->capacity) {  // 需要扩容
                    unsigned int old_capacity = table->capacity;
                    unsigned int new_capacity = old_capacity * 2;

                    table = realloc(table, sizeof(TlsTable) + new_capacity * sizeof(TlsValue));
                    if (!table) {
                        return TLS_NO_RESOURCES;
                    }

                    table->capacity = new_capacity;
                    memset(table->values + old_capacity, 0, (new_capacity - old_capacity) * sizeof(TlsValue));
                    *(TlsTable **)vitasdk_get_tls_data(0) = table;
                }

                index = (int)table->count++;
            }

            table->values[index].handle = handle;
        }
    }

    table->values[index].value = value;
    return TLS_OK;
}

void *__wrap_pte_osTlsGetValue(unsigned int handle) {
    // 先确认键仍然有效（已被删除或复用给别的键时，这里返回 NULL）
    lock_keys();
    int valid = (get_key_index(s_key_table, handle) >= 0);
    unlock_keys();
    if (!valid) {
        return NULL;
    }

    const TlsTable *table = *(TlsTable **)vitasdk_get_tls_data(0);
    if (!table) {
        return NULL;
    }

    for (unsigned int i = 0; i < table->count; i++) {
        if (table->values[i].handle == handle) {
            return table->values[i].value;
        }
    }

    return NULL;
}
