/*
 * com/gameloft/android/TBFV/GloftELHP/ML/EternalLegacy（游戏主 Activity）
 *
 * 引擎是静态注册的 JNI native：Java 侧只有一串 native 声明，真正的入口在
 * libEternalLegacy.so 的 Java_com_gameloft_..._EternalLegacy_native* 里。
 * 所以这里既要把宿主当 Java 用（按 Activity 生命周期调 native），又要提供
 * native 反向调用 Java 的方法（da/db/dc/Exit/sendAppToBackground/openBrowser）。
 */
#include <string.h>

#include <so_common/so_logger.h>
#include <so_java/java/lang/Array.h>
#include <so_java/java/lang/Object.h>
#include <so_java/java/lang/String.h>

#include "Device.h"
#include "EternalLegacy.h"

struct _EternalLegacy {
    JInt state;
};

static JClass  sClass    = NULL;
static JObject sInstance = NULL;
static int     sQuit     = 0;

static void (*native_SetPhone)(JNIEnv *env, jclass clazz, jint width);
static void (*native_Init)(JNIEnv *env, jclass clazz, jint state);
static void (*native_KeyDown)(JNIEnv *env, jclass clazz, jint key_code);
static void (*native_KeyUp)(JNIEnv *env, jclass clazz, jint key_code);
static jfloat (*native_GetMVolume)(JNIEnv *env, jclass clazz);
static jint (*native_CanInterrupt)(JNIEnv *env, jclass clazz);
static jint (*native_GetCurrentStateID)(JNIEnv *env, jclass clazz);
static jint (*native_getState)(JNIEnv *env, jclass clazz, jint key_code);
static void (*native_SetOrientation)(JNIEnv *env, jclass clazz, jint orientation);
static void (*native_Accelerometer)(JNIEnv *env, jclass clazz, jfloat x, jfloat y, jfloat z);
static void (*native_onTrackballEvent)(JNIEnv *env, jclass clazz, jint direction);
static void (*native_GetInfo)(JNIEnv *env, jclass clazz, jstring a, jstring b, jstring c, jstring d);
static void (*native_QuitGame)(JNIEnv *env, jclass clazz);

/*
 * 引擎拿设备标识当 DRM/存档的一部分：db() 是设备 ID，dc() 是 DRM 串。
 * 安卓上 dc() 走 GloftDRM 联网校验，这里统一用固定设备 ID 的字节，
 * 与 Android 侧校验失败时的回落值一致。
 */
static JObject EternalLegacy_db(void) {
    const char *id  = Device_getDeviceId();
    JInt        len = (JInt)strlen(id);

    JObject array = Array_new(JTYPE_BYTE, len);
    if (!array) {
        return NULL;
    }

    Array_setRegion(array, 0, len, id);
    return array;
}

static JObject EternalLegacy_dc(void) {
    return EternalLegacy_db();
}

// da(): 引擎拿 Activity 去 getSystemService，必须返回真实实例
static JObject EternalLegacy_da(void) {
    return Object_retain(sInstance);
}

static void EternalLegacy_Exit(void) {
    so_loga("[JAVA] EternalLegacy.Exit()");
    sQuit = 1;
}

static void EternalLegacy_sendAppToBackground(void) {
    so_logi("[JAVA] EternalLegacy.sendAppToBackground()");
}

static void EternalLegacy_openBrowser(JString url) {
    so_logi("[JAVA] EternalLegacy.openBrowser(%s)", url ? String_getUTFChars(url) : "NULL");
}

static void EternalLegacy_LicenseCheck(void) {
    so_logi("[JAVA] EternalLegacy.LicenseCheck()");
}

// Tracking.init(...)/onLaunchGame()：统计上报，无网络环境下什么都不做
static void EternalLegacy_d(void) {
}

// ActivityManager.MemoryInfo.availMem：引擎拿它决定资源缓存规模
static jlong EternalLegacy_GetMemoryIno(void) {
    return 64 * 1024 * 1024;
}

static void EternalLegacy_createNomedia(JString dir) {
    (void)dir;
}

static void EternalLegacy_nativeSetPhone(jint width) {
    if (!native_SetPhone) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_SetPhone(&_jni_env, JniNewLocalRefFromJava(sClass), width);
    EXIT_JNI_CALL(NULL)
}

void EternalLegacy_nativeInit(jint state) {
    if (!native_Init) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Init(&_jni_env, JniNewLocalRefFromJava(sClass), state);
    EXIT_JNI_CALL(NULL)
}

static void EternalLegacy_nativeKeyDown(jint key_code) {
    if (!native_KeyDown) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_KeyDown(&_jni_env, JniNewLocalRefFromJava(sClass), key_code);
    EXIT_JNI_CALL(NULL)
}

static void EternalLegacy_nativeKeyUp(jint key_code) {
    if (!native_KeyUp) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_KeyUp(&_jni_env, JniNewLocalRefFromJava(sClass), key_code);
    EXIT_JNI_CALL(NULL)
}

static jfloat EternalLegacy_nativeGetMVolume(void) {
    jfloat volume = 0.0f;

    if (!native_GetMVolume) {
        return volume;
    }

    ENTER_JNI_CALL(0)
    volume = native_GetMVolume(&_jni_env, JniNewLocalRefFromJava(sClass));
    EXIT_JNI_CALL(NULL)
    return volume;
}

static jint EternalLegacy_nativeCanInterrupt(void) {
    jint result = 0;

    if (!native_CanInterrupt) {
        return result;
    }

    ENTER_JNI_CALL(0)
    result = native_CanInterrupt(&_jni_env, JniNewLocalRefFromJava(sClass));
    EXIT_JNI_CALL(NULL)
    return result;
}

static jint EternalLegacy_nativeGetCurrentStateID(void) {
    jint result = 0;

    if (!native_GetCurrentStateID) {
        return result;
    }

    ENTER_JNI_CALL(0)
    result = native_GetCurrentStateID(&_jni_env, JniNewLocalRefFromJava(sClass));
    EXIT_JNI_CALL(NULL)
    return result;
}

static jint EternalLegacy_nativegetState(jint key_code) {
    jint result = 0;

    if (!native_getState) {
        return result;
    }

    ENTER_JNI_CALL(0)
    result = native_getState(&_jni_env, JniNewLocalRefFromJava(sClass), key_code);
    EXIT_JNI_CALL(NULL)
    return result;
}

static void EternalLegacy_nativeSetOrientation(jint orientation) {
    if (!native_SetOrientation) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_SetOrientation(&_jni_env, JniNewLocalRefFromJava(sClass), orientation);
    EXIT_JNI_CALL(NULL)
}

static void EternalLegacy_nativeAccelerometer(jfloat x, jfloat y, jfloat z) {
    if (!native_Accelerometer) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Accelerometer(&_jni_env, JniNewLocalRefFromJava(sClass), x, y, z);
    EXIT_JNI_CALL(NULL)
}

static void EternalLegacy_nativeonTrackballEvent(jint direction) {
    if (!native_onTrackballEvent) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_onTrackballEvent(&_jni_env, JniNewLocalRefFromJava(sClass), direction);
    EXIT_JNI_CALL(NULL)
}

static void EternalLegacy_nativeGetInfo(const char *a, const char *b, const char *c, const char *d) {
    if (!native_GetInfo) {
        return;
    }

    ENTER_JNI_CALL(4)
    jstring ja = JniNewStringUTF(a ? a : "");
    jstring jb = JniNewStringUTF(b ? b : "");
    jstring jc = JniNewStringUTF(c ? c : "");
    jstring jd = JniNewStringUTF(d ? d : "");
    native_GetInfo(&_jni_env, JniNewLocalRefFromJava(sClass), ja, jb, jc, jd);
    EXIT_JNI_CALL(NULL)
}

static void EternalLegacy_nativeQuitGame(void) {
    if (!native_QuitGame) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_QuitGame(&_jni_env, JniNewLocalRefFromJava(sClass));
    EXIT_JNI_CALL(NULL)
}

static JSymbol class_symbols[] = {
    // native（引擎导出）
    JAVA_SYMBOL_STATIC_NATIVE("nativeSetPhone", "(I)V", &native_SetPhone),
    JAVA_SYMBOL_STATIC_NATIVE("nativeInit", "(I)V", &native_Init),
    JAVA_SYMBOL_STATIC_NATIVE("nativeKeyDown", "(I)V", &native_KeyDown),
    JAVA_SYMBOL_STATIC_NATIVE("nativeKeyUp", "(I)V", &native_KeyUp),
    JAVA_SYMBOL_STATIC_NATIVE("nativeGetMVolume", "()F", &native_GetMVolume),
    JAVA_SYMBOL_STATIC_NATIVE("nativeCanInterrupt", "()I", &native_CanInterrupt),
    JAVA_SYMBOL_STATIC_NATIVE("nativeGetCurrentStateID", "()I", &native_GetCurrentStateID),
    JAVA_SYMBOL_STATIC_NATIVE("nativegetState", "(I)I", &native_getState),
    JAVA_SYMBOL_STATIC_NATIVE("nativeSetOrientation", "(I)V", &native_SetOrientation),
    JAVA_SYMBOL_STATIC_NATIVE("nativeAccelerometer", "(FFF)V", &native_Accelerometer),
    JAVA_SYMBOL_STATIC_NATIVE("nativeonTrackballEvent", "(I)V", &native_onTrackballEvent),
    JAVA_SYMBOL_STATIC_NATIVE("nativeGetInfo", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", &native_GetInfo),
    JAVA_SYMBOL_STATIC_NATIVE("nativeQuitGame", "()V", &native_QuitGame),

    // native 反向调用的 Java 方法
    JAVA_SYMBOL_STATIC_METHOD("da", "()Landroid/app/Activity;", &EternalLegacy_da),
    JAVA_SYMBOL_STATIC_METHOD("db", "()[B", &EternalLegacy_db),
    JAVA_SYMBOL_STATIC_METHOD("dc", "()[B", &EternalLegacy_dc),
    JAVA_SYMBOL_STATIC_METHOD("Exit", "()V", &EternalLegacy_Exit),
    JAVA_SYMBOL_STATIC_METHOD("sendAppToBackground", "()V", &EternalLegacy_sendAppToBackground),
    JAVA_SYMBOL_STATIC_METHOD("openBrowser", "(Ljava/lang/String;)V", &EternalLegacy_openBrowser),
    JAVA_SYMBOL_STATIC_METHOD("LicenseCheck", "()V", &EternalLegacy_LicenseCheck),
    JAVA_SYMBOL_STATIC_METHOD("d", "()V", &EternalLegacy_d),
    JAVA_SYMBOL_STATIC_METHOD("GetMemoryIno", "()J", &EternalLegacy_GetMemoryIno),
    JAVA_SYMBOL_STATIC_METHOD("createNomedia", "(Ljava/lang/String;)V", &EternalLegacy_createNomedia),
};

static JClassMeta class_meta = {
    .name          = "com/gameloft/android/TBFV/GloftELHP/ML/EternalLegacy",
    .super_name    = "android/app/Activity",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = sizeof(struct _EternalLegacy),
    .dtor          = NULL,
};

void EternalLegacy_onCreate(void) {
    // 安卓上是 getWindowManager() 的 widthPixels（屏幕物理宽度）
    EternalLegacy_nativeSetPhone(960);
}

void EternalLegacy_onResume(void) {
    so_logi("[JAVA] EternalLegacy.onResume (nativeGetMVolume=%.2f)", EternalLegacy_nativeGetMVolume());
}

void EternalLegacy_onPause(void) {
    so_logi("[JAVA] EternalLegacy.onPause");
}

void EternalLegacy_onDestroy(void) {
    so_logi("[JAVA] EternalLegacy.onDestroy");
}

void EternalLegacy_onKeyDown(int key_code) {
    EternalLegacy_nativeKeyDown(key_code);
}

void EternalLegacy_onKeyUp(int key_code) {
    // 与 Java 一致：只有返回键(4)/菜单键(82)先问 native 状态，其余直接透传
    if (key_code == 4 || key_code == 82) {
        jint state = EternalLegacy_nativegetState(key_code);
        if (state == 1 || state == 2) {
            EternalLegacy_nativeKeyUp(key_code);
        }
        return;
    }

    EternalLegacy_nativeKeyUp(key_code);
}

int EternalLegacy_should_quit(void) {
    return sQuit;
}

JObject EternalLegacy_activity(void) {
    return Object_retain(sInstance);
}

void setup_EternalLegacy(so_module *mod) {
    sClass = JavaCreateClass(&class_meta);
    if (!sClass) {
        so_loge("[JAVA] EternalLegacy: create class failed");
        return;
    }

    sInstance = Object_new(sClass);

    native_SetPhone          = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeSetPhone");
    native_Init              = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeInit");
    native_KeyDown           = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeKeyDown");
    native_KeyUp             = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeKeyUp");
    native_GetMVolume        = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeGetMVolume");
    native_CanInterrupt      = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeCanInterrupt");
    native_GetCurrentStateID = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeGetCurrentStateID");
    native_getState          = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativegetState");
    native_SetOrientation    = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeSetOrientation");
    native_Accelerometer     = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeAccelerometer");
    native_onTrackballEvent  = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeonTrackballEvent");
    native_GetInfo           = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeGetInfo");
    native_QuitGame          = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_EternalLegacy_nativeQuitGame");
}
