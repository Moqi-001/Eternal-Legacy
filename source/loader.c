#include <stdio.h>
#include <string.h>

#include <kubridge.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/vshbridge.h>
#include <so_common/so_logger.h>
#include <so_jni/so_jni.h>
#include <so_linker/so_linker.h>
#include <vitaGL.h>

#include "config.h"
#include "controls.h"
#include "java.h"
#include "loader.h"
#include "patch.h"
#include "utils/dialog.h"
#include "utils/utils.h"

extern so_library liblog_so;
extern so_library libz_so;
extern so_library libm_so;
extern so_library libdl_so;
extern so_library libEGL_so;
extern so_library libGLESv1_CM_so;
extern so_library libGLESv2_so;
extern so_library libandroid_so;
extern so_library libc_so;
extern so_library libstdcxx_so;

so_module *EternalLegacy_mod = NULL;

/* 为了兼容vita3k。vita3k不支持kubridge插件，但它有kuKernelMemProtect等函数实现。
 * 当判断插件未加载时再判断这个函数是否有绑定，这样的判断可实现对实机和vita3k都通用的检测。*/
static int is_unbound_stub(const void *fn) {
    if (!fn) {
        return 1;
    }

    const uint32_t *stub = (const uint32_t *)((uintptr_t)fn & ~(uintptr_t)1);
    return (stub[0] == 0xE24FC008u && stub[1] == 0xE3A0F000u);
}

static int check_kubridge() {
    int search_unk[2];
    if (_vshKernelSearchModuleByName("kubridge", search_unk) < 0 && is_unbound_stub(kuKernelMemProtect)) {
        fatal_error("[LOADER] kubridge.skprx is not installed.");
        return -1;
    }
    return 0;
}

static int check_opengl() {
    if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx")) {
        fatal_error(
            "Error: libshacccg.suprx is not installed. "
            "Google \"ShaRKBR33D\" for quick installation.");
        return -1;
    }
    return 0;
}

static int soloader_init_opengl() {
    static int opengl_inited = 0;
    if (!opengl_inited) {
        vglInitParams params     = {0};
        params.width             = SCREEN_W;
        params.height            = SCREEN_H;
        params.ram_threshold     = VITAGL_RAM_THRESHOLD_MB * 1024 * 1024;
        params.cdram_threshold   = VITAGL_CDRAM_THRESHOLD_MB * 1024 * 1024;
        params.phycont_threshold = VITAGL_PHYCONT_THRESHOLD_MB * 1024 * 1024;
        params.cdlg_threshold    = VITAGL_CDIALOG_THRESHOLD_MB * 1024 * 1024;
        params.msaa              = VGL_MSAA_4X;
        params.legacy_pool_size  = 0x100000;  // 开场视频用固定管线画，需要顶点池
        vglInit(&params);
        opengl_inited = 1;
    }

    return 0;
}

static int soloader_setup_modules() {
    so_module_create_virtual(&liblog_so);
    so_module_create_virtual(&libz_so);
    so_module_create_virtual(&libm_so);
    so_module_create_virtual(&libdl_so);
    so_module_create_virtual(&libEGL_so);
    so_module_create_virtual(&libGLESv1_CM_so);
    so_module_create_virtual(&libGLESv2_so);
    so_module_create_virtual(&libandroid_so);
    so_module_create_virtual(&libc_so);
    so_module_create_virtual(&libstdcxx_so);

    return 0;
}

so_module *soloader_load_library(const char *soname) {
    char path[1024];
    snprintf(path, 1024, "%slib%s.so", SO_LIB_DIR, soname);

    so_module *mod = so_module_open(path);
    if (!mod) {
        so_logf("Error: could not load %s.", path);
        return NULL;
    }

    return mod;
}

static int soloader_jni_onload(so_module *mod) {
    int (*JNI_OnLoad)(JavaVM *, void *) = so_module_find_export(mod, "JNI_OnLoad");
    if (!JNI_OnLoad) {
        return 0;
    }

    int result;
    ENTER_JNI_CALL(0)
    result = JNI_OnLoad(&_java_vm, NULL);
    EXIT_JNI_CALL(NULL)

    so_logd("[LOADER] JNI_OnLoad: %d ('%s')", result, so_module_get_name(mod));
    return result;
}

int soloader_init() {
    so_logd("[LOADER] soloader_init...");

    check_kubridge();
    check_opengl();
    so_linker_init();
    so_jni_init();

    soloader_setup_modules();
    controls_create();

    // 只加载游戏库。StormGLOFT 是改包方塞进来的钩子库（JNI_OnLoad 里把 dlopen
    // 的句柄当 bionic soinfo 去改游戏的 GL GOT 槽），不加载它也能跑
    EternalLegacy_mod = soloader_load_library("EternalLegacy");

    // Java 类要在 native 被调用前注册好；native 绑定依赖已重定位的模块
    soloader_setup_java(EternalLegacy_mod);
    soloader_patch_modules();

    soloader_init_opengl();

    so_logd("[LOADER] soloader_init success!");

    return 0;
}

/*
 * 启动顺序照抄 Java 侧：
 *   static 块 loadLibrary ×2（JNI_OnLoad）→ onCreate（nativeSetPhone、
 *   Device.init）→ onResume → GLSurfaceView 的尺寸/表面回调 → 每帧 onDrawFrame。
 * GLSurfaceView 的 GL 线程在 onDrawFrame 之后自己 swap，宿主就是那个线程。
 */
/*
 * 渲染线程：对应安卓 GLSurfaceView 的 GL 线程。
 *
 * 引擎在 nativeInit 里会开 256 KB 的栈缓冲区（ALicenseCheck::LoadConfig），
 * 跑在 256 KB 的主线程栈上会溢出；安卓那边这段在 GL 线程里跑，默认 1 MB。
 */
static int render_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    GameRenderer_onSurfaceCreated();
    GameRenderer_onSurfaceChanged(SCREEN_W, SCREEN_H);
    GameGLSurfaceView_onWindowFocusChanged();

    while (!EternalLegacy_should_quit()) {
        controls_poll();
        GameRenderer_onDrawFrame();
        vglSwapBuffers(GL_TRUE);
    }

    return 0;
}

int soloader_run() {
    so_logd("[LOADER] soloader_run...");

    soloader_jni_onload(EternalLegacy_mod);

    EternalLegacy_onCreate();
    EternalLegacy_onResume();

    GameGLSurfaceView_onSizeChanged(SCREEN_W, SCREEN_H);

    SceUID thid = sceKernelCreateThread("GLThread", render_thread, 0x10000100, SCREEN_THREAD_STACK,
                                        0, 0, NULL);
    if (thid < 0) {
        so_logf("[LOADER] failed to create render thread: %#x", thid);
        return -1;
    }

    sceKernelStartThread(thid, 0, NULL);
    sceKernelWaitThreadEnd(thid, NULL, NULL);
    sceKernelDeleteThread(thid);

    GameRenderer_onSurfaceDestroyed();
    EternalLegacy_onDestroy();

    so_logd("[LOADER] soloader_run over.");
    return 0;
}

void soloader_term() {
    so_logd("[LOADER] soloader_term...");

    so_jni_fini();
    so_linker_fini();

    so_logd("[LOADER] soloader_term success!");
}
