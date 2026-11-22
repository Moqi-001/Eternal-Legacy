#ifndef ETERNAL_FRAME_PROFILE_H
#define ETERNAL_FRAME_PROFILE_H

#include <stddef.h>
#include <stdint.h>
#include <so_linker/so_linker.h>

#if PERF_DIAGNOSTICS
int soloader_patch_frame_profile(so_module *mod);
/* The GL thread brackets game frames only; video and worker calls are excluded.
 * Poll after the completed frame, never from an inner measured operation. */
void frame_profile_begin(void);
void frame_profile_end(void);
void frame_profile_poll(int force);
void frame_profile_log_cost(uint64_t elapsed);
#else
static inline int soloader_patch_frame_profile(so_module *mod) { (void)mod; return 0; }
static inline void frame_profile_begin(void) {}
static inline void frame_profile_end(void) {}
static inline void frame_profile_poll(int force) { (void)force; }
static inline void frame_profile_log_cost(uint64_t elapsed) { (void)elapsed; }
#endif

#endif
