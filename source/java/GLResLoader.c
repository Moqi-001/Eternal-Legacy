/*
 * com/gameloft/android/TBFV/GloftELHP/ML/GLResLoader
 *
 * 引擎按相对路径找资源（"data/3d/..."、"data/script/..."），Java 侧先查
 * Resources/AssetManager，再退到 /sdcard/gameloft/games/GloftELHP/ 下的真实
 * 文件。移植时前两条都不存在，直接读 GAME_DATA_DIR。
 */
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>

#include <so_common/so_logger.h>
#include <so_java/java/lang/Array.h>
#include <so_java/java/lang/Class.h>
#include <so_java/java/lang/String.h>

#include "GLResLoader.h"
#include "config.h"

static JClass sClass = NULL;

static void (*native_Init)(JNIEnv *env, jclass clazz, jint flag);

// "data/xxx" 与 "./data/xxx" 都落到同一个文件
static void GLResLoader_buildPath(const char *rel, char *out, size_t out_size) {
    while (rel[0] == '.' && rel[1] == '/') {
        rel += 2;
    }

    snprintf(out, out_size, "%s%s", GAME_DATA_DIR, rel);
}

static int GLResLoader_open(const char *rel) {
    char path[512];
    GLResLoader_buildPath(rel, path, sizeof(path));
    return sceIoOpen(path, SCE_O_RDONLY, 0777);
}

static JInt GLResLoader_getResourceLength(JString path) {
    if (!path) {
        return 0;
    }

    int fd = GLResLoader_open(String_getUTFChars(path));
    if (fd < 0) {
        return 0;
    }

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoClose(fd);

    return (JInt)(size > 0 ? size : 0);
}

static JObject GLResLoader_getResourceFull(JString path) {
    if (!path) {
        return NULL;
    }

    int fd = GLResLoader_open(String_getUTFChars(path));
    if (fd < 0) {
        return NULL;
    }

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (size <= 0) {
        sceIoClose(fd);
        return NULL;
    }

    sceIoLseek(fd, 0, SCE_SEEK_SET);

    JObject array = Array_new(JTYPE_BYTE, (JInt)size);
    if (!array) {
        sceIoClose(fd);
        return NULL;
    }

    // 直接读进数组的数据区，省一次中转
    jbyte *data = JniGetByteArrayElements(array, NULL);
    if (!data) {
        sceIoClose(fd);
        return NULL;
    }

    sceIoRead(fd, data, (SceSize)size);
    sceIoClose(fd);

    return array;
}

static JObject GLResLoader_getResourceBytes(JString path, JInt offset, JInt length) {
    if (!path || length <= 0) {
        return NULL;
    }

    int fd = GLResLoader_open(String_getUTFChars(path));
    if (fd < 0) {
        return NULL;
    }

    sceIoLseek(fd, offset, SCE_SEEK_SET);

    JObject array = Array_new(JTYPE_BYTE, length);
    if (!array) {
        sceIoClose(fd);
        return NULL;
    }

    jbyte *data = JniGetByteArrayElements(array, NULL);
    if (!data) {
        sceIoClose(fd);
        return NULL;
    }

    int read = sceIoRead(fd, data, (SceSize)length);
    sceIoClose(fd);

    if (read != length) {
        so_loge("[JAVA] getResourceBytes('%s', %d, %d): read %d bytes",
                String_getUTFChars(path), offset, length, read);
    }

    return array;
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
