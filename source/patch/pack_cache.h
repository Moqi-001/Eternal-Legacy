#ifndef ETERNAL_PATCH_PACK_CACHE_H
#define ETERNAL_PATCH_PACK_CACHE_H
#include <so_linker/so_linker.h>
/* Install before native initialization. Unsupported code retains original I/O. */
int soloader_patch_pack_cache(so_module *mod);
/* Console-only aggregates and bounded queued slow details. Call at frame end;
 * cache opens/destructors never log or poll in the measured native phase. */
void pack_cache_poll(int force);
/* Call after native game/audio threads stop and BEFORE unloading the SO.
 * Releases cache bytes and archive generation records. Idempotent. */
void pack_cache_shutdown(void);
#endif
