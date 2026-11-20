#ifndef SOLOADER_LOADER_H
#define SOLOADER_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <so_linker/so_linker.h>

extern so_module *EternalLegacy_mod;

int  soloader_init();
int  soloader_run();
void soloader_term();

int IsRunningInVita3K();

#ifdef __cplusplus
};
#endif

#endif