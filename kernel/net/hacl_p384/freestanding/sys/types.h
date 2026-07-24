/* Freestanding shim for lib_intrinsics.h. */
#if defined(PEAK_HOST_TEST)
#include_next <sys/types.h>
#else
#ifndef PEAK_HACL_SYS_TYPES_H
#define PEAK_HACL_SYS_TYPES_H
#include "types.h"
#endif
#endif
