/*
 * 补丁总入口
 *
 * 结构约定：
 *   patch.c/.h      —— 总入口，只对外暴露 soloader_patch_modules()
 *   patch/<模块>.c  —— 各 .so 真正的补丁实现，模块指针由这里传入
 */
#include <psp2/io/stat.h>

#include "config.h"
#include "loader.h"
#include "patch.h"
#include "patch/paths.h"
#include "patch/diagnostics.h"
#include "patch/resource_parser.h"
#include "patch/voice_index.h"
#include "patch/presentation.h"
#include "patch/font_cache.h"
#include "patch/pack_cache.h"
#include "patch/shader_cache.h"
#include "utils/resource_index.h"
#include "utils/io_trace.h"
#include <so_common/so_logger.h>

int soloader_patch_modules() {
    io_trace_init(EternalLegacy_mod);
    if (resource_index_init() < 0)
        so_loga("[IO] inventory unavailable; original filesystem behavior retained");
    // 引擎按自己写死的安卓路径找资源和库文件
    if (soloader_patch_paths(EternalLegacy_mod) < 0 ||
        soloader_patch_diagnostics(EternalLegacy_mod) < 0 ||
        soloader_patch_voice_index(EternalLegacy_mod) < 0 ||
        soloader_patch_resource_parser(EternalLegacy_mod) < 0 ||
        soloader_patch_presentation(EternalLegacy_mod) < 0 ||
        soloader_patch_font_cache(EternalLegacy_mod) < 0 ||
        soloader_patch_pack_cache(EternalLegacy_mod) < 0 ||
        soloader_patch_shader_cache(EternalLegacy_mod) < 0)
        return -1;

    // 可写目录（存档走 /data/data/<包名>/ → files/）
    sceIoMkdir(DATA_PATH "files", 0777);

    return 0;
}
