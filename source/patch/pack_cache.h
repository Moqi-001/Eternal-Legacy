#ifndef ETERNAL_PATCH_PACK_CACHE_H
#define ETERNAL_PATCH_PACK_CACHE_H
#include <so_linker/so_linker.h>
/* Install before native initialization. Unsupported code retains original I/O. */
int soloader_patch_pack_cache(so_module *mod);
/* Console-only aggregate spans; call from the existing periodic diagnostics. */
void pack_cache_poll(int force);
/* Call after native game/audio threads stop and BEFORE unloading the SO.
 * Releases cache bytes and archive generation records. Idempotent. */
void pack_cache_shutdown(void);
#endif
