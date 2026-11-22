/* Supported SO SHA256 c3555dd862c78d0da3c6ef2505e98a0aca64473d686441b3f75bfb2fe4526212.
 * Physical identities/sizes verified in analysis-0104/io-shader/actual-assets.json.
 * Only compressed file bytes are prepared: native decoding, audio emitters and
 * GL objects retain their original game-thread lifecycle. No missing dash files.
 */
#include "resource_warmup.h"
#include "../config.h"
#include "../utils/native_io.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <so_common/so_logger.h>

static so_module *s_module;
static so_hook_ref s_hook;
static int s_in_loading;
static unsigned s_boundaries, s_ready_boundaries, s_timeout_boundaries, s_failed_boundaries;
static uint64_t s_wait_us, s_max_wait_us, s_report;
static unsigned s_last_ready = ~0u, s_last_pending = ~0u, s_last_failed = ~0u;

static void battle_loading(void *game) {
    int saved_errno = errno;
    if (!s_in_loading) {
        s_in_loading = 1;
        NativeIoWarmStatus status;
        uint64_t start = sceKernelGetProcessTimeWide();
        native_io_warm_status(&status);
        /* Boot normally finishes long before this point. Never wait in Prog,
         * PlayBeat, or a first attack; timeout falls back to original I/O. Both
         * elapsed time and iteration count are bounded, including clock faults. */
        for (unsigned i = 0; status.pending && i < 500 &&
                sceKernelGetProcessTimeWide() - start < 500000; ++i) {
            if (sceKernelDelayThread(1000) < 0) break;
            native_io_warm_status(&status);
        }
        uint64_t elapsed = sceKernelGetProcessTimeWide() - start;
        ++s_boundaries;
        if (status.ready == status.requested && !status.failed && !status.pending) ++s_ready_boundaries;
        if (status.pending) ++s_timeout_boundaries;
        if (status.failed) ++s_failed_boundaries;
        s_wait_us += elapsed;
        if (elapsed > s_max_wait_us) s_max_wait_us = elapsed;
    }
    errno = saved_errno;
    /* GameState::Update reads the pending state byte at +10. The guarded native
     * successful load requests state 11 (BattleScene_Prog); failed native loads
     * request state 8 (Field), both via Set_Child_State. Reset only
     * after this real transition, allowing a single bounded check next battle. */
    unsigned char before = ((const unsigned char *)game)[10];
    void (*original)(void *) = s_hook.orig_func;
    original(game);
    if (((const unsigned char *)game)[10] != before) s_in_loading = 0;
}

void resource_warmup_poll(int force) {
    if (!s_module) return;
    int saved_errno = errno;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (!force && now - s_report < 1000000) { errno = saved_errno; return; }
    s_report = now;
    NativeIoWarmStatus status;
    native_io_warm_status(&status);
    if (force || s_boundaries || status.ready != s_last_ready || status.pending != s_last_pending ||
            status.failed != s_last_failed) {
        so_loga("[WARMUP] set=battle_sfx policy=async_boot_protected state=battle_loading ready=%u/%u pending=%u failed=%u failed_mask=%#x bytes=%u boundaries=%u ready_boundaries=%u timeouts=%u failed_boundaries=%u wait_us=%llu max_wait_us=%llu wait_limit_us=500000 fallback=native",
                status.ready, status.requested, status.pending, status.failed, status.failed_mask, (unsigned)status.ready_bytes,
                s_boundaries, s_ready_boundaries, s_timeout_boundaries, s_failed_boundaries,
                (unsigned long long)s_wait_us, (unsigned long long)s_max_wait_us);
        s_last_ready = status.ready; s_last_pending = status.pending; s_last_failed = status.failed;
        s_boundaries = s_ready_boundaries = s_timeout_boundaries = s_failed_boundaries = 0;
        s_wait_us = s_max_wait_us = 0;
    }
    errno = saved_errno;
}

int soloader_patch_resource_warmup(so_module *mod) {
    if (!mod) return -1;
    if (s_module) return s_module == mod ? 0 : -1;
    intptr_t bias = so_module_get_load_bias(mod);
    static const struct { const char *symbol; uint32_t offset; unsigned char bytes[16]; } symbols[] = {
        {"_ZN9GameState26Update_BattleScene_LoadingEv", 0x24770c,
         {0xf0,0xb5,0x5f,0x46,0x56,0x46,0x4d,0x46,0x44,0x46,0xf0,0xb4,0x69,0x4f,0x6a,0x49}},
        {"_ZN9GameState23Update_BattleScene_ProgEv", 0x236014,
         {0x70,0xb5,0x00,0x21,0x04,0x1c,0xff,0xf7,0xdb,0xf9,0x00,0x28,0x16,0xd0,0x63,0x68}},
        {"_ZN9GameState15Set_Child_StateEa", 0x233454,
         {0x43,0x7a,0x81,0x72,0x03,0x72,0x06,0x29,0x02,0xd1,0x00,0x22,0xc8,0x23,0xc2,0x50}}
    };
    if (so_module_get_load_size(mod) < 0x24f608) goto unsupported;
    for (unsigned i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i)
        if ((uintptr_t)so_module_find_export(mod, symbols[i].symbol) !=
                (uintptr_t)bias + symbols[i].offset + 1 ||
            memcmp((void *)(bias + symbols[i].offset), symbols[i].bytes, 16)) goto unsupported;
    static const unsigned char state_read[] = {0xa5,0x7a,0x2d,0x06,0x2d,0x16};
    static const unsigned char state_exit[] = {0x2b,0x68,0x28,0x1c,0x0b,0x21,0x9b,0x68,0x98,0x47};
    if (memcmp((void *)(bias + 0x24f602), state_read, sizeof(state_read)) ||
        memcmp((void *)(bias + 0x247b8e), state_exit, sizeof(state_exit))) goto unsupported;
    /* so_hook plans/allocates before writing the target. A negative return
     * leaves the original function intact; this optional optimization must not
     * turn that safe refusal into a loader failure. */
    if (so_module_hook_address(mod, 0x24770d, battle_loading, &s_hook) < 0) {
        so_loga("[WARMUP] disabled reason=hook_failure fallback=native");
        return 0;
    }
    if (!s_hook.orig_func) {
        /* Real successful hooks always provide orig_func. If that contract is
         * broken, restore a target that may already have been patched. Only a
         * failed rollback of an installed target remains a fatal error. */
        int rollback = s_hook.target ? so_unhook(&s_hook) : 0;
        so_loga("[WARMUP] disabled reason=missing_original rollback=%d fallback=native", rollback);
        return rollback < 0 ? -1 : 0;
    }
    static const NativeIoWarmResource resources[] = {
        {GAME_DATA_DIR "file001634.dat", 21564}, /* twohandsword impact_metal_1 */
        {GAME_DATA_DIR "file000834.dat", 20540}, /* blunt impact_flesh_1 */
        {GAME_DATA_DIR "file000257.dat", 7228},  /* blunt swoosh_1 */
        {GAME_DATA_DIR "file002023.dat", 8252},  /* blunt swoosh_2 */
        {GAME_DATA_DIR "file001783.dat", 16444}, /* soldier death */
        {GAME_DATA_DIR "file000271.dat", 83004}, /* massheal impact */
        {GAME_DATA_DIR "file000542.dat", 11324}, /* astrian attack */
        {GAME_DATA_DIR "file002181.dat", 6204},  /* soldier hurt */
        {GAME_DATA_DIR "file002592.dat", 11324}  /* astrian hurt */
    };
    if (native_io_warm_start(resources, sizeof(resources) / sizeof(resources[0])) < 0) {
        so_unhook(&s_hook);
        so_loga("[WARMUP] disabled reason=warm_set_unavailable fallback=native");
        return 0;
    }
    s_module = mod;
    so_loga("[WARMUP] installed supported_so=c3555dd862c78d0d files=9 bytes=185884 ids_in_mask_order=1634/834/257/2023/1783/271/542/2181/2592 worker=1 boot_wait=0 boundary_wait_limit_us=500000 protected_within_cache=1");
    return 0;
unsupported:
    so_loga("[WARMUP] disabled reason=unsupported_so_targets fallback=native");
    return 0;
}
