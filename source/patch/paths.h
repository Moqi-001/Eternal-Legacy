#ifndef SOLOADER_PATCH_PATHS_H
#define SOLOADER_PATCH_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <so_linker/so_linker.h>

// 给一个游戏库装上路径映射（资源目录 / 安装目录 / 库目录 → port 的目录布局）
int soloader_patch_paths(so_module *mod);

#ifdef __cplusplus
};
#endif

#endif
