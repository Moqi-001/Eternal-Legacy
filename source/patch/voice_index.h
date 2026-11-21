#ifndef ETERNAL_PATCH_VOICE_INDEX_H
#define ETERNAL_PATCH_VOICE_INDEX_H
#include <stddef.h>
#include <so_linker/so_linker.h>
/* Install before native game initialization. Unsupported SOs retain original code.
 * resource_index_init() must run once before the game reaches loading state 14. */
int soloader_patch_voice_index(so_module *mod);
/* Exact current mapped voice only: queue at most 3 valid following voices,
 * looking ahead at most 8 table rows. The queue copies each path. */
void voice_index_prefetch_following(const char *mapped_path);
#endif
