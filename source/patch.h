#ifndef SOLOADED_PATCH_H
#define SOLOADED_PATCH_H

#ifdef __cplusplus
extern "C" {
#endif

// 补丁总入口：所有 .so 加载完成后调用一次。
// 具体补丁按模块放在 patch/ 目录下，对 .so 的修改与观测都在那里。
int soloader_patch_modules();

#ifdef __cplusplus
};
#endif

#endif
