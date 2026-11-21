/*
 * com/gameloft/android/TBFV/GloftELHP/ML/GameRenderer
 *
 * Android 上 GLSurfaceView 的 GL 线程按 onSurfaceCreated → onSurfaceChanged →
 * 每帧 onDrawFrame 的顺序调这里，宿主扮演这个线程。
 */
#include <so_common/so_logger.h>
#include <so_java/java/lang/Class.h>
#include <so_java/java/lang/Object.h>

#include "Device.h"
#include "EternalLegacy.h"
#include "GLMediaPlayer.h"
#include "GLResLoader.h"
#include "GameRenderer.h"
#include "config.h"

struct _GameRenderer {
    JInt placeholder;
};

static JClass  sClass    = NULL;
static JObject sInstance = NULL;
static JObject sContext  = NULL;  // Java 侧的 GameRenderer.a

static void (*native_GetJNIEnv)(JNIEnv *env, jobject obj);
static void (*native_Init)(JNIEnv *env, jclass clazz, jint flag);
static void (*native_Render)(JNIEnv *env, jclass clazz);
static void (*native_Resize)(JNIEnv *env, jclass clazz, jint width, jint height);
static void (*native_Done)(JNIEnv *env, jclass clazz);

static void GameRenderer_nativeGetJNIEnv(void) {
    if (!native_GetJNIEnv) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_GetJNIEnv(&_jni_env, JniNewLocalRefFromJava(sInstance));
    EXIT_JNI_CALL(NULL)
}

static void GameRenderer_nativeInit(jint flag) {
    if (!native_Init) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Init(&_jni_env, JniNewLocalRefFromJava(sClass), flag);
    EXIT_JNI_CALL(NULL)
}

static void GameRenderer_nativeResize(jint width, jint height) {
    if (!native_Resize) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Resize(&_jni_env, JniNewLocalRefFromJava(sClass), width, height);
    EXIT_JNI_CALL(NULL)
}

static void GameRenderer_nativeRender(void) {
    if (!native_Render) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Render(&_jni_env, JniNewLocalRefFromJava(sClass));
    EXIT_JNI_CALL(NULL)
}

static void GameRenderer_nativeDone(void) {
    if (!native_Done) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Done(&_jni_env, JniNewLocalRefFromJava(sClass));
    EXIT_JNI_CALL(NULL)
}

void GameRenderer_onSurfaceCreated(void) {
    so_logi("[JAVA] GameRenderer.onSurfaceCreated");

    // 顺序照抄 GameRenderer.onSurfaceCreated：先取 JNIEnv 与资源/音频模块，
    // 再进两个 nativeInit
    GameRenderer_nativeGetJNIEnv();
    GLResLoader_init();
    GLMediaPlayer_init();
    GameRenderer_nativeInit(1);
    EternalLegacy_nativeInit(1);
}

void GameRenderer_onSurfaceChanged(int width, int height) {
    width = GAME_LOGICAL_W;
    height = GAME_LOGICAL_H;
    so_logi("[JAVA] GameRenderer.onSurfaceChanged(%d, %d)", width, height);
    GameRenderer_nativeResize(width, height);
}

void GameRenderer_onDrawFrame(void) {
    GameRenderer_nativeRender();
}

void GameRenderer_onSurfaceDestroyed(void) {
    GameRenderer_nativeDone();
}

static JSymbol class_symbols[] = {
    JAVA_SYMBOL_STATIC_NATIVE("nativeInit", "(I)V", &native_Init),
    JAVA_SYMBOL_STATIC_NATIVE("nativeRender", "()V", &native_Render),
    JAVA_SYMBOL_STATIC_NATIVE("nativeResize", "(II)V", &native_Resize),
    JAVA_SYMBOL_STATIC_NATIVE("nativeDone", "()V", &native_Done),
    JAVA_SYMBOL_NATIVE("nativeGetJNIEnv", "()V", &native_GetJNIEnv),

    JAVA_SYMBOL_STATIC_FIELD("a", "Landroid/content/Context;", &sContext),
};

static JClassMeta class_meta = {
    .name          = "com/gameloft/android/TBFV/GloftELHP/ML/GameRenderer",
    .super_name    = "java/lang/Object",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = sizeof(struct _GameRenderer),
    .dtor          = NULL,
};

void setup_GameRenderer(so_module *mod) {
    sClass = JavaCreateClass(&class_meta);
    if (!sClass) {
        so_loge("[JAVA] GameRenderer: create class failed");
        return;
    }

    sInstance = Object_new(sClass);
    sContext  = EternalLegacy_activity();  // Java 侧 GameRenderer.a 指向 Activity

    native_GetJNIEnv = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameRenderer_nativeGetJNIEnv");
    native_Init      = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameRenderer_nativeInit");
    native_Render    = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameRenderer_nativeRender");
    native_Resize    = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameRenderer_nativeResize");
    native_Done      = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameRenderer_nativeDone");
}
