/*
 * 引擎的路径都是 Android 上的绝对路径，Vita 上没有这些目录。
 * 这里集中做一次前缀替换，供 libEternalLegacy 的 IO 钩子和视频播放共用。
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "paths.h"
#include "../config.h"

#define ANDROID_GAME_DIR "/sdcard/gameloft/games/GloftELHP"
#define ANDROID_DATA_DIR "/data/data/com.gameloft.android.TBFV.GloftELHP.ML"
#define ANDROID_LIB_DIR  "/data/data/com.gameloft.android.TBFV.GloftELHP.ML/lib"

static const char *after_directory(const char *path, const char *directory) {
    size_t n = strlen(directory);
    if (strncmp(path, directory, n) || (path[n] && path[n] != '/')) return NULL;
    path += n;
    while (*path == '/') ++path;
    return path;
}

const char *paths_normalize_resource(const char *path, char *buf, size_t size) {
    if (!path || !buf || !size) { errno = EINVAL; return NULL; }
    size_t length = strlen(path), prefix = sizeof(GAME_DATA_DIR) - 1;
    if (length >= size) { errno = ENAMETOOLONG; return NULL; }
    if (strncmp(path, GAME_DATA_DIR, prefix)) return path;
    /* Resolving '..' could cross a real loose directory/alias. Leave the whole
     * path to native resolution rather than inventing inventory coverage. */
    for (const char *part = path + prefix; *part;) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (n == 2 && part[0] == '.' && part[1] == '.') return path;
        if (!end) break;
        part = end + 1;
    }
    memmove(buf, path, prefix);
    const char *src = path + prefix;
    char *dst = buf + prefix;
    while (*src) {
        if ((src == path + prefix || src[-1] == '/') && src[0] == '.' &&
            (src[1] == '/' || !src[1])) {
            src += src[1] ? 2 : 1;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = 0;
    return buf;
}

const char *paths_from_android(const char *path, char *buf, size_t size) {
    if (!path || !buf || !size) { errno = EINVAL; return NULL; }
    if (strlen(path) >= size) { errno = ENAMETOOLONG; return NULL; }

    if (!strcmp(path, "/proc/cpuinfo")) return "app0:/cpuinfo";
    if (!strcmp(path, "/proc/meminfo")) return "app0:/meminfo";
    if (!strcmp(path, "/dev/urandom")) return "app0:/urandom";

    // 已经是 Vita 设备路径（ux0:/app0:/...），别再当相对路径拼一次
    const char *colon = strchr(path, ':');
    const char *slash = strchr(path, '/');
    if (colon && (!slash || colon < slash)) {
        return paths_normalize_resource(path, buf, size);
    }

    const char *prefix = NULL, *rest = path, *suffix;
    if ((suffix = after_directory(path, ANDROID_GAME_DIR))) {
        prefix = GAME_DATA_DIR; rest = suffix;
    } else if ((suffix = after_directory(path, ANDROID_LIB_DIR))) {
        prefix = SO_LIB_DIR; rest = suffix;
    } else if ((suffix = after_directory(path, ANDROID_DATA_DIR))) {
        prefix = SO_FILES_DIR; rest = suffix;
    } else if (path[0] != '/') {
        prefix = GAME_DATA_DIR;
        while (rest[0] == '.' && rest[1] == '/') rest += 2;
    }
    if (!prefix) return path;
    int n = snprintf(buf, size, "%s%s", prefix, rest);
    if (n < 0 || (size_t)n >= size) { errno = ENAMETOOLONG; return NULL; }
    return paths_normalize_resource(buf, buf, size);
}
