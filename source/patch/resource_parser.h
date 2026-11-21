#ifndef ETERNAL_PATCH_RESOURCE_PARSER_H
#define ETERNAL_PATCH_RESOURCE_PARSER_H

#include <stdint.h>
#include <stdio.h>
#include <so_linker/so_linker.h>

/* Install after diagnostics so fallback seeks retain its instrumentation. */
int soloader_patch_resource_parser(so_module *mod);

#endif
