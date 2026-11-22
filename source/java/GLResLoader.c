/*
 * com/gameloft/android/TBFV/GloftELHP/ML/GLResLoader
 *
 * 引擎按相对路径找资源（"data/3d/..."、"data/script/..."），Java 侧先查
 * Resources/AssetManager，再退到 /sdcard/gameloft/games/GloftELHP/ 下的真实
 * 文件。移植时前两条都不存在，直接读 GAME_DATA_DIR。
 */
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>

#include <so_common/so_logger.h>
#include <so_java/java/lang/Array.h>
#include <so_java/java/lang/Class.h>
#include <so_java/java/lang/String.h>

#include "GLResLoader.h"
#include "config.h"
#include "utils/paths.h"
#include "utils/perf.h"

static JClass sClass = NULL;

static void (*native_Init)(JNIEnv *env, jclass clazz, jint flag);

/* Only immutable packaged resources use this cache, never save files. Holding
 * the lock through pread prevents eviction while another thread uses a slot.
 * Positional reads avoid sharing a mutable file cursor between requests. */
#define RESOURCE_CACHE_SIZE 4
typedef struct { char path[PATH_MAPPED_MAX]; int fd; SceOff size; uint64_t age; } ResourceFile;
static ResourceFile sFiles[RESOURCE_CACHE_SIZE];
static uint64_t sFileAge;
static pthread_mutex_t sFileLock = PTHREAD_MUTEX_INITIALIZER;

static void resource_close(ResourceFile *file) {
    if (file->path[0]) sceIoClose(file->fd);
    memset(file, 0, sizeof(*file));
}

void GLResLoader_close(void) {
    pthread_mutex_lock(&sFileLock);
    for (int i = 0; i < RESOURCE_CACHE_SIZE; ++i) resource_close(&sFiles[i]);
    pthread_mutex_unlock(&sFileLock);
}

static ResourceFile *resource_open(const char *path, int *hit, ResourceFile *temporary) {
    ResourceFile *oldest = &sFiles[0];
    *hit = 0;
    int cacheable = !strncmp(path, GAME_DATA_DIR, sizeof(GAME_DATA_DIR) - 1) && !strstr(path, "/../");
    for (int i = 0; cacheable && i < RESOURCE_CACHE_SIZE; ++i) {
        ResourceFile *f = &sFiles[i];
        if (f->path[0] && !strcmp(f->path, path)) {
            *hit = 1; f->age = ++sFileAge; return f;
        }
        if (!f->path[0] || (oldest->path[0] && f->age < oldest->age)) oldest = f;
    }
    if (!cacheable) oldest = temporary;
    int fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) { errno = ENOENT; return NULL; }
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (size < 0 || size > INT_MAX - 64) {
        sceIoClose(fd); errno = size < 0 ? EIO : EFBIG; return NULL;
    }
    resource_close(oldest);
    snprintf(oldest->path, sizeof(oldest->path), "%s", path);
    oldest->fd = fd; oldest->size = size; oldest->age = ++sFileAge;
    return oldest;
}

static JObject resource_request(JString name, JInt offset, JInt length, PerfOp op, JInt *size_out) {
    uint64_t start = perf_now_us();
    char buf[PATH_MAPPED_MAX];
    const char *path = name ? String_getUTFChars(name) : NULL;
    path = paths_from_android(path, buf, sizeof(buf));
    JObject array = NULL;
    size_t completed = 0;
    int hit = 0, ok = 0;
    if (size_out) *size_out = 0;
    if (!path || !*path || offset < 0 || (op == PERF_RES_BYTES && length <= 0)) {
        if (path) errno = EINVAL;
        perf_record(op, path, start, 0, 0, 0); return NULL;
    }
    pthread_mutex_lock(&sFileLock);
    ResourceFile temporary = {0};
    ResourceFile *file = resource_open(path, &hit, &temporary);
    if (!file) goto done;
    if (op == PERF_RES_LENGTH) { *size_out = (JInt)file->size; ok = 1; goto done; }
    if (op == PERF_RES_FULL) length = (JInt)file->size;
    if (length <= 0 || length > INT_MAX - 64 || (SceOff)offset + length > file->size) {
        errno = EINVAL; goto done;
    }
    array = Array_new(JTYPE_BYTE, length);
    if (!array) { errno = ENOMEM; goto done; }
    // Array_new returns the data pointer, not a JNI handle. JNI registers the
    // returned object in JniCallCommonMethodV after this Java method returns.
    while (completed < (size_t)length) {
        int n = sceIoPread(file->fd, (char *)array + completed, (SceSize)(length - completed), offset + completed);
        if (n <= 0 || (size_t)n > (size_t)length - completed) {
            errno = EIO;
            Object_release(array); array = NULL;
            resource_close(file); // A failed descriptor must not stay cached.
            goto done;
        }
        completed += n;
    }
    ok = 1;
done:
    resource_close(&temporary);
    pthread_mutex_unlock(&sFileLock);
    perf_record(op, path, start, completed, ok, hit);
    return array;
}

static JInt GLResLoader_getResourceLength(JString path) {
    JInt size;
    resource_request(path, 0, 0, PERF_RES_LENGTH, &size);
    return size;
}

static JObject GLResLoader_getResourceFull(JString path) {
    return resource_request(path, 0, 0, PERF_RES_FULL, NULL);
}

static JObject GLResLoader_getResourceBytes(JString path, JInt offset, JInt length) {
    return resource_request(path, offset, length, PERF_RES_BYTES, NULL);
}

static void GLResLoader_nativeInit(jint flag) {
    if (!native_Init) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Init(&_jni_env, JniNewLocalRefFromJava(sClass), flag);
    EXIT_JNI_CALL(NULL)
}

void GLResLoader_init(void) {
    GLResLoader_nativeInit(0);
}

static JSymbol class_symbols[] = {
    JAVA_SYMBOL_STATIC_NATIVE("nativeInit", "(I)V", &native_Init),

    JAVA_SYMBOL_STATIC_METHOD("getResourceLength", "(Ljava/lang/String;)I", &GLResLoader_getResourceLength),
    JAVA_SYMBOL_STATIC_METHOD("getResourceFull", "(Ljava/lang/String;)[B", &GLResLoader_getResourceFull),
    JAVA_SYMBOL_STATIC_METHOD("getResourceBytes", "(Ljava/lang/String;II)[B", &GLResLoader_getResourceBytes),
};

static JClassMeta class_meta = {
    .name          = "com/gameloft/android/TBFV/GloftELHP/ML/GLResLoader",
    .super_name    = "java/lang/Object",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = 0,  // 纯静态
    .dtor          = NULL,
};

void setup_GLResLoader(so_module *mod) {
    sClass = JavaCreateClass(&class_meta);
    if (!sClass) {
        so_loge("[JAVA] GLResLoader: create class failed");
        return;
    }

    native_Init = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GLResLoader_nativeInit");
}
