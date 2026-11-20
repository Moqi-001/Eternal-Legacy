#ifndef JAVA_GAME_RENDERER_H
#define JAVA_GAME_RENDERER_H

#include <so_jni/so_jni.h>
#include <so_linker/so_linker.h>

// com/gameloft/android/TBFV/GloftELHP/ML/GameRenderer
void setup_GameRenderer(so_module *mod);

// 对应 GLSurfaceView.Renderer 的三个回调
void GameRenderer_onSurfaceCreated(void);
void GameRenderer_onSurfaceChanged(int width, int height);
void GameRenderer_onDrawFrame(void);
void GameRenderer_onSurfaceDestroyed(void);

#endif
