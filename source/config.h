#ifndef SOLOADER_CONFIG_H
#define SOLOADER_CONFIG_H

#define SCREEN_W 960
#define SCREEN_H 544

// 渲染线程栈：引擎在 nativeInit 里要用到 256 KB 以上的栈（ALicenseCheck::LoadConfig），
// 安卓上这段跑在 GL 线程（默认 1 MB）
#define SCREEN_THREAD_STACK (1024 * 1024)

#define MEMORY_NEWLIB_MB            256
#define VITAGL_RAM_THRESHOLD_MB     20
#define VITAGL_CDRAM_THRESHOLD_MB   4
#define VITAGL_PHYCONT_THRESHOLD_MB 8
#define VITAGL_CDIALOG_THRESHOLD_MB 12

#define SO_LIB_DIR    DATA_PATH "lib/armeabi-v7a/"
#define SO_ASSETS_DIR DATA_PATH "assets/"
#define SO_FILES_DIR  DATA_PATH "files/"
#define SO_CACHE_DIR  DATA_PATH "cache/"

#define SO_APK_PATH DATA_PATH "base.apk"
#define SO_TTF_PATH "app0:font.ttf"

// 游戏把资源目录写死成 /sdcard/gameloft/games/GloftELHP/，移植后换成这条前缀
#define GAME_DATA_DIR DATA_PATH "gameloft/games/GloftELHP/"

#endif