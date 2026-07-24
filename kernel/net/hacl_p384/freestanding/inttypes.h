/* Peak freestanding shim for HACL* — host: always include_next (no guard)
 * so nested system headers can re-enter and get real libc types. */
#if defined(PEAK_HOST_TEST)
#include_next <inttypes.h>
#else
#ifndef PEAK_HACL_INTTYPES_H
#define PEAK_HACL_INTTYPES_H
#include "types.h"
#endif
#endif
