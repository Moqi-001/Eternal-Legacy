/*
 * Android 上有、Vita 上没有对应实现的 POSIX 接口。
 *
 * 这些函数游戏会正常调用，但 Vita 内核没有等价物（伪终端、/proc 符号链接、
 * POSIX 信号）。这里按"尽力而为"补齐：能给出合理答案的给答案，做不到的明确
 * 返回失败/错误码，不假装成功到让调用方算出错误结果。
 *
 * 对应符号在 libc_so.c 的 "File IO (POSIX)" / "Signal" 段导出。
 */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

// 没有 /proc 的符号链接可读
ssize_t readlink_soloader(const char *path, char *buf, size_t bufsiz) {
    (void)path;
    (void)buf;
    (void)bufsiz;
    errno = ENOENT;
    return -1;
}

// 没有伪终端
char *ptsname_soloader(int fd) {
    (void)fd;
    errno = ENOTSUP;
    return NULL;
}

int unlockpt_soloader(int fd) {
    (void)fd;
    errno = ENOTSUP;
    return -1;
}

/*
 * POSIX 信号在 Vita 上没有对应机制：不安装任何处理函数，直接返回成功。
 * 调用方（通常是忽略 SIGPIPE）看重的就是"设置成功"这个结果。
 */
int sigaction_soloader(int sig, const void *act, void *old) {
    (void)sig;
    (void)act;
    (void)old;
    return 0;
}
