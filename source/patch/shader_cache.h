#ifndef ETERNAL_SHADER_CACHE_H
#define ETERNAL_SHADER_CACHE_H
#include <stddef.h>
#include <so_linker/so_linker.h>
int soloader_patch_shader_cache(so_module *mod);
int shader_cache_gl_init(void);
void shader_cache_poll(int force);
void shader_cache_shutdown(void);
#endif
