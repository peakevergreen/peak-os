#if defined(PEAK_HOST_TEST)
#include_next <stdlib.h>
/* Linux host libc may omit alloca without extra feature macros. */
#ifndef alloca
#define alloca __builtin_alloca
#endif
#else
#ifndef PEAK_HACL_STDLIB_H
#define PEAK_HACL_STDLIB_H
#include "types.h"
/* Unused on ECDSA verify success path; satisfy KaRaMeL macros. */
static inline void exit(int code) {
    (void)code;
    for (;;)
        ;
}
static inline void *malloc(size_t n) {
    (void)n;
    return NULL;
}
static inline void *calloc(size_t n, size_t s) {
    (void)n;
    (void)s;
    return NULL;
}
static inline void free(void *p) {
    (void)p;
}
#define alloca __builtin_alloca
#endif
#endif
