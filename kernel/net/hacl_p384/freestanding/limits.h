#if defined(PEAK_HOST_TEST)
#include_next <limits.h>
#else
#ifndef PEAK_HACL_LIMITS_H
#define PEAK_HACL_LIMITS_H
#define UINT32_MAX 0xffffffffu
#define UINT64_MAX 0xffffffffffffffffull
#define SIZE_MAX   UINT64_MAX
#define INT32_MAX  0x7fffffff
#define CHAR_BIT   8
#endif
#endif
