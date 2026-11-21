/*
 * com/gameloft/android/TBFV/GloftELHP/ML/GameGLSurfaceView
 *
 * 视图本身在移植里没有实体，但引擎要两个数：onSizeChanged 报上来的绘制区
 * 尺寸，以及获得焦点时触发的 nativeResume。这两个都由宿主转进来。
 */
#include <so_common/so_logger.h>
#include <so_java/java/lang/Class.h>
#include <so_java/java/lang/Object.h>

#include "GameGLSurfaceView.h"
#include "config.h"

struct _GameGLSurfaceView {
    JInt placeholder;
};

static JClass  sClass    = NULL;
static JObject sInstance = NULL;
static JInt    sWidth    = GAME_LOGICAL_W;
static JInt    sHeight   = GAME_LOGICAL_H;

static void (*native_OnSize)(JNIEnv *env, jclass clazz, jint width, jint height);
static void (*native_OnTouch)(JNIEnv *env, jclass clazz, jint type, jint x, jint y, jint pointer);
static void (*native_Pause)(JNIEnv *env, jobject obj);
static void (*native_Resume)(JNIEnv *env, jobject obj);

int GameGLSurfaceView_getWidth(void) {
    return sWidth;
}

int GameGLSurfaceView_getHeight(void) {
    return sHeight;
}

void GameGLSurfaceView_onSizeChanged(int width, int height) {
    // Android sees one complete 3:2 canvas; presentation maps it to Vita pixels.
    width = GAME_LOGICAL_W;
    height = GAME_LOGICAL_H;
    sWidth  = width;
    sHeight = height;

    if (!native_OnSize) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_OnSize(&_jni_env, JniNewLocalRefFromJava(sClass), width, height);
    EXIT_JNI_CALL(NULL)
}

void GameGLSurfaceView_onTouch(int type, int x, int y, int pointer) {
    if (!native_OnTouch) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_OnTouch(&_jni_env, JniNewLocalRefFromJava(sClass), type, x, y, pointer);
    EXIT_JNI_CALL(NULL)
}

void GameGLSurfaceView_onWindowFocusChanged(void) {
    if (!native_Resume) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Resume(&_jni_env, JniNewLocalRefFromJava(sInstance));
    EXIT_JNI_CALL(NULL)
}

static void GameGLSurfaceView_onPause(void) {
    if (!native_Pause) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Pause(&_jni_env, JniNewLocalRefFromJava(sInstance));
    EXIT_JNI_CALL(NULL)
}

static JSymbol class_symbols[] = {
    JAVA_SYMBOL_STATIC_NATIVE("nativeOnSize", "(II)V", &native_OnSize),
    JAVA_SYMBOL_STATIC_NATIVE("nativeOnTouch", "(IIII)V", &native_OnTouch),
    JAVA_SYMBOL_NATIVE("nativePause", "()V", &native_Pause),
    JAVA_SYMBOL_NATIVE("nativeResume", "()V", &native_Resume),

    JAVA_SYMBOL_STATIC_FIELD("a", "I", &sWidth),
    JAVA_SYMBOL_STATIC_FIELD("b", "I", &sHeight),
};

static JClassMeta class_meta = {
    .name          = "com/gameloft/android/TBFV/GloftELHP/ML/GameGLSurfaceView",
    .super_name    = "java/lang/Object",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = sizeof(struct _GameGLSurfaceView),
    .dtor          = NULL,
};

void setup_GameGLSurfaceView(so_module *mod) {
    sClass = JavaCreateClass(&class_meta);
    if (!sClass) {
        so_loge("[JAVA] GameGLSurfaceView: create class failed");
        return;
    }

    sInstance = Object_new(sClass);

    native_OnSize  = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameGLSurfaceView_nativeOnSize");
    native_OnTouch = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameGLSurfaceView_nativeOnTouch");
    native_Pause   = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameGLSurfaceView_nativePause");
    native_Resume  = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GameGLSurfaceView_nativeResume");
}
