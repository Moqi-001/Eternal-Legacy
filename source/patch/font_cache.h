#ifndef ETERNAL_FONT_CACHE_H
#define ETERNAL_FONT_CACHE_H
#include <stddef.h>
#include <so_linker/so_linker.h>

/* Install before native initialization. Unknown ELF layouts stay unmodified. */
int soloader_patch_font_cache(so_module *mod);
void font_cache_poll(int force);
/* Idle blobs are retained for reuse within the fixed 12 MiB process budget.
 * Stops new cache users; live faces keep their byte pins until native destruction.
 * Call after game threads stop, before unloading the SO. Does not destroy faces. */
void font_cache_shutdown(void);
#endif
