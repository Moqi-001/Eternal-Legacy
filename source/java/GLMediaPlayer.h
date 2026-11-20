#ifndef JAVA_GLMEDIAPLAYER_H
#define JAVA_GLMEDIAPLAYER_H

#include <so_jni/so_jni.h>
#include <so_linker/so_linker.h>

// com/gameloft/android/TBFV/GloftELHP/ML/GLMediaPlayer
void setup_GLMediaPlayer(so_module *mod);

// Android 侧 GLMediaPlayer.init()
void GLMediaPlayer_init(void);

#endif
