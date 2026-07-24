#ifndef PEAK_CONSOLE_H
#define PEAK_CONSOLE_H

#include "types.h"

void console_init(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);
/* Capture console_putc/write into buf (no screen/serial while active). */
void console_capture_begin(char *buf, size_t cap);
size_t console_capture_end(void); /* bytes written (NUL not counted) */
void console_printf(const char *fmt, ...);
/* Screen/GUI only — does not mirror to COM1 (agent chatter / privacy). */
void console_write_ui(const char *s);
void console_printf_ui(const char *fmt, ...);
void console_set_color(uint32_t fg, uint32_t bg);
void console_get_cursor(uint32_t *row, uint32_t *col);
void console_backspace(void);

/* Gentoo/OpenRC-style right-aligned status:  * msg ... [ ok ] */
void console_status_ok(const char *msg);
void console_status_fail(const char *msg);
/* Compact ASCII PEAK wordmark after successful boot. */
void console_boot_logo(void);

#endif
