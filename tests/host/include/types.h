#ifndef PEAK_TYPES_H
#define PEAK_TYPES_H

#include <stdint.h>

/* Match Darwin LP64 so we can link libc string/stdio helpers. */
typedef unsigned long size_t;
typedef long          ssize_t;
typedef unsigned long uintptr_t;
typedef long          intptr_t;

#ifndef NULL
#define NULL ((void *)0)
#endif
#define true  1
#define false 0
typedef int bool;

#endif
