/*
 * 两个游戏库的路径映射
 *
 * 引擎把资源目录写死成 /sdcard/gameloft/games/GloftELHP/，可写目录写死成
 * /data/data/<包名>/，还会去 /data/data/<包名>/lib/ 读自己的 .so（StormGLOFT
 * 用来自查符号表）。这些路径在 Vita 上都不存在，所以改掉库自己的这几个 libc
 * 导入，指向 port 的目录布局。
 *
 * 直接调 *_soloader 那层而不是 hook 真正的 fopen/open：宿主自己的文件访问
 * （日志、缓存）不该被改道。
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <psp2/io/stat.h>

#include <so_common/so_logger.h>
#include <so_libs/libc/bionic_fcntl.h>
#include <so_libs/libc/bionic_stdio.h>
#include <so_linker/so_linker.h>

#include "paths.h"
#include "config.h"

#define ANDROID_GAME_DIR "/sdcard/gameloft/games/GloftELHP/"
#define ANDROID_DATA_DIR "/data/data/com.gameloft.android.TBFV.GloftELHP.ML"
#define ANDROID_LIB_DIR  "/data/data/com.gameloft.android.TBFV.GloftELHP.ML/lib/"

#define MAP_PATH_MAX 512

/*
 * 命中映射规则时把结果写进 buf 并返回 buf，否则返回原指针（零拷贝）。
 * 相对路径里引擎常用的 "./data/..." / "data/..." 也落回游戏数据目录。
 */
static const char *map_path(const char *path, char *buf) {
    if (!path) {
        return path;
    }

    if (strncmp(path, ANDROID_GAME_DIR, sizeof(ANDROID_GAME_DIR) - 1) == 0) {
        snprintf(buf, MAP_PATH_MAX, "%s%s", GAME_DATA_DIR, path + sizeof(ANDROID_GAME_DIR) - 1);
        return buf;
    }

    // 引擎会读自己安装目录下的 .so（自查符号表），指回 port 的库目录
    if (strncmp(path, ANDROID_LIB_DIR, sizeof(ANDROID_LIB_DIR) - 1) == 0) {
        snprintf(buf, MAP_PATH_MAX, "%s%s", SO_LIB_DIR, path + sizeof(ANDROID_LIB_DIR) - 1);
        return buf;
    }

    if (strncmp(path, ANDROID_DATA_DIR, sizeof(ANDROID_DATA_DIR) - 1) == 0) {
        const char *rest = path + sizeof(ANDROID_DATA_DIR) - 1;

        while (*rest == '/') {
            rest++;
        }
        snprintf(buf, MAP_PATH_MAX, "%sfiles/%s", DATA_PATH, rest);
        return buf;
    }

    if (path[0] != '/') {
        const char *rel = path;

        while (rel[0] == '.' && rel[1] == '/') {
            rel += 2;
        }
        snprintf(buf, MAP_PATH_MAX, "%s%s", GAME_DATA_DIR, rel);
        return buf;
    }

    return path;
}

static FILE *repl_fopen(const char *path, const char *mode) {
    char        buf[MAP_PATH_MAX];
    const char *mapped = map_path(path, buf);

    FILE *result = fopen_soloader(mapped, mode);
    if (!result) {
        so_logd("[PATCH] fopen('%s','%s'): 失败", mapped, mode);
    }

    return result;
}

static int repl_open(const char *path, int oflag, ...) {
    char        buf[MAP_PATH_MAX];
    mode_t      mode   = 0;
    const char *mapped = map_path(path, buf);

    if (oflag & BIONIC_O_CREAT) {
        va_list args;
        va_start(args, oflag);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    return open_soloader(mapped, oflag, mode);
}

static int repl_access(const char *path, int mode) {
    char        buf[MAP_PATH_MAX];
    const char *mapped = map_path(path, buf);

    return access(mapped, mode);
}

int soloader_patch_paths(so_module *mod) {
    if (!mod) {
        return -1;
    }

    so_module_hook_import(mod, "fopen", (void *)repl_fopen, NULL);
    so_module_hook_import(mod, "open", (void *)repl_open, NULL);
    so_module_hook_import(mod, "access", (void *)repl_access, NULL);

    return 0;
}
