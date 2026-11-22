#ifndef ETERNAL_PATCH_FLOAT_MATH_H
#define ETERNAL_PATCH_FLOAT_MATH_H

#include <stddef.h>
#include <so_linker/so_linker.h>

/* Install exact base-PCS VFP adapters for this module's software FP imports.
 * Exceptional inputs and non-default FP control modes retain original helpers.
 */
int soloader_patch_float_math(so_module *mod);

#endif
