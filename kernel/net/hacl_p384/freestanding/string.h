/* Freestanding shim so HACL* sources can #include <string.h>. */
#if defined(PEAK_HOST_TEST)
#include_next <string.h>
#else
#ifndef PEAK_HACL_STRING_SHIM_H
#define PEAK_HACL_STRING_SHIM_H
#include "util.h"
static inline void bzero(void *p, size_t n) {
    memset(p, 0, n);
}
#endif
#endif
