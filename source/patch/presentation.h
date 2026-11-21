#ifndef ETERNAL_PRESENTATION_H
#define ETERNAL_PRESENTATION_H
#include <so_linker/so_linker.h>

/* Initialize GL after vglInit and before nativeInit. */
int soloader_patch_presentation(so_module *mod);
int presentation_gl_init(void);
void presentation_frame_begin(void);
void presentation_frame_end(void);

/* Physical top-origin pixels -> Android logical top-origin pixels. */
int presentation_map_touch(int x, int y, int *logical_x, int *logical_y);
#endif
