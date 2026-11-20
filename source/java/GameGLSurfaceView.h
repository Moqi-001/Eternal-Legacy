#ifndef JAVA_GAME_GL_SURFACE_VIEW_H
#define JAVA_GAME_GL_SURFACE_VIEW_H

#include <so_jni/so_jni.h>
#include <so_linker/so_linker.h>

// com/gameloft/android/TBFV/GloftELHP/ML/GameGLSurfaceView
void setup_GameGLSurfaceView(so_module *mod);

// 对应 View 回调：窗口尺寸变化、焦点变化
void GameGLSurfaceView_onSizeChanged(int width, int height);
void GameGLSurfaceView_onWindowFocusChanged(void);

// 触摸：(type, x, y, pointer) —— type: 0 抬起 / 1 按下 / 2 移动
void GameGLSurfaceView_onTouch(int type, int x, int y, int pointer);

int GameGLSurfaceView_getWidth(void);
int GameGLSurfaceView_getHeight(void);

#endif
