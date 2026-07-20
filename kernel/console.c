#include "console.h"
#include "fb.h"
#include "gui.h"
#include "serial.h"
#include "shell.h"
#include "util.h"
#include <stdarg.h>

static uint32_t cursor_row, cursor_col;
static uint32_t cols, rows;
static uint32_t fg_color, bg_color;

void console_init(void) {
    struct framebuffer *fb = fb_get();
    cols = (uint32_t)(fb->width / 8);
    rows = (uint32_t)(fb->height / 16);
    cursor_row = 0;
    cursor_col = 0;
    fg_color = fb_rgb(0xE8, 0xEC, 0xF0);
    bg_color = fb_rgb(0x0B, 0x1A, 0x12);
    console_clear();
}

void console_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

void console_clear(void) {
    fb_clear(bg_color);
    cursor_row = 0;
    cursor_col = 0;
}

void console_get_cursor(uint32_t *row, uint32_t *col) {
    if (row) *row = cursor_row;
    if (col) *col = cursor_col;
}

static void scroll(void) {
    struct framebuffer *fb = fb_get();
    uint32_t line_bytes = (uint32_t)fb->pitch;
    uint32_t glyph_h = 16;
    for (uint32_t y = 0; y < fb->height - glyph_h; y++) {
        uint8_t *dst = fb->addr + y * fb->pitch;
        uint8_t *src = fb->addr + (y + glyph_h) * fb->pitch;
        memcpy(dst, src, line_bytes);
    }
    fb_fill_rect(0, (uint32_t)(fb->height - glyph_h), (uint32_t)fb->width, glyph_h, bg_color);
    if (cursor_row > 0)
        cursor_row--;
}

void console_putc(char c) {
    serial_write(c);
    if (shell_mode() == MODE_GUI) {
        gui_term_putc(c);
        return;
    }
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= rows)
            scroll();
        return;
    }
    if (c == '\r') {
        cursor_col = 0;
        return;
    }
    if (c == '\t') {
        cursor_col = (cursor_col + 4) & ~(uint32_t)3;
        if (cursor_col >= cols) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= rows)
                scroll();
        }
        return;
    }
    fb_draw_char(cursor_col * 8, cursor_row * 16, c, fg_color, bg_color);
    cursor_col++;
    if (cursor_col >= cols) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= rows)
            scroll();
    }
}

void console_backspace(void) {
    if (shell_mode() == MODE_GUI) {
        gui_term_putc('\b');
        return;
    }
    if (cursor_col > 0) {
        cursor_col--;
    } else if (cursor_row > 0) {
        cursor_row--;
        cursor_col = cols - 1;
    } else {
        return;
    }
    fb_draw_char(cursor_col * 8, cursor_row * 16, ' ', fg_color, bg_color);
}

void console_write(const char *s) {
    while (*s)
        console_putc(*s++);
}

void console_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char num[32];
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            console_putc(*p);
            continue;
        }
        p++;
        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            console_write(s ? s : "(null)");
        } else if (*p == 'c') {
            console_putc((char)va_arg(ap, int));
        } else if (*p == 'd' || *p == 'i') {
            int v = va_arg(ap, int);
            if (v < 0) {
                console_putc('-');
                v = -v;
            }
            itoa_u((uint64_t)(uint32_t)v, num, 10);
            console_write(num);
        } else if (*p == 'u') {
            itoa_u(va_arg(ap, uint32_t), num, 10);
            console_write(num);
        } else if (*p == 'x') {
            itoa_u(va_arg(ap, uint32_t), num, 16);
            console_write(num);
        } else if (*p == 'l' && *(p + 1) == 'u') {
            p++;
            itoa_u(va_arg(ap, uint64_t), num, 10);
            console_write(num);
        } else if (*p == 'l' && *(p + 1) == 'x') {
            p++;
            itoa_u(va_arg(ap, uint64_t), num, 16);
            console_write(num);
        } else if (*p == '%') {
            console_putc('%');
        } else {
            console_putc('%');
            console_putc(*p);
        }
    }
    va_end(ap);
}
