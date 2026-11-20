#ifndef SOLOADED_JAVA_H
#define SOLOADED_JAVA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <so_linker/so_linker.h>

#include "java/Device.h"
#include "java/EternalLegacy.h"
#include "java/GameGLSurfaceView.h"
#include "java/GameRenderer.h"
#include "java/GLMediaPlayer.h"
#include "java/GLResLoader.h"

// 所有 Java 类在这里注册。native 绑定要用 .so 的导出，所以模块要先加载。
int soloader_setup_java(void);

#ifdef __cplusplus
};
#endif

#endif
