#include "console.h"
#include "console_scroll.h"
#include "fb.h"
#include "gui.h"
#include "serial.h"
#include "shell.h"
#include "theme.h"
#include "util.h"
#include <stdarg.h>

static uint32_t cursor_row, cursor_col;
static uint32_t cols, rows;
static uint32_t fg_color, bg_color;

static char *capture_buf;
static size_t capture_cap;
static size_t capture_len;
static int capture_on;

void console_capture_begin(char *buf, size_t cap) {
    capture_buf = buf;
    capture_cap = cap ? cap : 0;
    capture_len = 0;
    capture_on = buf && cap > 0;
    if (capture_on && capture_cap)
        capture_buf[0] = '\0';
}

size_t console_capture_end(void) {
    size_t n = capture_len;
    capture_on = 0;
    capture_buf = 0;
    capture_cap = 0;
    capture_len = 0;
    return n;
}

void console_init(void) {
    struct framebuffer *fb = fb_get();
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    cols = cw ? (uint32_t)(fb->width / cw) : 1;
    rows = ch ? (uint32_t)(fb->height / ch) : 1;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    cursor_row = 0;
    cursor_col = 0;
    const struct peak_theme *t = theme_get();
    fg_color = t->fg;
    bg_color = t->bg;
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
    uint32_t glyph_h = fb_cell_h();
    uint32_t h = (uint32_t)fb->height;
    uint32_t copy_rows = 0;
    if (!console_scroll_plan(h, glyph_h, &copy_rows))
        return;

    /*
     * CLI draws glyphs to the *front* buffer (not in a compose frame).
     * Scrolling the compositor back buffer and presenting it would wipe
     * the visible boot log with a stale/empty back — keep CLI on front.
     * Invariant: scroll copies via fb->addr (front), never fb backbuffer.
     */
    uint64_t nbytes = console_scroll_bytes((uint32_t)fb->pitch, copy_rows);
    if (nbytes)
        memmove(fb->addr, fb->addr + (uint64_t)glyph_h * fb->pitch, (size_t)nbytes);
    /* Always clear the vacated strip on the front buffer (not draw-target/back). */
    fb_front_fill_rect(0, h - glyph_h, (uint32_t)fb->width, glyph_h, bg_color);
    if (cursor_row > 0)
        cursor_row--;
}

static void console_putc_screen(char c) {
    if (shell_mode() == MODE_GUI) {
        gui_term_putc(c);
        return;
    }
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
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
    fb_draw_char(cursor_col * cw, cursor_row * ch, c, fg_color, bg_color);
    cursor_col++;
    if (cursor_col >= cols) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= rows)
            scroll();
    }
}

void console_putc(char c) {
    if (capture_on) {
        if (capture_len + 1 < capture_cap) {
            capture_buf[capture_len++] = c;
            capture_buf[capture_len] = '\0';
        }
        return;
    }
    serial_write(c);
    console_putc_screen(c);
}

void console_backspace(void) {
    if (shell_mode() == MODE_GUI) {
        gui_term_putc('\b');
        return;
    }
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    if (cursor_col > 0) {
        cursor_col--;
    } else if (cursor_row > 0) {
        cursor_row--;
        cursor_col = cols - 1;
    } else {
        return;
    }
    fb_draw_char(cursor_col * cw, cursor_row * ch, ' ', fg_color, bg_color);
}

void console_write(const char *s) {
    while (*s)
        console_putc(*s++);
}

void console_write_ui(const char *s) {
    if (!s)
        return;
    while (*s)
        console_putc_screen(*s++);
}

static void console_vprintf(void (*putc_fn)(char), void (*write_fn)(const char *),
                            const char *fmt, va_list ap) {
    char num[32];
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            putc_fn(*p);
            continue;
        }
        p++;
        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            write_fn(s ? s : "(null)");
        } else if (*p == 'c') {
            putc_fn((char)va_arg(ap, int));
        } else if (*p == 'd' || *p == 'i') {
            int v = va_arg(ap, int);
            if (v < 0) {
                putc_fn('-');
                v = -v;
            }
            itoa_u((uint64_t)(uint32_t)v, num, 10);
            write_fn(num);
        } else if (*p == 'u') {
            itoa_u(va_arg(ap, uint32_t), num, 10);
            write_fn(num);
        } else if (*p == 'x') {
            itoa_u(va_arg(ap, uint32_t), num, 16);
            write_fn(num);
        } else if (*p == 'l' && *(p + 1) == 'u') {
            p++;
            itoa_u(va_arg(ap, uint64_t), num, 10);
            write_fn(num);
        } else if (*p == 'l' && *(p + 1) == 'x') {
            p++;
            itoa_u(va_arg(ap, uint64_t), num, 16);
            write_fn(num);
        } else if (*p == '%') {
            putc_fn('%');
        } else {
            putc_fn('%');
            putc_fn(*p);
        }
    }
}

/* Gentoo brand: purple brackets + green ok (classic emerge/OpenRC feel). */
static uint32_t gentoo_purple(void) { return fb_rgb(0x88, 0x66, 0xCC); }
static uint32_t gentoo_green(void)  { return fb_rgb(0x39, 0xB5, 0x4A); }
static uint32_t gentoo_bad(void)    { return fb_rgb(0xCC, 0x33, 0x33); }

static void console_write_fg(uint32_t fg, const char *s) {
    uint32_t old = fg_color;
    fg_color = fg;
    console_write(s);
    fg_color = old;
}

static void console_status(const char *msg, int ok) {
    if (!msg)
        msg = "";
    /* " * msg" then pad so "[ ok ]" ends at cols-1 without auto-wrap + newline. */
    console_write(" ");
    console_write_fg(gentoo_green(), "*");
    console_write(" ");
    console_write(msg);

    size_t msg_len = strlen(msg);
    size_t used = 3 + msg_len; /* space + * + space + msg */
    size_t tag_len = 6;         /* "[ ok ]" / "[ !! ]" */
    uint32_t pad = 1;
    if (cols > used + tag_len)
        pad = (uint32_t)(cols - used - tag_len - 1);

    for (uint32_t i = 0; i < pad; i++)
        console_putc(' ');

    uint32_t br = gentoo_purple();
    uint32_t mid = ok ? gentoo_green() : gentoo_bad();
    console_write_fg(br, "[");
    console_write_fg(mid, ok ? " ok " : " !! ");
    console_write_fg(br, "]");
    console_putc('\n');
}

void console_boot_logo(void) {
    static const char *lines[] = {
        " ____  _____    _    _  __     ___  ____",
        "|  _ \\| ____|  / \\  | |/ /    / _ \\/ ___|",
        "| |_) |  _|   / _ \\ | ' /    | | | \\___ \\",
        "|  __/| |___ / ___ \\| . \\    | |_| |___) |",
        "|_|   |_____/_/   \\_\\_|\\_\\    \\___/|____/",
        NULL
    };
    console_write("\n");
    for (int i = 0; lines[i]; i++) {
        console_write("  ");
        console_write(lines[i]);
        console_write("\n");
    }
    console_write("\n");
}

void console_status_ok(const char *msg) {
    console_status(msg, 1);
}

void console_status_fail(const char *msg) {
    console_status(msg, 0);
}

void console_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    console_vprintf(console_putc, console_write, fmt, ap);
    va_end(ap);
}

void console_printf_ui(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    console_vprintf(console_putc_screen, console_write_ui, fmt, ap);
    va_end(ap);
}
