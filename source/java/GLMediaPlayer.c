/*
 * com/gameloft/android/TBFV/GloftELHP/ML/GLMediaPlayer
 *
 * 安卓上这里包着 MediaPlayer/SoundPool（音乐与音效）。移植先把接口摆齐：
 * 查询类方法返回"没有这个声音"，播放类方法直接忽略，让引擎按无声继续跑。
 * 真正出声要接 libOpenSLES，等画面跑通再做。
 */
#include <stdio.h>

#include <so_common/so_logger.h>
#include <so_java/java/lang/Class.h>
#include <so_java/java/lang/String.h>

#include "GameGLSurfaceView.h"
#include "GLMediaPlayer.h"

static JClass sClass            = NULL;
static int    sLogoVideoStarted = 0;
static int    sLogoVideoPolls   = 0;
static int    sLogoVideoDone    = 0;

static void (*native_Init)(JNIEnv *env, jclass clazz, jint flag);
static jint (*native_GetTotalSounds)(JNIEnv *env, jclass clazz);
static jint (*native_GetTotalSoundsOfSameInstance)(JNIEnv *env, jclass clazz);
static void (*native_SetStopOnMusic)(JNIEnv *env, jclass clazz, jint stop);

static JInt GLMediaPlayer_Get_DeviceLockState(void) {
    return 1;  // 未锁屏
}

static JInt GLMediaPlayer_Get_PhoneLanguage(void) {
    return 0;  // 0 = 英文
}

static JInt GLMediaPlayer_Get_PhoneManufacturer(void) {
    return 1;  // 未知厂商（对应 Java 侧最后的兜底分支）
}

static JInt GLMediaPlayer_Get_logoVideoState(void) {
    // 0 未播放 / 1 播放中 / 2 播完（Java 侧 MyVideoView 与它的 OnCompletionListener 写这两个值）
    if (sLogoVideoStarted) {
        if (++sLogoVideoPolls > 3) {
            sLogoVideoStarted = 0;
            sLogoVideoDone    = 1;
        } else {
            return 1;
        }
    }

    return sLogoVideoDone ? 2 : 0;
}

static JInt GLMediaPlayer_detectPhoneLang(void) {
    return 0;
}

static JInt GLMediaPlayer_getWidth(void) {
    return GameGLSurfaceView_getWidth();
}

static JInt GLMediaPlayer_getHeight(void) {
    return GameGLSurfaceView_getHeight();
}

// 返回值约定：0 = 已就绪/播放中，-1 = 没有
static JInt GLMediaPlayer_isSoundLoaded(JInt sound_id, JInt instance) {
    (void)sound_id;
    (void)instance;
    return -1;
}

static JInt GLMediaPlayer_isSoundLoadedBig(JInt sound_id) {
    (void)sound_id;
    return -1;
}

static JInt GLMediaPlayer_isMediaPlaying(JInt media_id) {
    (void)media_id;
    return 0;
}

static JInt GLMediaPlayer_loadMovie(JString name) {
    // 视频暂不接入：按"播放中"报几次再报"播完"，让引擎走完它的视频流程
    so_logi("[JAVA] GLMediaPlayer.loadMovie(%s): 跳过播放", name ? String_getUTFChars(name) : "NULL");

    sLogoVideoStarted = 1;
    sLogoVideoPolls   = 0;
    sLogoVideoDone    = 0;

    return 1;
}

static void GLMediaPlayer_loadSound(JInt sound_id, JInt resource_id) {
    (void)sound_id;
    (void)resource_id;
}

static void GLMediaPlayer_loadSoundBig(JInt sound_id) {
    (void)sound_id;
}

static void GLMediaPlayer_unloadSound(JInt sound_id, JInt instance) {
    (void)sound_id;
    (void)instance;
}

static void GLMediaPlayer_unloadSoundBig(JInt sound_id) {
    (void)sound_id;
}

static void GLMediaPlayer_playSound(JInt sound_id, JInt instance, JFloat volume) {
    (void)sound_id;
    (void)instance;
    (void)volume;
}

static void GLMediaPlayer_playSoundBig(JInt sound_id, JFloat volume, JInt loop) {
    (void)sound_id;
    (void)volume;
    (void)loop;
}

static void GLMediaPlayer_pauseSound(JInt sound_id, JInt instance) {
    (void)sound_id;
    (void)instance;
}

static void GLMediaPlayer_pauseSoundBig(JInt sound_id) {
    (void)sound_id;
}

static void GLMediaPlayer_resumeSound(JInt sound_id, JInt instance) {
    (void)sound_id;
    (void)instance;
}

static void GLMediaPlayer_resumeSoundBig(JInt sound_id) {
    (void)sound_id;
}

static void GLMediaPlayer_stopSound(JInt sound_id, JInt instance) {
    (void)sound_id;
    (void)instance;
}

static void GLMediaPlayer_stopSoundBig(JInt sound_id) {
    (void)sound_id;
}

static void GLMediaPlayer_setVolume(JInt sound_id, JInt instance, JFloat volume) {
    (void)sound_id;
    (void)instance;
    (void)volume;
}

static void GLMediaPlayer_setVolumeBig(JInt sound_id, JFloat volume) {
    (void)sound_id;
    (void)volume;
}

static void GLMediaPlayer_setPitch(JInt sound_id, JInt instance, JFloat pitch) {
    (void)sound_id;
    (void)instance;
    (void)pitch;
}

static void GLMediaPlayer_resetSound(JInt sound_id) {
    (void)sound_id;
}

static void GLMediaPlayer_stopAllSounds(void) {
}

static void GLMediaPlayer_stopAllPool(JInt pool_id) {
    (void)pool_id;
}

static void GLMediaPlayer_stopAllBig(JInt sound_id) {
    (void)sound_id;
}

static void GLMediaPlayer_destroySoundPool(void) {
}

static void GLMediaPlayer_initSoundPoolArray(void) {
}

static void GLMediaPlayer_ResumeMovie(void) {
}

static void GLMediaPlayer_SoundUpdate(void) {
}

static void GLMediaPlayer_OpenGLive(JInt index) {
    (void)index;
}

static void GLMediaPlayer_OpenIGP(JInt index) {
    (void)index;
}

static void GLMediaPlayer_openBrowser(JString url) {
    (void)url;
}

static void GLMediaPlayer_nativeInit(jint flag) {
    if (!native_Init) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Init(&_jni_env, JniNewLocalRefFromJava(sClass), flag);
    EXIT_JNI_CALL(NULL)
}

void GLMediaPlayer_init(void) {
    GLMediaPlayer_nativeInit(0);
}

static JSymbol class_symbols[] = {
    JAVA_SYMBOL_STATIC_NATIVE("nativeInit", "(I)V", &native_Init),
    JAVA_SYMBOL_STATIC_NATIVE("nativeGetTotalSounds", "()I", &native_GetTotalSounds),
    JAVA_SYMBOL_STATIC_NATIVE("nativeGetTotalSoundsOfSameInstance", "()I", &native_GetTotalSoundsOfSameInstance),
    JAVA_SYMBOL_STATIC_NATIVE("nativeSetStopOnMusic", "(I)V", &native_SetStopOnMusic),

    JAVA_SYMBOL_STATIC_METHOD("Get_DeviceLockState", "()I", &GLMediaPlayer_Get_DeviceLockState),
    JAVA_SYMBOL_STATIC_METHOD("Get_PhoneLanguage", "()I", &GLMediaPlayer_Get_PhoneLanguage),
    JAVA_SYMBOL_STATIC_METHOD("Get_PhoneManufacturer", "()I", &GLMediaPlayer_Get_PhoneManufacturer),
    JAVA_SYMBOL_STATIC_METHOD("Get_logoVideoState", "()I", &GLMediaPlayer_Get_logoVideoState),
    JAVA_SYMBOL_STATIC_METHOD("detectPhoneLang", "()I", &GLMediaPlayer_detectPhoneLang),
    JAVA_SYMBOL_STATIC_METHOD("getWidth", "()I", &GLMediaPlayer_getWidth),
    JAVA_SYMBOL_STATIC_METHOD("getHeight", "()I", &GLMediaPlayer_getHeight),
    JAVA_SYMBOL_STATIC_METHOD("isSoundLoaded", "(II)I", &GLMediaPlayer_isSoundLoaded),
    JAVA_SYMBOL_STATIC_METHOD("isSoundLoadedBig", "(I)I", &GLMediaPlayer_isSoundLoadedBig),
    JAVA_SYMBOL_STATIC_METHOD("isMediaPlaying", "(I)I", &GLMediaPlayer_isMediaPlaying),
    JAVA_SYMBOL_STATIC_METHOD("loadMovie", "(Ljava/lang/String;)I", &GLMediaPlayer_loadMovie),

    JAVA_SYMBOL_STATIC_METHOD("loadSound", "(II)V", &GLMediaPlayer_loadSound),
    JAVA_SYMBOL_STATIC_METHOD("loadSoundBig", "(I)V", &GLMediaPlayer_loadSoundBig),
    JAVA_SYMBOL_STATIC_METHOD("unloadSound", "(II)V", &GLMediaPlayer_unloadSound),
    JAVA_SYMBOL_STATIC_METHOD("unloadSoundBig", "(I)V", &GLMediaPlayer_unloadSoundBig),
    JAVA_SYMBOL_STATIC_METHOD("playSound", "(IIF)V", &GLMediaPlayer_playSound),
    JAVA_SYMBOL_STATIC_METHOD("playSoundBig", "(IFI)V", &GLMediaPlayer_playSoundBig),
    JAVA_SYMBOL_STATIC_METHOD("pauseSound", "(II)V", &GLMediaPlayer_pauseSound),
    JAVA_SYMBOL_STATIC_METHOD("pauseSoundBig", "(I)V", &GLMediaPlayer_pauseSoundBig),
    JAVA_SYMBOL_STATIC_METHOD("resumeSound", "(II)V", &GLMediaPlayer_resumeSound),
    JAVA_SYMBOL_STATIC_METHOD("resumeSoundBig", "(I)V", &GLMediaPlayer_resumeSoundBig),
    JAVA_SYMBOL_STATIC_METHOD("stopSound", "(II)V", &GLMediaPlayer_stopSound),
    JAVA_SYMBOL_STATIC_METHOD("stopSoundBig", "(I)V", &GLMediaPlayer_stopSoundBig),
    JAVA_SYMBOL_STATIC_METHOD("setVolume", "(IIF)V", &GLMediaPlayer_setVolume),
    JAVA_SYMBOL_STATIC_METHOD("setVolumeBig", "(IF)V", &GLMediaPlayer_setVolumeBig),
    JAVA_SYMBOL_STATIC_METHOD("setPitch", "(IIF)V", &GLMediaPlayer_setPitch),
    JAVA_SYMBOL_STATIC_METHOD("resetSound", "(I)V", &GLMediaPlayer_resetSound),
    JAVA_SYMBOL_STATIC_METHOD("stopAllSounds", "()V", &GLMediaPlayer_stopAllSounds),
    JAVA_SYMBOL_STATIC_METHOD("stopAllPool", "(I)V", &GLMediaPlayer_stopAllPool),
    JAVA_SYMBOL_STATIC_METHOD("stopAllBig", "(I)V", &GLMediaPlayer_stopAllBig),
    JAVA_SYMBOL_STATIC_METHOD("destroySoundPool", "()V", &GLMediaPlayer_destroySoundPool),
    JAVA_SYMBOL_STATIC_METHOD("initSoundPoolArray", "()V", &GLMediaPlayer_initSoundPoolArray),
    JAVA_SYMBOL_STATIC_METHOD("ResumeMovie", "()V", &GLMediaPlayer_ResumeMovie),
    JAVA_SYMBOL_STATIC_METHOD("SoundUpdate", "()V", &GLMediaPlayer_SoundUpdate),
    JAVA_SYMBOL_STATIC_METHOD("OpenGLive", "(I)V", &GLMediaPlayer_OpenGLive),
    JAVA_SYMBOL_STATIC_METHOD("OpenIGP", "(I)V", &GLMediaPlayer_OpenIGP),
    JAVA_SYMBOL_STATIC_METHOD("openBrowser", "(Ljava/lang/String;)V", &GLMediaPlayer_openBrowser),
};

static JClassMeta class_meta = {
    .name          = "com/gameloft/android/TBFV/GloftELHP/ML/GLMediaPlayer",
    .super_name    = "java/lang/Object",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = 0,  // 纯静态
    .dtor          = NULL,
};

void setup_GLMediaPlayer(so_module *mod) {
    sClass = JavaCreateClass(&class_meta);
    if (!sClass) {
        so_loge("[JAVA] GLMediaPlayer: create class failed");
        return;
    }

    native_Init                         = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GLMediaPlayer_nativeInit");
    native_GetTotalSounds               = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GLMediaPlayer_nativeGetTotalSounds");
    native_GetTotalSoundsOfSameInstance = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GLMediaPlayer_nativeGetTotalSoundsOfSameInstance");
    native_SetStopOnMusic               = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GLMediaPlayer_nativeSetStopOnMusic");
}
