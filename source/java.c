#include <so_java/android/app/Activity.h>
#include <so_java/android/content/Context.h>
#include <so_java/android/media/AudioTrack.h>
#include <so_java/android/telephony/TelephonyManager.h>

#include "java.h"

int soloader_setup_java(so_module *mod) {
    // android 基础类：引擎初始化时按 getSystemService 的链路去取设备信息
    setup_android_content_Context();
    setup_android_app_Activity();
    setup_android_telephony_TelephonyManager();
    setup_android_media_AudioTrack();

    // 游戏自己的类（顺序：EternalLegacy 提供 Activity 实例，GameRenderer.a 要用）
    setup_EternalLegacy(mod);
    setup_Device(mod);
    setup_GLResLoader(mod);
    setup_GLMediaPlayer(mod);
    setup_GameRenderer(mod);
    setup_GameGLSurfaceView(mod);

    return 0;
}
