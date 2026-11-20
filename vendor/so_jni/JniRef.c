#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <so_common/so_logger.h>

#include "so_jni_internal.h"

#define MAX_REF_CAPACITY  0X40000u
#define INIT_REF_CAPACITY 512

#define LOCAL_REF_BIAS  0X10000000u
#define GLOBAL_REF_BIAS 0X20000000u
#define WEAK_REF_BIAS   0X30000000u

typedef struct JniRefSlot {
    JObject obj;  //  Java 对象指针
} JniRefSlot;

typedef struct JniRefTable {
    JniRefSlot *slots;          // 槽位数组
    jint        capacity;       // 槽位容量
    jint        count;          // 已分配到的索引上界（高水位，PopLocalFrame 回退用）
    jint        recycled_head;  // 当前作用域回收的槽位链表头（-1 表示没有）
    jint       *next_recycled;  // 回收链表：next_recycled[i] = 下一个回收槽位索引
} JniRefTable;

typedef struct JniRefFrame {
    uint32_t            start;          // 帧索引起始
    uint32_t            capacity;       // 帧引用容量
    jint                recycled_head;  // 本帧回收的槽位链表头（-1 表示没有）
    struct JniRefFrame *prev;           // 前一帧
} JniRefFrame;

typedef struct JniLocalContext {
    JniRefTable  table;  // 本线程的槽位表
    JniRefFrame *frame;  // 帧栈
} JniLocalContext;

typedef struct JniGlobalContext {
    JniRefTable     table;  // 进程级的槽位表
    pthread_mutex_t lock;   // 线程锁
} JniGlobalContext;

/*
 * local 上下文的生命周期 = 线程的生命周期。
 * 每个线程一份上下文，线程退出时由 pthread key 的析构回调释放。
 *
 * global / weak 上下文是进程级，需手动释放。
 */
static __thread JniLocalContext *_so_local_ctx  = NULL;
static JniGlobalContext         *_so_global_ctx = NULL;
static JniGlobalContext         *_so_weak_ctx   = NULL;

static pthread_key_t  _so_local_ctx_key;
static pthread_once_t _so_local_ctx_key_once  = PTHREAD_ONCE_INIT;
static int            _so_local_ctx_key_ready = 0;

/*
 * ============================================================================
 * 槽位表公共操作
 * ============================================================================
 */

static jint _ref_table_init(JniRefTable *table) {
    table->slots = calloc(INIT_REF_CAPACITY, sizeof(JniRefSlot));
    if (!table->slots) {
        return -1;
    }

    table->next_recycled = malloc(INIT_REF_CAPACITY * sizeof(jint));
    if (!table->next_recycled) {
        free(table->slots);
        table->slots = NULL;
        return -1;
    }

    table->capacity      = INIT_REF_CAPACITY;
    table->count         = 0;
    table->recycled_head = -1;

    return 0;
}

// 释放表里所有引用并回收数组
static void _ref_table_free(JniRefTable *table, int is_weak) {
    if (table->slots) {
        for (jint i = 0; i < table->count; i++) {
            if (!table->slots[i].obj) {
                continue;
            }
            if (is_weak) {  // weak 引用用不同的释放函数
                Object_releaseWeak(table->slots[i].obj, &table->slots[i].obj);
            } else {
                Object_release(table->slots[i].obj);
            }
        }

        free(table->slots);
        table->slots = NULL;
    }
    if (table->next_recycled) {
        free(table->next_recycled);
        table->next_recycled = NULL;
    }

    table->capacity      = 0;
    table->count         = 0;
    table->recycled_head = -1;
}

// 取一个可用槽位：优先用回收链上的，没有再尾部取（必要时扩容）；-1 表示取不到
// recycled_head 指向当前作用域的链表头（local 有帧时传帧的，其余传表自己的）
static jint _ref_table_get_recycled(JniRefTable *table, jint *recycled_head) {
    if (*recycled_head >= 0) {  // 有空回收槽
        jint index     = *recycled_head;
        *recycled_head = table->next_recycled[index];
        return index;
    }

    // 需扩容
    if (table->count >= table->capacity) {
        if (table->capacity >= MAX_REF_CAPACITY) {
            return -1;
        }

        jint new_capacity = table->capacity * 2;
        if (new_capacity > MAX_REF_CAPACITY) {
            new_capacity = MAX_REF_CAPACITY;
        }

        JniRefSlot *new_slots = realloc(table->slots, new_capacity * sizeof(JniRefSlot));
        if (!new_slots) {
            return -1;
        }
        table->slots = new_slots;

        jint *new_next_recycled = realloc(table->next_recycled, new_capacity * sizeof(jint));
        if (!new_next_recycled) {
            return -1;
        }
        table->next_recycled = new_next_recycled;
        table->capacity      = new_capacity;
    }

    return table->count++;
}

// 回收槽位进表：先写 next 再改头
static void _ref_table_recycle(JniRefTable *table, jint index, jint *recycled_head) {
    table->next_recycled[index] = *recycled_head;
    *recycled_head              = index;
}

/*
 * ============================================================================
 * 引用类型 / 解引用
 * ============================================================================
 */
jobjectRefType JniGetObjectRefType(jobject object) {
    if (!object) {
        return JNIInvalidRefType;
    }

    uintptr_t addr = (uintptr_t)object;
    if (addr >= LOCAL_REF_BIAS && addr < LOCAL_REF_BIAS + MAX_REF_CAPACITY) {
        return JNILocalRefType;
    }
    if (addr >= GLOBAL_REF_BIAS && addr < GLOBAL_REF_BIAS + MAX_REF_CAPACITY) {
        return JNIGlobalRefType;
    }
    if (addr >= WEAK_REF_BIAS && addr < WEAK_REF_BIAS + MAX_REF_CAPACITY) {
        return JNIWeakGlobalRefType;
    }

    return JNIInvalidRefType;
}

JObject JniObjectToJava(jobject object) {
    if (!object) {
        return NULL;
    }

    uintptr_t addr = (uintptr_t)object;

    if (addr >= LOCAL_REF_BIAS && addr < LOCAL_REF_BIAS + MAX_REF_CAPACITY) {
        JniLocalContext *ctx   = _so_local_ctx;
        jint             index = addr - LOCAL_REF_BIAS;
        if (!ctx || !ctx->table.slots || index >= ctx->table.count) {
            return NULL;
        }
        return ctx->table.slots[index].obj;
    }
    if (addr >= GLOBAL_REF_BIAS && addr < GLOBAL_REF_BIAS + MAX_REF_CAPACITY) {
        JniGlobalContext *ctx   = _so_global_ctx;
        jint              index = addr - GLOBAL_REF_BIAS;
        if (!ctx || index >= ctx->table.count) {
            return NULL;
        }
        return ctx->table.slots[index].obj;
    }
    if (addr >= WEAK_REF_BIAS && addr < WEAK_REF_BIAS + MAX_REF_CAPACITY) {
        JniGlobalContext *ctx   = _so_weak_ctx;
        jint              index = addr - WEAK_REF_BIAS;
        if (!ctx || index >= ctx->table.count) {
            return NULL;
        }
        return ctx->table.slots[index].obj;
    }

    return NULL;
}

/*
 * ============================================================================
 * local 引用
 * ============================================================================
 */
// 获取 local 上下文
static JniLocalContext *local_ctx_get(void) {
    if (!_so_local_ctx && JniInitLocalRefContext() != JNI_OK) {
        return NULL;
    }

    return _so_local_ctx;
}

jobject JniNewLocalRefFromJava(JObject obj) {
    if (!obj) {
        return NULL;
    }

    JniLocalContext *ctx = local_ctx_get();
    if (!ctx) {
        so_loge("[JNI] JniNewLocalRefFromJava(%p): local ref context is NULL!", obj);
        return NULL;
    }

    JniRefFrame *frame = ctx->frame;
    if (frame && ctx->table.count >= frame->start + frame->capacity) {
        so_loge("[JNI] JniNewLocalRefFromJava(%p): frame capacity limit exceeded!", obj);
        return NULL;  // 帧容量已满
    }

    // 只从当前帧回收的槽位里取，不能用其它帧的槽位，
    // 否则它落在 [frame->start, count) 之外，PopLocalFrame 释放不到
    jint *recycled_head = frame ? &frame->recycled_head : &ctx->table.recycled_head;

    jint index = _ref_table_get_recycled(&ctx->table, recycled_head);
    if (index < 0) {
        so_loge("[JNI] JniNewLocalRefFromJava(%p): no slot available!", obj);
        return NULL;
    }

    ctx->table.slots[index].obj = Object_retain(obj);

    return (jobject)(index + LOCAL_REF_BIAS);
}

jobject JniNewLocalRef(jobject object) {
    return JniNewLocalRefFromJava(JniObjectToJava(object));
}

void JniDeleteLocalRef(jobject object) {
    JniLocalContext *ctx = _so_local_ctx;
    if (!ctx || !ctx->table.slots) {
        return;
    }

    jint      index = -1;
    uintptr_t addr  = (uintptr_t)object;
    if (addr >= LOCAL_REF_BIAS && addr < LOCAL_REF_BIAS + MAX_REF_CAPACITY) {
        index = addr - LOCAL_REF_BIAS;
    }
    if (index < 0 || index >= ctx->table.count || !ctx->table.slots[index].obj) {
        return;
    }

    Object_release(ctx->table.slots[index].obj);
    ctx->table.slots[index].obj = NULL;

    // 查找该槽位所属帧：从当前帧开始找，如果 index >= 此帧的 start 说明它属于此帧，
    // 否则继续往上一帧找。
    JniRefFrame *scope = ctx->frame;
    while (scope && index < (jint)scope->start) {
        scope = scope->prev;
    }

    jint *recycled_head = scope ? &scope->recycled_head : &ctx->table.recycled_head;
    _ref_table_recycle(&ctx->table, index, recycled_head);
}

jint JniPushLocalFrame(jint capacity) {
    JniLocalContext *ctx = local_ctx_get();
    if (!ctx) {
        return JNI_ERR;
    }

    if (capacity <= 0) {
        capacity = MAX_REF_CAPACITY - ctx->table.count;
    } else if (capacity > MAX_REF_CAPACITY - ctx->table.count) {  // 超出最大容量
        return JNI_ERR;
    }

    JniRefFrame *frame = (JniRefFrame *)malloc(sizeof(JniRefFrame));
    if (!frame) {
        return JNI_ERR;
    }

    frame->start         = ctx->table.count;
    frame->capacity      = capacity;
    frame->recycled_head = -1;
    frame->prev          = ctx->frame;
    ctx->frame           = frame;

    return JNI_OK;
}

jobject JniPopLocalFrame(jobject result) {
    JniLocalContext *ctx = _so_local_ctx;
    if (!ctx || !ctx->table.slots || !ctx->frame) {
        return NULL;
    }

    JniRefSlot  *slots = ctx->table.slots;
    JniRefFrame *frame = ctx->frame;
    ctx->frame         = frame->prev;

    // 需对 Java 对象进行引用保护以防止弹帧时被释放
    JObject obj = JniObjectToJava(result);
    if (obj) {
        Object_retain(obj);
    }

    // 释放帧内所有引用， [start, count) 就是本帧用过的全部槽位。
    for (jint i = frame->start; i < ctx->table.count; i++) {
        if (slots[i].obj) {
            Object_release(slots[i].obj);
            slots[i].obj = NULL;
        }
    }

    ctx->table.count = frame->start;  // 回退 count (max_index)

    // 切回上一帧且必须在释放当前帧后，才能将 result 注册到上一帧
    if (obj) {
        result = JniNewLocalRefFromJava(obj);
        Object_release(obj);
    } else {
        result = NULL;
    }

    free(frame);  // 释放 frame

    return result;
}

static void local_ctx_release(JniLocalContext *ctx) {
    if (!ctx) {
        return;
    }

    JniRefFrame *frame = ctx->frame;
    while (frame) {
        JniRefFrame *prev = frame->prev;
        free(frame);
        frame = prev;
    }

    _ref_table_free(&ctx->table, 0);
    free(ctx);
}

// 线程退出时由 pthread key 析构回调释放
static void local_ctx_key_dtor(void *value) {
    local_ctx_release((JniLocalContext *)value);
    so_logd("[JNI] local ref context released on thread exit");
}

static void local_ctx_key_init(void) {
    if (pthread_key_create(&_so_local_ctx_key, local_ctx_key_dtor) != 0) {
        so_loge("[JNI] pthread_key_create() failed, local refs will not be released on thread exit");
        return;
    }
    _so_local_ctx_key_ready = 1;
}

jint JniInitLocalRefContext(void) {
    if (_so_local_ctx) {
        return JNI_OK;
    }

    JniLocalContext *ctx = calloc(1, sizeof(JniLocalContext));
    if (!ctx) {
        return JNI_ERR;
    }

    if (_ref_table_init(&ctx->table) != 0) {
        free(ctx);
        return JNI_ERR;
    }

    ctx->frame = NULL;

    _so_local_ctx = ctx;

    // 登记到线程，等线程结束自动释放
    pthread_once(&_so_local_ctx_key_once, local_ctx_key_init);
    if (_so_local_ctx_key_ready) {
        pthread_setspecific(_so_local_ctx_key, ctx);
    }

    so_logd("[JNI] local ref context created for thread %p", (void *)pthread_self());
    return JNI_OK;
}

void JniFiniLocalRefContext(void) {
    JniLocalContext *ctx = _so_local_ctx;
    if (!ctx) {
        return;
    }
    _so_local_ctx = NULL;

    // 上下文已释放：将key置 NULL 防止线程退出时二次释放
    if (_so_local_ctx_key_ready) {
        pthread_setspecific(_so_local_ctx_key, NULL);
    }

    local_ctx_release(ctx);

    so_logd("[JNI] JniFiniLocalRefContext()");
}

/*
 * ============================================================================
 * global 引用
 * ============================================================================
 */
jobject JniNewGlobalRefFromJava(JObject obj) {
    if (!obj) {
        return NULL;
    }

    JniGlobalContext *ctx = _so_global_ctx;
    if (!ctx) {
        return NULL;
    }

    pthread_mutex_lock(&ctx->lock);

    jint index = _ref_table_get_recycled(&ctx->table, &ctx->table.recycled_head);
    if (index < 0) {
        pthread_mutex_unlock(&ctx->lock);
        return NULL;
    }

    ctx->table.slots[index].obj = Object_retain(obj);

    pthread_mutex_unlock(&ctx->lock);

    return (jobject)(index + GLOBAL_REF_BIAS);
}

jobject JniNewGlobalRef(jobject object) {
    return JniNewGlobalRefFromJava(JniObjectToJava(object));
}

void JniDeleteGlobalRef(jobject object) {
    JniGlobalContext *ctx = _so_global_ctx;
    if (!object || !ctx) {
        return;
    }

    pthread_mutex_lock(&ctx->lock);

    jint      index = -1;
    uintptr_t addr  = (uintptr_t)object;
    if (addr >= GLOBAL_REF_BIAS && addr < GLOBAL_REF_BIAS + MAX_REF_CAPACITY) {
        index = addr - GLOBAL_REF_BIAS;
    }
    if (index < 0 || index >= ctx->table.count || !ctx->table.slots[index].obj) {
        pthread_mutex_unlock(&ctx->lock);
        return;
    }

    Object_release(ctx->table.slots[index].obj);
    ctx->table.slots[index].obj = NULL;

    _ref_table_recycle(&ctx->table, index, &ctx->table.recycled_head);

    pthread_mutex_unlock(&ctx->lock);
}

/*
 * ============================================================================
 * weak global 引用
 * ============================================================================
 */
jweak JniNewWeakGlobalRefFromJava(JObject obj) {
    if (!obj) {
        return NULL;
    }

    JniGlobalContext *ctx = _so_weak_ctx;
    if (!ctx) {
        return NULL;
    }

    pthread_mutex_lock(&ctx->lock);

    jint index = _ref_table_get_recycled(&ctx->table, &ctx->table.recycled_head);
    if (index < 0) {
        pthread_mutex_unlock(&ctx->lock);
        return NULL;
    }

    if (Object_retainWeak(obj, &ctx->table.slots[index].obj) != JNI_OK) {
        // 引用没建立起来，把槽位还回去，避免留下一个空洞
        _ref_table_recycle(&ctx->table, index, &ctx->table.recycled_head);
        pthread_mutex_unlock(&ctx->lock);
        return NULL;
    }
    ctx->table.slots[index].obj = obj;

    pthread_mutex_unlock(&ctx->lock);

    return (jweak)(index + WEAK_REF_BIAS);
}

jobject JniNewWeakGlobalRef(jobject object) {
    return JniNewWeakGlobalRefFromJava(JniObjectToJava(object));
}

void JniDeleteWeakGlobalRef(jweak object) {
    JniGlobalContext *ctx = _so_weak_ctx;
    if (!object || !ctx) {
        return;
    }

    pthread_mutex_lock(&ctx->lock);

    jint      index = -1;
    uintptr_t addr  = (uintptr_t)object;
    if (addr >= WEAK_REF_BIAS && addr < WEAK_REF_BIAS + MAX_REF_CAPACITY) {
        index = addr - WEAK_REF_BIAS;
    }
    if (index < 0 || index >= ctx->table.count || !ctx->table.slots[index].obj) {
        pthread_mutex_unlock(&ctx->lock);
        return;
    }

    Object_releaseWeak(ctx->table.slots[index].obj, &ctx->table.slots[index].obj);
    ctx->table.slots[index].obj = NULL;

    _ref_table_recycle(&ctx->table, index, &ctx->table.recycled_head);

    pthread_mutex_unlock(&ctx->lock);
}

/*
 * ============================================================================
 * global / weak 上下文的初始化与销毁（进程级）
 * ============================================================================
 */
jint JniInitGlobalRefContext(void) {
    if (_so_global_ctx) {
        return JNI_OK;
    }

    JniGlobalContext *ctx = calloc(1, sizeof(JniGlobalContext));
    if (!ctx) {
        return JNI_ERR;
    }

    if (_ref_table_init(&ctx->table) != 0) {
        free(ctx);
        return JNI_ERR;
    }

    pthread_mutex_init(&ctx->lock, NULL);

    _so_global_ctx = ctx;

    so_logd("[JNI] JniInitGlobalRefContext(): success!");
    return JNI_OK;
}

void JniFiniGlobalRefContext(void) {
    JniGlobalContext *ctx = _so_global_ctx;
    if (!ctx) {
        return;
    }
    _so_global_ctx = NULL;

    pthread_mutex_lock(&ctx->lock);
    _ref_table_free(&ctx->table, 0);
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    so_logd("[JNI] JniFiniGlobalRefContext()");
}

jint JniInitWeakGlobalRefContext(void) {
    if (_so_weak_ctx) {
        return JNI_OK;
    }

    JniGlobalContext *ctx = calloc(1, sizeof(JniGlobalContext));
    if (!ctx) {
        return JNI_ERR;
    }

    if (_ref_table_init(&ctx->table) != 0) {
        free(ctx);
        return JNI_ERR;
    }

    pthread_mutex_init(&ctx->lock, NULL);

    _so_weak_ctx = ctx;

    so_logd("[JNI] JniInitWeakGlobalRefContext(): success!");
    return JNI_OK;
}

void JniFiniWeakGlobalRefContext(void) {
    JniGlobalContext *ctx = _so_weak_ctx;
    if (!ctx) {
        return;
    }
    _so_weak_ctx = NULL;

    pthread_mutex_lock(&ctx->lock);
    _ref_table_free(&ctx->table, 1);
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    so_logd("[JNI] JniFiniWeakGlobalRefContext()");
}
