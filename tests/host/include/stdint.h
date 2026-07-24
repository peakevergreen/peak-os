#ifndef PEAK_HOST_STDINT_H
#define PEAK_HOST_STDINT_H

/* Prefer this over kernel/include/stdint.h during host JS tests. */
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long      uintptr_t;
typedef long               intptr_t;
typedef long long          intmax_t;
typedef unsigned long long uintmax_t;

#define INT8_MAX    127
#define UINT8_MAX   255u
#define INT16_MAX   32767
#define UINT16_MAX  65535u
#define INT32_MAX   2147483647
#define UINT32_MAX  4294967295u
#define INT64_MAX   9223372036854775807LL
#define UINT64_MAX  18446744073709551615ULL
#define INTMAX_MAX  INT64_MAX
#define UINTMAX_MAX UINT64_MAX
#define SIZE_MAX    UINT64_MAX
#define PTRDIFF_MAX INT64_MAX

#endif
