#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include "reimpl/controls.h"
#include <stdio.h>
int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif
#define SCREEN_W 960
#define SCREEN_H 544
so_module so_mod;
#define CONFIG_FILE_PATH DATA_PATH"config.txt"

jobjectArray JobjectArray;
int StartOffset;

void loadConfig() {
    char buffer[30];
    int value;
    StartOffset=0;
    FILE *config = fopen(CONFIG_FILE_PATH, "r");

    if (config) {
        while (EOF != fscanf(config, "%[^ ] %d\n", buffer, &value)) {
            if (strcmp("StartOffset", buffer) == 0) 
                 StartOffset = value;
          
        }
        fclose(config);
    }
}

int main_thread(SceSize args, void *argp)
{
	int (* nativeRender)() = (void *)so_symbol(&so_mod, "Java_com_gameloft_android_GAND_GloftELHP_GameRenderer_nativeRender");
     while (1) {
          controls_poll();
          nativeRender();
          gl_swap();
     }
    debugPrintf("RenderUpdate over \n");
	return 0;
}

int main() {
    soloader_init_all();
    // loadConfig();
    int (* JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    JNI_OnLoad(&jvm);
    gl_init();
    //Java_com_gameloft_android_TBFV_GloftELHP_ML_GameRenderer_nativeGetJNIEnv
    //Java_com_gameloft_android_GAND_GloftELHP_GameRenderer_nativeGetJNIEnv
    int (* nativeGetJNIEnv)(void *) = (void *)so_symbol(&so_mod, "Java_com_gameloft_android_GAND_GloftELHP_GameRenderer_nativeGetJNIEnv");
    int (* GLResLoader_nativeInit)() = (void *)so_symbol(&so_mod, "Java_com_gameloft_android_GAND_GloftELHP_GLResLoader_nativeInit");
    int (* GLMediaPlayer_nativeInit)(void *,void *) = (void *)so_symbol(&so_mod, "Java_com_gameloft_android_GAND_GloftELHP_GLMediaPlayer_nativeInit");
	int (* GameRenderer_nativeInit)(void *,int,int) = (void *)so_symbol(&so_mod, "Java_com_gameloft_android_GAND_GloftELHP_GameRenderer_nativeInit");
	int (* EternalLegacy_nativeInit)(void *,char *) = (void *)so_symbol(&so_mod, "Java_com_gameloft_android_GAND_GloftELHP_EternalLegacy_nativeInit");
    int (* nativeResize)(int,int,int,int) = (void *)so_symbol(&so_mod, "Java_com_gameloft_android_GAND_GloftELHP_GameRenderer_nativeResize");
    int (* nativeKeyboardEnabled)(int,int,int) = (void *)so_symbol(&so_mod, "Java_com_gameloft_android_GAND_GloftELHP_EternalLegacy_nativeKeyboardEnabled");
    
    nativeGetJNIEnv(&jni);
    debugPrintf("nativeGetJNIEnv over \n");

    GLResLoader_nativeInit();
    debugPrintf("GLResLoader_nativeInit over \n");

    GLMediaPlayer_nativeInit(&jni,"mClassGLMediaPlayer");
    debugPrintf("GLMediaPlayer_nativeInit over \n");

    GameRenderer_nativeInit(&jni,0,1);
    debugPrintf("GameRenderer_nativeInit over \n");

    EternalLegacy_nativeInit(&jni,"java");
    debugPrintf("EternalLegacy_nativeInit over \n");
    
    //eglSwapInterval(0, 2);

    nativeResize(0,0,SCREEN_W,SCREEN_H);
    debugPrintf("nativeResize over \n");

    // ... do some initialization
    SceUID thid = sceKernelCreateThread("main_thread", (SceKernelThreadEntry)main_thread, 0x40,2* 128 * 1024, 0, 0, NULL);
	sceKernelStartThread(thid, 0, NULL);
	return sceKernelExitDeleteThread(0);
}
