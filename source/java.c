#include <so_java/android/app/Activity.h>
#include <so_java/android/content/Context.h>
#include <so_java/android/media/AudioTrack.h>
#include <so_java/android/telephony/TelephonyManager.h>

#include "java.h"
#include "loader.h"

int soloader_setup_java(void) {
    // android 基础类：引擎初始化时按 getSystemService 的链路去取设备信息
    setup_android_content_Context();
    setup_android_app_Activity();
    setup_android_telephony_TelephonyManager();
    setup_android_media_AudioTrack();

    // 游戏自己的类（顺序：EternalLegacy 提供 Activity 实例，GameRenderer.a 要用）
    setup_EternalLegacy(EternalLegacy_mod);
    setup_Device(EternalLegacy_mod);
    setup_GLResLoader(EternalLegacy_mod);
    setup_GLMediaPlayer(EternalLegacy_mod);
    setup_GameRenderer(EternalLegacy_mod);
    setup_GameGLSurfaceView(EternalLegacy_mod);

    return 0;
}
