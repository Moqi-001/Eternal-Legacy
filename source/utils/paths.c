/*
 * 引擎的路径都是 Android 上的绝对路径，Vita 上没有这些目录。
 * 这里集中做一次前缀替换，供 libEternalLegacy 的 IO 钩子和视频播放共用。
 */
#include <stdio.h>
#include <string.h>

#include "paths.h"
#include "../config.h"

#define ANDROID_GAME_DIR "/sdcard/gameloft/games/GloftELHP/"
#define ANDROID_DATA_DIR "/data/data/com.gameloft.android.TBFV.GloftELHP.ML"
#define ANDROID_LIB_DIR  "/data/data/com.gameloft.android.TBFV.GloftELHP.ML/lib/"

static const char *replace_prefix(const char *path, const char *prefix, const char *replacement,
                                  char *buf, size_t size) {
    size_t prefix_len = strlen(prefix);

    if (strncmp(path, prefix, prefix_len) != 0) {
        return NULL;
    }

    snprintf(buf, size, "%s%s", replacement, path + prefix_len);
    return buf;
}

const char *paths_from_android(const char *path, char *buf, size_t size) {
    if (!path || !buf || size == 0) {
        return path;
    }

    // 已经是 Vita 设备路径（ux0:/app0:/...），别再当相对路径拼一次
    const char *colon = strchr(path, ':');
    const char *slash = strchr(path, '/');
    if (colon && (!slash || colon < slash)) {
        return path;
    }

    const char *result;

    result = replace_prefix(path, ANDROID_GAME_DIR, GAME_DATA_DIR, buf, size);
    if (result) {
        return result;
    }

    // 引擎会读自己安装目录下的 .so（校验/自加载），指回 port 的库目录
    result = replace_prefix(path, ANDROID_LIB_DIR, SO_LIB_DIR, buf, size);
    if (result) {
        return result;
    }

    result = replace_prefix(path, ANDROID_DATA_DIR, SO_FILES_DIR, buf, size);
    if (result) {
        return result;
    }

    if (path[0] != '/') {
        const char *relative = path;
        while (relative[0] == '.' && relative[1] == '/') {
            relative += 2;
        }
        snprintf(buf, size, "%s%s", GAME_DATA_DIR, relative);
        return buf;
    }

    return path;
}
