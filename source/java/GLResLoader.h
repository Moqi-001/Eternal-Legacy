#ifndef JAVA_GLRESLOADER_H
#define JAVA_GLRESLOADER_H

#include <so_jni/so_jni.h>
#include <so_linker/so_linker.h>

// com/gameloft/android/TBFV/GloftELHP/ML/GLResLoader
void setup_GLResLoader(so_module *mod);

// Android 侧 GLResLoader.init()
void GLResLoader_init(void);

#endif
