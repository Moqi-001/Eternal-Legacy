/*
 * 补丁总入口
 *
 * 结构约定：
 *   patch.c/.h      —— 总入口，只对外暴露 soloader_patch_modules()
 *   patch/<模块>.c  —— 各 .so 真正的补丁实现，模块指针由这里传入
 */
#include <psp2/io/stat.h>

#include "config.h"
#include "loader.h"
#include "patch.h"
#include "patch/paths.h"

int soloader_patch_modules() {
    // 引擎按自己写死的安卓路径找资源和库文件
    soloader_patch_paths(EternalLegacy_mod);

    // 可写目录（存档走 /data/data/<包名>/ → files/）
    sceIoMkdir(DATA_PATH "files", 0777);

    return 0;
}
