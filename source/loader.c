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
#include "video_player.h"
#include "utils/perf.h"
#include "java/GLResLoader.h"
#include "patch/presentation.h"
#include "patch/font_cache.h"
#include "patch/pack_cache.h"
#include "patch/shader_cache.h"
#include "utils/native_io.h"
#include "utils/io_trace.h"

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
int        is_vita3k         = 0;

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
    if (_vshKernelSearchModuleByName("kubridge", search_unk) < 0) {
        if (is_unbound_stub(kuKernelMemProtect)) {
            fatal_error("[LOADER] kubridge.skprx is not installed.");
            return -1;
        }
        is_vita3k = 1;
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
        params.msaa              = VITA_MSAA == 4 ? VGL_MSAA_4X : VITA_MSAA == 2 ? VGL_MSAA_2X : VGL_MSAA_NONE;
        params.legacy_pool_size  = 0x100000;  // 开场视频用固定管线画，需要顶点池
        if (!vglInit(&params)) so_logf("[LOADER] vitaGL initialization failed");
        if (presentation_gl_init() < 0) so_logf("[LOADER] presentation initialization failed");
        shader_cache_gl_init();
        opengl_inited = 1;
        perf_gpu_ready();
        so_loga("[PERF] config revision=perf-master-20261121 build=%s screen=%dx%d logical=%dx%d msaa=%d heap_mb=%d console_only=1 diagnostics=%d file_pool_lock=recursive",
#ifdef DEBUG_SOLOADER
                "Debug",
#else
                "Release",
#endif
                SCREEN_W, SCREEN_H, GAME_LOGICAL_W, GAME_LOGICAL_H, VITA_MSAA, MEMORY_NEWLIB_MB, PERF_DIAGNOSTICS);
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

    EternalLegacy_mod = soloader_load_library("EternalLegacy");
    if (soloader_patch_modules() < 0) so_logf("[LOADER] could not install game patches");

    // Java 类注册应在 module 加载后，调用 native 前
    soloader_setup_java();
    soloader_setup_controls();

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
/* Host video draws in physical pixels. Preserve the engine's framebuffer,
 * viewport and scissor state so the next game frame can resume its logical fit. */
static void draw_video_frame(void) {
    GLint framebuffer, viewport[4];
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glDisable(GL_SCISSOR_TEST);
    video_draw();
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

static int run_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    uint64_t init_start = perf_now_us();
    so_loga("[PERF] native_init begin");
    GameRenderer_onSurfaceCreated();
    GameRenderer_onSurfaceChanged(SCREEN_W, SCREEN_H);
    GameGLSurfaceView_onWindowFocusChanged();
    so_loga("[PERF] native_init end us=%llu", (unsigned long long)(perf_now_us() - init_start));

    int video_was_playing = 0;
    while (!EternalLegacy_should_quit()) {
        uint64_t start = perf_now_us();
        uint64_t input_start = perf_now_us();
        soloader_poll_controls();
        uint64_t input_end = perf_now_us();

        if (video_is_playing()) {
            draw_video_frame();
            video_was_playing = 1;
        } else {
            // 视频播完等于安卓上 MyVideoView 结束、切回游戏 Activity：
            // Activity.onResume → onWindowFocusChanged(true) → nativeResume
            if (video_was_playing) {
                video_was_playing = 0;
                video_close();

                EternalLegacy_onResume();
                GameGLSurfaceView_onWindowFocusChanged();
            }
            presentation_frame_begin();
            GameRenderer_onDrawFrame();
            presentation_frame_end();
        }

        uint64_t render_end = perf_now_us();
        uint64_t swap_start = perf_now_us();
        vglSwapBuffers(GL_TRUE);
        uint64_t swap_end = perf_now_us();
        perf_frame(input_end - input_start, render_end - input_end, swap_end - swap_start, swap_end - start);
        perf_poll(0);
        io_trace_poll(0);
        native_io_poll(0);
        font_cache_poll(0);
        pack_cache_poll(0);
        shader_cache_poll(0);
    }

    return 0;
}

int soloader_run() {
    so_logd("[LOADER] soloader_run...");

    soloader_jni_onload(EternalLegacy_mod);

    EternalLegacy_onCreate();
    EternalLegacy_onResume();

    GameGLSurfaceView_onSizeChanged(SCREEN_W, SCREEN_H);

    SceUID thid = sceKernelCreateThread("GLThread", run_thread, 0x10000100, SCREEN_THREAD_STACK, 0, 0, NULL);
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

    video_close();
    GLResLoader_close();
    pack_cache_shutdown();
    font_cache_shutdown();
    shader_cache_shutdown();
    native_io_shutdown();
    native_io_poll(1);
    io_trace_poll(1);
    perf_poll(1);
    so_jni_fini();
    so_linker_fini();

    so_logd("[LOADER] soloader_term success!");
}

int IsRunningInVita3K() {
    return is_vita3k;
}