#ifndef ETERNAL_IO_TRACE_H
#define ETERNAL_IO_TRACE_H
#include <so_linker/so_linker.h>
/* Kernel I/O counters and bounded slow/error details. Stream semantics and
 * lifetime protection belong to native_io/newlib, not this observer. */
#if PERF_DIAGNOSTICS
void io_trace_init(so_module *mod);
void io_trace_poll(int force);
#else
static inline void io_trace_init(so_module *mod) { (void)mod; }
static inline void io_trace_poll(int force) { (void)force; }
#endif
#endif
