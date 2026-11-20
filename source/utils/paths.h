#ifndef SOLOADER_PATHS_H
#define SOLOADER_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// 映射后路径的缓冲区长度（ux0:data/<游戏>/gameloft/games/GloftELHP/ + 资源名）
#define PATH_MAPPED_MAX 512

/*
 * 把引擎里写死的 Android 路径换成 port 的目录布局：
 *   /sdcard/gameloft/games/GloftELHP/...  → GAME_DATA_DIR
 *   /data/data/<包名>/lib/...             → SO_LIB_DIR
 *   /data/data/<包名>/...                 → SO_FILES_DIR
 *   相对路径（"./data/..." 等）            → GAME_DATA_DIR 下
 * 命中时结果写进 buf 并返回 buf；不命中直接返回 path（零拷贝）。
 */
const char *paths_from_android(const char *path, char *buf, size_t size);

#ifdef __cplusplus
};
#endif

#endif
