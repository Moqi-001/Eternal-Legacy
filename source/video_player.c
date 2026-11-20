/*
 * 开场 mp4 播放（SceAvPlayer）
 *
 * 当前未接入：GLMediaPlayer.loadMovie 直接按"播完"处理，视频留给后续再做
 * —— Vita3K 的 SceAvPlayer 在这条流程之后会让模拟器宿主崩溃（VCRUNTIME140
 * 0xc0000005），需要实机确认或等模拟器修复。
 */
#include <malloc.h>
#include <string.h>

#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <psp2/touch.h>
#include <so_common/so_logger.h>
#include <vitaGL.h>

#include "config.h"
#include "video_player.h"

#define MAX_AUDIO_VOLUME    32767
#define NUM_VIDEO_BUFFERS   2
#define FB_ALIGNMENT        0x100000
#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

enum {
    PLAYER_STOPPED = 0,
    PLAYER_PAUSED,
    PLAYER_PLAYING,
};

static volatile int      player_state                    = PLAYER_STOPPED;
static SceAvPlayerHandle player_handle                   = 0;

// 引擎线程（loadMovie/状态查询）和宿主渲染线程、音频线程都会碰播放器句柄，
// 句柄的读写统一在这把锁里做；等音频线程退出时不能持锁
static pthread_mutex_t   player_mutex                    = PTHREAD_MUTEX_INITIALIZER;
static SceUID            audio_thid                      = -1;
static int               audio_volume                    = MAX_AUDIO_VOLUME;
static GLuint            gl_textures[NUM_VIDEO_BUFFERS]  = {0};
static SceGxmTexture    *gxm_textures[NUM_VIDEO_BUFFERS] = {0};
static int               frame_idx                       = 0;
static GLuint            movie_fs                        = GL_NONE;
static GLuint            movie_vs                        = GL_NONE;
static GLuint            movie_prog                      = GL_NONE;

static const char *movie_v =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texCoord;\n"
    "varying vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "    v_texCoord = a_texCoord;\n"
    "}\n";

static const char *movie_f =
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D tex0;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(tex0, v_texCoord);\n"
    "}\n";

float movie_vertices[] = {
    -1.0f, 1.0f,
    -1.0f, -1.0f,
    1.0f, 1.0f,
    1.0f, -1.0f};

float movie_texcoords[] = {
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 0.0f,
    1.0f, 1.0f};

static void *mem_alloc(void *p, uint32_t align, uint32_t size) {
    return memalign(align, size);
}

static void mem_dealloc(void *p, void *ptr) {
    free(ptr);
}

static void *gpu_alloc(void *p, uint32_t align, uint32_t size) {
    if (align < FB_ALIGNMENT) {
        align = FB_ALIGNMENT;
    }
    size = ALIGN_MEM(size, align);

    SceUID blk = sceKernelAllocMemBlock("av_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, size, NULL);
    if (blk < 0) {
        return NULL;
    }

    void *res;
    sceKernelGetMemBlockBase(blk, &res);
    sceGxmMapMemory(res, size, SCE_GXM_MEMORY_ATTRIB_RW);

    return res;
}

static void gpu_dealloc(void *p, void *addr) {
    glFinish();

    SceUID blk = sceKernelFindMemBlockByAddr(addr, 0);
    sceGxmUnmapMemory(addr);
    sceKernelFreeMemBlock(blk);
}

static int audio_thread(SceSize args, void *argp) {
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        return sceKernelExitDeleteThread(0);
    }

    int vols[] = {audio_volume, audio_volume};
    sceAudioOutSetVolume(port, (SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vols);

    while (player_state != PLAYER_STOPPED) {
        SceAvPlayerFrameInfo frame;
        int                 has_data = 0;

        pthread_mutex_lock(&player_mutex);
        if (player_state == PLAYER_PLAYING && player_handle > 0 && sceAvPlayerIsActive(player_handle)) {
            has_data = sceAvPlayerGetAudioData(player_handle, &frame);
        }
        pthread_mutex_unlock(&player_mutex);

        if (!has_data) {
            sceKernelDelayThread(1000);
            continue;
        }
        sceAudioOutSetConfig(port, -1, frame.details.audio.sampleRate, frame.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
        sceAudioOutOutput(port, frame.pData);
    }

    sceAudioOutReleasePort(port);

    return sceKernelExitDeleteThread(0);
}

int video_open(const char *filepath, float volume) {
    audio_volume = volume * MAX_AUDIO_VOLUME;

    SceAvPlayerInitData init_data;
    memset(&init_data, 0, sizeof(SceAvPlayerInitData));
    init_data.memoryReplacement.allocate          = mem_alloc;
    init_data.memoryReplacement.deallocate        = mem_dealloc;
    init_data.memoryReplacement.allocateTexture   = gpu_alloc;
    init_data.memoryReplacement.deallocateTexture = gpu_dealloc;
    init_data.basePriority                        = 0xA0;
    init_data.numOutputVideoFrameBuffers          = 3;
    init_data.autoStart                           = 1;
    // init_data.debugLevel                          = 3;

    player_handle = sceAvPlayerInit(&init_data);
    if (!player_handle) {
        so_loge("[VIDEO] sceAvPlayerInit failed! error=0x%x", player_handle);
        return -1;
    }
    if (sceAvPlayerAddSource(player_handle, filepath) < 0) {
        so_loge("[VIDEO] sceAvPlayerAddSource failed!");
        return -1;
    }

    glGenTextures(NUM_VIDEO_BUFFERS, gl_textures);
    for (int i = 0; i < NUM_VIDEO_BUFFERS; i++) {
        glBindTexture(GL_TEXTURE_2D, gl_textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        gxm_textures[i] = vglGetGxmTexture(GL_TEXTURE_2D);
        vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
        vglOverloadTexDataPointer(GL_TEXTURE_2D, NULL);
    }

    movie_vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(movie_vs, 1, &movie_v, NULL);
    glCompileShader(movie_vs);
    movie_fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(movie_fs, 1, &movie_f, NULL);
    glCompileShader(movie_fs);

    movie_prog = glCreateProgram();
    glAttachShader(movie_prog, movie_vs);
    glAttachShader(movie_prog, movie_fs);
    glBindAttribLocation(movie_prog, 0, "a_position");
    glBindAttribLocation(movie_prog, 1, "a_texCoord");
    glLinkProgram(movie_prog);
    glUniform1i(glGetUniformLocation(movie_prog, "tex0"), 0);

    player_state = PLAYER_PAUSED;

    audio_thid = sceKernelCreateThread("audio_thread", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (audio_thid < 0) {
        so_loge("[VIDEO] sceKernelCreateThread failed!");
        return -1;
    }
    if (sceKernelStartThread(audio_thid, 0, NULL) < 0) {
        so_loge("[VIDEO] sceKernelStartThread failed!");
        return -1;
    }

    return 0;
}

void video_close() {
    // 先置状态再等音频线程：它看到 STOPPED 就会退出循环，
    // 等线程期间不能持锁，否则它拿不到锁没法退出
    pthread_mutex_lock(&player_mutex);
    player_state = PLAYER_STOPPED;
    pthread_mutex_unlock(&player_mutex);

    if (audio_thid >= 0) {
        sceKernelWaitThreadEnd(audio_thid, NULL, NULL);
    }

    if (player_handle) {
        sceAvPlayerStop(player_handle);
    }

    pthread_mutex_lock(&player_mutex);
    if (player_handle) {
        sceAvPlayerClose(player_handle);
        player_handle = 0;
    }
    pthread_mutex_unlock(&player_mutex);
    audio_thid = -1;

    glDeleteTextures(NUM_VIDEO_BUFFERS, gl_textures);
    glDeleteProgram(movie_prog);
    glDeleteShader(movie_vs);
    glDeleteShader(movie_fs);
    movie_fs   = GL_NONE;
    movie_vs   = GL_NONE;
    movie_prog = GL_NONE;
}

int video_draw() {
    SceAvPlayerFrameInfo frame;
    int                 got_frame = 0;

    pthread_mutex_lock(&player_mutex);
    if (player_state != PLAYER_STOPPED) {
        // 播完就落下状态：调用方下一帧回到游戏画面，Get_logoVideoState 也就成了"播完"
        if (player_handle <= 0 || !sceAvPlayerIsActive(player_handle)) {
            player_state = PLAYER_STOPPED;
        } else {
            // 解码器还在跑但这一帧还没解出来时保持播放状态，等下一帧
            got_frame = sceAvPlayerGetVideoData(player_handle, &frame);
        }
    }
    pthread_mutex_unlock(&player_mutex);

    if (!got_frame) {
        return 0;
    }

    sceGxmTextureInitLinear(
        gxm_textures[frame_idx],
        frame.pData,
        SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
        frame.details.video.width,
        frame.details.video.height,
        0);
    sceGxmTextureSetMinFilter(gxm_textures[frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(gxm_textures[frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(movie_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl_textures[frame_idx]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, &movie_vertices[0]);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, &movie_texcoords[0]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glUseProgram(0);

    frame_idx = (frame_idx + 1) % NUM_VIDEO_BUFFERS;

    return 1;
}

int video_play() {
    pthread_mutex_lock(&player_mutex);
    if (player_handle > 0) {
        player_state = PLAYER_PLAYING;
    }
    pthread_mutex_unlock(&player_mutex);

    return player_handle > 0 ? 0 : -1;
}

int video_pause() {
    pthread_mutex_lock(&player_mutex);
    player_state = PLAYER_PAUSED;
    pthread_mutex_unlock(&player_mutex);

    return 0;
}

int video_is_playing() {
    return player_state == PLAYER_PLAYING;
}
