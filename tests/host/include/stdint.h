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

#endif
