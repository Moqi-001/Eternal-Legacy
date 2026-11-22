/* Native frame and child spans, published only after complete game frames.
 * Counters belong to the GL thread; worker calls use the original functions
 * without span timers or logging. The scope check uses the runtime's TLS. */
#include "frame_profile.h"

#if PERF_DIAGNOSTICS
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <so_common/so_logger.h>
#include "utils/perf.h"

enum {
    GAME_UPDATE, GAME_RENDER, GAME_SLEEP, GAME_FLUSH,
    UPDATE_SOUND, UPDATE_MENU, UPDATE_SCRIPT, UPDATE_OBJECT, UPDATE_EFFECT, UPDATE_WORLD, UPDATE_BATTLE,
    RENDER_SCENE, RENDER_GUI, MRT_PRE, MRT_POST, SCENE_REGISTER, SCENE_LISTS, SCENE_SHADOW, SKIN, MODULAR_SKIN,
    SPAN_COUNT
};
typedef struct {
    uint64_t us, max_us, requested;
    unsigned calls, histogram[5];
    uint64_t self_us;
} Span;
typedef struct {
    Span spans[SPAN_COUNT];
    uint64_t start, native_us;
    unsigned frames;
} Window;
typedef struct { unsigned op; uint64_t start, children; } StackEntry;
static _Thread_local struct {
    Span spans[SPAN_COUNT]; uint64_t start; int active;
    StackEntry stack[32]; unsigned depth, recursion[SPAN_COUNT];
    uint64_t logger_us;
} s_frame;
static Window s_window;
static so_hook_ref s_hooks[SPAN_COUNT];
static so_module *s_module;
static unsigned s_sequence;
typedef struct {
    unsigned sequence; uint64_t native_us, logger_us, times[SPAN_COUNT];
} SlowFrame;
static SlowFrame sSlow[8];
static unsigned sSlowCount, sSlowDropped;
static const char *const sNames[] = {
    "game_update", "game_render", "usleep", "glFlush", "update_sound", "update_menu", "update_script",
    "update_object", "update_effect", "update_world", "update_battle", "render_scene", "render_gui",
    "mrt_pre", "mrt_post", "scene_register", "scene_lists", "scene_shadow", "skin", "modular_skin"
};
typedef struct { int tracked; unsigned op, index; } ProfileToken;

static ProfileToken profile_enter(unsigned op) {
    ProfileToken token = {0, op, 0};
    if (!s_frame.active) return token;
    ++s_frame.recursion[op];
    if (s_frame.recursion[op] > 1 || s_frame.depth == 32) return token;
    token.tracked = 1; token.index = s_frame.depth++;
    s_frame.stack[token.index] = (StackEntry){op, perf_now_us(), 0};
    return token;
}
static void profile_leave(ProfileToken token, unsigned requested) {
    if (!s_frame.active) return;
    if (s_frame.recursion[token.op]) --s_frame.recursion[token.op];
    if (!token.tracked) return;
    int saved_errno = errno;
    StackEntry *entry = &s_frame.stack[token.index];
    uint64_t elapsed = perf_now_us() - entry->start;
    Span *span = &s_frame.spans[token.op];
    ++span->calls;
    span->us += elapsed;
    span->requested += requested;
    span->self_us += elapsed >= entry->children ? elapsed - entry->children : 0;
    if (elapsed > span->max_us) span->max_us = elapsed;
    unsigned bucket = elapsed <= 17000 ? 0 : elapsed <= 25000 ? 1 :
                      elapsed <= 34000 ? 2 : elapsed <= 50000 ? 3 : 4;
    ++span->histogram[bucket];
    s_frame.depth = token.index;
    if (s_frame.depth) s_frame.stack[s_frame.depth - 1].children += elapsed;
    errno = saved_errno;
}
void frame_profile_log_cost(uint64_t elapsed) {
    if (s_module && s_frame.active) s_frame.logger_us += elapsed;
}

static void game_update(void *state) {
    void (*original)(void *) = s_hooks[GAME_UPDATE].orig_func;
    if (!s_frame.active) { original(state); return; }
    ProfileToken token = profile_enter(GAME_UPDATE);
    original(state);
    profile_leave(token, 0);
}
static void game_render(void *state) {
    void (*original)(void *) = s_hooks[GAME_RENDER].orig_func;
    if (!s_frame.active) { original(state); return; }
    ProfileToken token = profile_enter(GAME_RENDER);
    original(state);
    profile_leave(token, 0);
}
static int game_sleep(useconds_t requested) {
    int (*original)(useconds_t) = s_hooks[GAME_SLEEP].orig_func;
    if (!s_frame.active) return original(requested);
    ProfileToken token = profile_enter(GAME_SLEEP);
    int result = original(requested);
    profile_leave(token, requested);
    return result;
}
static void game_flush(void) {
    void (*original)(void) = s_hooks[GAME_FLUSH].orig_func;
    if (!s_frame.active) { original(); return; }
    ProfileToken token = profile_enter(GAME_FLUSH);
    original();
    profile_leave(token, 0);
}

void frame_profile_begin(void) {
    if (!s_module) return;
    int saved_errno = errno;
    memset(&s_frame, 0, sizeof(s_frame));
    s_frame.start = perf_now_us();
    if (!s_window.start) s_window.start = s_frame.start;
    s_frame.active = 1;
    errno = saved_errno;
}
void frame_profile_end(void) {
    if (!s_frame.active) return;
    int saved_errno = errno;
    s_frame.active = 0;
    uint64_t native_us = perf_now_us() - s_frame.start;
    s_window.native_us += native_us;
    ++s_window.frames;
    ++s_sequence;
    if (native_us >= 80000 || s_frame.spans[GAME_UPDATE].max_us >= 50000) {
        if (sSlowCount < sizeof(sSlow)/sizeof(sSlow[0])) {
            SlowFrame *slow = &sSlow[sSlowCount++];
            slow->sequence = s_sequence; slow->native_us = native_us; slow->logger_us = s_frame.logger_us;
            for (unsigned op = 0; op < SPAN_COUNT; ++op) slow->times[op] = s_frame.spans[op].us;
        } else ++sSlowDropped;
    }
    for (unsigned op = 0; op < SPAN_COUNT; ++op) {
        Span *out = &s_window.spans[op], *in = &s_frame.spans[op];
        out->calls += in->calls;
        out->us += in->us;
        out->requested += in->requested;
        out->self_us += in->self_us;
        if (in->max_us > out->max_us) out->max_us = in->max_us;
        for (unsigned i = 0; i < 5; ++i) out->histogram[i] += in->histogram[i];
    }
    errno = saved_errno;
}
void frame_profile_poll(int force) {
    if (!s_window.frames || s_frame.active) return;
    int saved_errno = errno;
    uint64_t elapsed = perf_now_us() - s_window.start;
    if (!force && elapsed < 1000000) { errno = saved_errno; return; }
    so_loga("[FRAME] phase=game first=%u last=%u frames=%u window_us=%llu native_us=%llu inclusive=1",
            s_sequence - s_window.frames + 1, s_sequence, s_window.frames,
            (unsigned long long)elapsed, (unsigned long long)s_window.native_us);
    for (unsigned op = 0; op < SPAN_COUNT; ++op) {
        Span *span = &s_window.spans[op];
        if (op >= 4 && !span->calls) continue;
        so_loga("[FRAME] op=%s calls=%u total_us=%llu max_us=%llu avg_us=%llu requested_us=%llu hist_17_25_34_50_over=%u/%u/%u/%u/%u self_us=%llu",
                sNames[op], span->calls, (unsigned long long)span->us,
                (unsigned long long)span->max_us,
                (unsigned long long)(span->calls ? span->us / span->calls : 0),
                (unsigned long long)span->requested, span->histogram[0], span->histogram[1],
                span->histogram[2], span->histogram[3], span->histogram[4], (unsigned long long)span->self_us);
    }
    for (unsigned i = 0; i < sSlowCount; ++i) {
        SlowFrame *slow = &sSlow[i];
        unsigned hot = 4;
        for (unsigned op = 5; op < SPAN_COUNT; ++op) if (slow->times[op] > slow->times[hot]) hot = op;
        so_loga("[FRAME_SLOW] seq=%u native_us=%llu update_us=%llu render_us=%llu sleep_us=%llu flush_us=%llu logger_us=%llu hottest=%s hottest_us=%llu scene_us=%llu gui_us=%llu mrt_pre_us=%llu mrt_post_us=%llu",
                slow->sequence, (unsigned long long)slow->native_us,
                (unsigned long long)slow->times[GAME_UPDATE], (unsigned long long)slow->times[GAME_RENDER],
                (unsigned long long)slow->times[GAME_SLEEP], (unsigned long long)slow->times[GAME_FLUSH],
                (unsigned long long)slow->logger_us, sNames[hot], (unsigned long long)slow->times[hot],
                (unsigned long long)slow->times[RENDER_SCENE], (unsigned long long)slow->times[RENDER_GUI],
                (unsigned long long)slow->times[MRT_PRE], (unsigned long long)slow->times[MRT_POST]);
    }
    if (sSlowDropped) so_loga("[FRAME_SLOW] dropped=%u capacity=8", sSlowDropped);
    sSlowCount = sSlowDropped = 0;
    memset(&s_window, 0, sizeof(s_window));
    errno = saved_errno;
}

/* Child boundaries use the base ARM PCS. These native methods take this and
 * at most one 32-bit argument, with no FP-register or 64-bit return ABI. Preserve
 * the original r0 result even for void methods; callers may ignore it. */
#define PROFILE_ONE(name, op) \
static uintptr_t name(void *object) { \
    uintptr_t (*original)(void *) = s_hooks[op].orig_func; \
    ProfileToken token = profile_enter(op); \
    uintptr_t result = original(object); profile_leave(token, 0); return result; \
}
#define PROFILE_TWO(name, op) \
static uintptr_t name(void *object, uintptr_t argument) { \
    uintptr_t (*original)(void *, uintptr_t) = s_hooks[op].orig_func; \
    ProfileToken token = profile_enter(op); \
    uintptr_t result = original(object, argument); profile_leave(token, 0); return result; \
}
PROFILE_TWO(update_sound, UPDATE_SOUND)
PROFILE_TWO(update_menu, UPDATE_MENU)
PROFILE_TWO(update_script, UPDATE_SCRIPT)
PROFILE_TWO(update_object, UPDATE_OBJECT)
PROFILE_TWO(update_effect, UPDATE_EFFECT)
PROFILE_TWO(update_world, UPDATE_WORLD)
PROFILE_TWO(update_battle, UPDATE_BATTLE)
PROFILE_TWO(render_scene, RENDER_SCENE)
PROFILE_ONE(render_gui, RENDER_GUI)
PROFILE_ONE(mrt_pre, MRT_PRE)
PROFILE_ONE(mrt_post, MRT_POST)
PROFILE_TWO(scene_register, SCENE_REGISTER)
PROFILE_TWO(scene_lists, SCENE_LISTS)
PROFILE_ONE(scene_shadow, SCENE_SHADOW)
PROFILE_TWO(skin, SKIN)
PROFILE_TWO(modular_skin, MODULAR_SKIN)
#undef PROFILE_ONE
#undef PROFILE_TWO
static const struct {
    unsigned op; const char *symbol; uint32_t offset;
    unsigned char signature[16]; void *replacement;
} sChildren[] = {
    {UPDATE_SOUND, "_ZN15VoxSoundManager6UpdateEi", 0x251360,
     {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x56,0x49,0x57,0x4a}, update_sound},
    {UPDATE_MENU, "_ZN7MenuMgr6UpdateEi", 0x2b44a8,
     {0xf8,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x83,0x6a,0x81,0x46}, update_menu},
    {UPDATE_SCRIPT, "_ZN14CScriptManager6UpdateEi", 0x2f6a08,
     {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0xc2,0x4a,0x04,0x1c}, update_script},
    {UPDATE_OBJECT, "_ZN9GameState12UpdateObjectEj", 0x243d40,
     {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x98,0x4d,0x06,0x1c}, update_object},
    {UPDATE_EFFECT, "_ZN14CEffectManager6UpdateEi", 0x2eccdc,
     {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0xbb,0xb0,0x00,0x23}, update_effect},
    {UPDATE_WORLD, "_ZN16CWorldMapManager11UpdateWorldEi", 0x2f08e0,
     {0xf8,0xb5,0xc4,0x68,0x06,0x1c,0x0f,0x1c,0x05,0x1d,0xac,0x42,0x0f,0xd0,0x20,0x69}, update_world},
    {UPDATE_BATTLE, "_ZN14CBattleManager6UpdateEj", 0x2909e8,
     {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0xc4,0x4a,0x91,0xb0}, update_battle},
    {RENDER_SCENE, "_ZN6glitch5scene13CSceneManager7drawAllEPNS0_10ISceneNodeE", 0x43ce48,
     {0x70,0xb5,0x03,0x68,0x04,0x1c,0x0d,0x1c,0x1b,0x6c,0x81,0x69,0x98,0x47,0x23,0x68}, render_scene},
    {RENDER_GUI, "_ZN6glitch3gui15CGUIEnvironment7drawAllEv", 0x463894,
     {0x70,0xb5,0xd4,0x23,0x5b,0x00,0xc3,0x58,0x05,0x1c,0x04,0x1c,0x08,0x35,0x00,0x2b}, render_gui},
    {MRT_PRE, "_ZN10MrtManager7PreDrawEv", 0x254ef0,
     {0x10,0xb5,0x43,0x6d,0x01,0x1c,0x18,0x31,0x5a,0x69,0x8c,0x23,0x14,0x68,0x10,0x1c}, mrt_pre},
    {MRT_POST, "_ZN10MrtManager8PostDrawEv", 0x255480,
     {0xf0,0xb5,0x3a,0x4c,0x85,0xb0,0x05,0x1c,0x7c,0x44,0xa3,0x69,0xda,0x07,0x44,0xd5}, mrt_post},
    {SCENE_REGISTER, "_ZN6glitch5scene13CSceneManager18registerSceneNodesEPNS0_10ISceneNodeE", 0x43d2dc,
     {0xf8,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x83,0x46,0x0c,0x1e}, scene_register},
    {SCENE_LISTS, "_ZN6glitch5scene13CSceneManager11renderListsEPNS_5video12IVideoDriverE", 0x441d60,
     {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x90,0x4a,0xab,0xb0}, scene_lists},
    {SCENE_SHADOW, "_ZN6glitch5scene13CSceneManager19drawShadowReceiversEv", 0x43d6f8,
     {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x90,0x25,0x94,0x24}, scene_shadow},
    {SKIN, "_ZN6glitch7collada12CSkinnedMesh4skinEj", 0x40ebd0,
     {0xf0,0xb5,0x57,0x46,0x4e,0x46,0x45,0x46,0xe0,0xb4,0x8e,0x00,0x76,0x18,0x0f,0x1c}, skin},
    {MODULAR_SKIN, "_ZN6glitch7collada19CModularSkinnedMesh4skinEj", 0x3fca18,
     {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x04,0x6b,0x49,0x01}, modular_skin},
};

int soloader_patch_frame_profile(so_module *mod) {
    if (!mod) return -1;
    if (s_module) return s_module == mod ? 0 : -1;
    intptr_t bias = so_module_get_load_bias(mod);
    static const struct {
        const char *symbol;
        uint32_t offset;
        unsigned char signature[16];
        void *replacement;
    } targets[] = {
        {"_ZN9GameState6UpdateEv", 0x24f5e4,
         {0xf0,0xb5,0x47,0x46,0x80,0xb4,0xc7,0x4b,0xc7,0x4a,0x04,0x1c,0x7b,0x44,0x9b,0x58}, game_update},
        {"_ZN9GameState6RenderEv", 0x235ea4,
         {0x10,0xb5,0x43,0x7b,0x04,0x1c,0x00,0x2b,0x03,0xd1,0x40,0x68,0xc3,0x69,0x00,0x2b}, game_render},
    };
    if (so_module_get_load_size(mod) < 0x24f5f4) goto unsupported;
    for (unsigned i = 0; i < 2; ++i)
        if ((uintptr_t)so_module_find_export(mod, targets[i].symbol) !=
                (uintptr_t)bias + targets[i].offset + 1 ||
            memcmp((void *)(bias + targets[i].offset), targets[i].signature, 16)) goto unsupported;
    if (!so_module_find_import(mod, "usleep") || !so_module_find_import(mod, "glFlush")) goto unsupported;
    unsigned installed = 0;
    for (; installed < 2; ++installed) {
        int result = so_module_hook_address(mod, targets[installed].offset | 1,
                                            targets[installed].replacement, &s_hooks[installed]);
        if (result < 0) goto failed;
        if (!s_hooks[installed].orig_func) { ++installed; goto failed; }
    }
    const char *imports[] = {"usleep", "glFlush"};
    void *replacements[] = {game_sleep, game_flush};
    for (; installed < 4; ++installed) {
        int result = so_module_hook_import(mod, imports[installed - 2], replacements[installed - 2], &s_hooks[installed]);
        if (result < 0) goto failed;
        if (!s_hooks[installed].orig_func) { ++installed; goto failed; }
    }
    unsigned children = 0;
    for (unsigned i = 0; i < sizeof(sChildren)/sizeof(sChildren[0]); ++i) {
        unsigned op = sChildren[i].op;
        if (so_module_get_load_size(mod) < sChildren[i].offset + 16 ||
            (uintptr_t)so_module_find_export(mod, sChildren[i].symbol) !=
                (uintptr_t)bias + sChildren[i].offset + 1 ||
            memcmp((void *)(bias + sChildren[i].offset), sChildren[i].signature, 16)) continue;
        int result = so_module_hook_address(mod, sChildren[i].offset | 1,
                                            sChildren[i].replacement, &s_hooks[op]);
        if (result < 0 || !s_hooks[op].orig_func) {
            if (!result) so_unhook(&s_hooks[op]);
            so_loga("[FRAME] child_disabled op=%s reason=hook_failure", sNames[op]);
        } else ++children;
    }
    s_module = mod;
    so_loga("[FRAME] installed supported_so=c3555dd862c78d0d spans=4 children=%u game_scope=1 completed_frames=1 inclusive=1 self_time=1 console_only=1", children);
    return 0;
failed:
    while (installed) so_unhook(&s_hooks[--installed]);
    so_loga("[FRAME] disabled reason=hook_failure (original game calls restored)");
    return 0;
unsupported:
    so_loga("[FRAME] disabled reason=unsupported_so_targets");
    return 0;
}
#endif
