#ifndef ETERNAL_RESOURCE_WARMUP_H
#define ETERNAL_RESOURCE_WARMUP_H
#include <stddef.h>
#include <so_linker/so_linker.h>
/* After inventory init, before native initialization. One small async boot set;
 * one <=500ms readiness check at each noninteractive BattleScene_Loading entry.
 * Unsupported symbols/instructions never acquire native object offsets/hooks. */
int soloader_patch_resource_warmup(so_module *mod);
/* Main thread, completed-frame diagnostics only. No worker console output. */
void resource_warmup_poll(int force);
#endif
