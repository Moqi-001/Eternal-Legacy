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
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <so_libs/libc/bionic_fcntl.h>
#include <so_libs/libc/bionic_stdio.h>
#include <so_libs/libc/sys/bionic_stat.h>
#include <so_linker/so_linker.h>

#include "paths.h"
#include "utils/paths.h"
#include "utils/perf.h"
#include "utils/native_io.h"
#include "voice_index.h"

static FILE *repl_fopen(const char *path, const char *mode) {
    uint64_t start = perf_now_us();
    char buf[PATH_MAPPED_MAX];
    const char *mapped = paths_from_android(path, buf, sizeof(buf));
    int hit = 0;
    FILE *result = mapped ? native_io_fopen_at(mapped, mode, &hit, (uintptr_t)__builtin_return_address(0)) : NULL;
    int saved_errno = errno;
    perf_record(PERF_FOPEN, mapped ? mapped : path, start, 0, result != NULL, hit == 1);
    if (result && mapped) voice_index_prefetch_following(mapped);
    native_io_poll(0);
    errno = saved_errno;
    return result;
}

static int repl_open(const char *path, int oflag, ...) {
    uint64_t start = perf_now_us();
    char buf[PATH_MAPPED_MAX];
    const char *mapped = paths_from_android(path, buf, sizeof(buf));
    int mode = 0;
    if (oflag & BIONIC_O_CREAT) {
        va_list args;
        va_start(args, oflag);
        mode = va_arg(args, int);
        va_end(args);
    }
    int result = mapped ? native_io_open_at(mapped, oflag, mode, (uintptr_t)__builtin_return_address(0)) : -1;
    perf_record(PERF_OPEN, mapped ? mapped : path, start, 0, result >= 0, 0);
    return result;
}

static int repl_access(const char *path, int mode) {
    uint64_t start = perf_now_us();
    char buf[PATH_MAPPED_MAX];
    const char *mapped = paths_from_android(path, buf, sizeof(buf));
    int result = mapped ? access(mapped, mode) : -1;
    perf_record(PERF_STAT, mapped ? mapped : path, start, 0, result == 0, 0);
    return result;
}

static int repl_stat(const char *path, struct stat_bionic *st) {
    uint64_t start = perf_now_us();
    char buf[PATH_MAPPED_MAX];
    const char *mapped = paths_from_android(path, buf, sizeof(buf));
    int result = mapped ? stat_soloader(mapped, st) : -1;
    perf_record(PERF_STAT, mapped ? mapped : path, start, 0, result == 0, 0);
    return result;
}

static int repl_lstat(const char *path, struct stat_bionic *st) {
    uint64_t start = perf_now_us();
    char buf[PATH_MAPPED_MAX];
    const char *mapped = paths_from_android(path, buf, sizeof(buf));
    int result = mapped ? lstat_soloader(mapped, st) : -1;
    perf_record(PERF_STAT, mapped ? mapped : path, start, 0, result == 0, 0);
    return result;
}

int soloader_patch_paths(so_module *mod) {
    if (!mod) return -1;
    const struct { const char *name; void *fn; } hooks[] = {
        {"fopen", repl_fopen}, {"open", repl_open}, {"access", repl_access},
        {"stat", repl_stat}, {"lstat", repl_lstat},
    };
    for (unsigned i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i)
        if (so_module_find_import(mod, hooks[i].name) &&
            so_module_hook_import(mod, hooks[i].name, hooks[i].fn, NULL) < 0) return -1;
    return 0;
}
