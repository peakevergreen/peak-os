#ifndef PEAK_CONSOLE_H
#define PEAK_CONSOLE_H

#include "types.h"
#include <stdio.h>
#include <stdarg.h>

static inline void console_write(const char *s) {
    fputs(s, stdout);
}

static inline void console_write_ui(const char *s) {
    console_write(s);
}

static inline void console_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static inline void console_printf_ui(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

#endif
