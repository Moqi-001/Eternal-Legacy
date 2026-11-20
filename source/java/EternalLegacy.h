#ifndef JAVA_ETERNAL_LEGACY_H
#define JAVA_ETERNAL_LEGACY_H

#include <so_jni/so_jni.h>
#include <so_linker/so_linker.h>

typedef struct _EternalLegacy *EternalLegacy;

// Java 侧类对象（com/gameloft/android/TBFV/GloftELHP/ML/EternalLegacy）
void setup_EternalLegacy(so_module *mod);

// 宿主扮演 Activity：onCreate 里做 nativeSetPhone，onResume/onPause 对应
// GameGLSurfaceView.nativeResume/nativePause
void EternalLegacy_onCreate(void);
void EternalLegacy_onResume(void);
void EternalLegacy_onPause(void);
void EternalLegacy_onDestroy(void);

// 引擎入口（GameRenderer.onSurfaceCreated 的最后一步）
void EternalLegacy_nativeInit(int state);

// Activity 实例（给 GameRenderer.a 这类静态 Context 字段用）
JObject EternalLegacy_activity(void);

// 输入（按键码用 Android KeyEvent 的 KEYCODE_*）
void EternalLegacy_onKeyDown(int key_code);
void EternalLegacy_onKeyUp(int key_code);

int EternalLegacy_should_quit(void);

#endif
