#if defined(PEAK_HOST_TEST)
#include_next <stdio.h>
#else
#ifndef PEAK_HACL_STDIO_H
#define PEAK_HACL_STDIO_H
#include "types.h"
/* HACL headers typedef FILE*; printf only used in abort macros. */
static inline int printf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}
static inline int fprintf(void *stream, const char *fmt, ...) {
    (void)stream;
    (void)fmt;
    return 0;
}
#endif
#endif
