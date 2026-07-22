#ifndef PEAK_HOST_STDARG_H
#define PEAK_HOST_STDARG_H

/*
 * Host unit tests put -Itests/host/include before -Ikernel/include.
 * Without this file, <stdarg.h> resolves to the freestanding Peak header,
 * which omits __gnuc_va_list and breaks glibc <stdio.h> on Linux CI.
 */
typedef __builtin_va_list va_list;
typedef __builtin_va_list __gnuc_va_list;

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)
#define va_copy(d, s)  __builtin_va_copy(d, s)

/* Block kernel/include/stdarg.h and system stdarg from re-entering. */
#define _STDARG_H
#define _STDARG_H_

#endif
