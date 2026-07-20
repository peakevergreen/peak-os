#include "gui.h"
#include "console.h"
#include "fb.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"
#include "timer.h"
#include "util.h"

#define TASKBAR_H 36
#define TITLE_H   24
#define TERM_COLS 60
#define TERM_ROWS 14

static uint32_t win_x, win_y, win_w, win_h;
static int win_open;
static int dragging;
static int32_t drag_off_x, drag_off_y;
static uint32_t cursor_backup[16][16];
static int cursor_saved;
static int32_t last_cx, last_cy;

static char term_lines[TERM_ROWS][TERM_COLS + 1];
static uint32_t term_row, term_col;
static int term_dirty;

static uint32_t color_bg(void) { return fb_rgb(0x12, 0x2A, 0x1C); }
static uint32_t color_accent(void) { return fb_rgb(0x3D, 0xA3, 0x6A); }
static uint32_t color_panel(void) { return fb_rgb(0x0E, 0x1F, 0x16); }
static uint32_t color_text(void) { return fb_rgb(0xE8, 0xF0, 0xEA); }
static uint32_t color_term(void) { return fb_rgb(0x0B, 0x1A, 0x12); }

void gui_term_reset(void) {
    memset(term_lines, 0, sizeof(term_lines));
    term_row = 0;
    term_col = 0;
    term_dirty = 1;
}

static void term_scroll(void) {
    for (uint32_t r = 1; r < TERM_ROWS; r++)
        memcpy(term_lines[r - 1], term_lines[r], TERM_COLS + 1);
    memset(term_lines[TERM_ROWS - 1], 0, TERM_COLS + 1);
    if (term_row > 0)
        term_row--;
}

void gui_term_putc(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
        if (term_row >= TERM_ROWS)
            term_scroll();
        term_dirty = 1;
        return;
    }
    if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            term_lines[term_row][term_col] = '\0';
        }
        term_dirty = 1;
        return;
    }
    if (c < 32)
        return;
    if (term_col >= TERM_COLS) {
        term_col = 0;
        term_row++;
        if (term_row >= TERM_ROWS)
            term_scroll();
    }
    term_lines[term_row][term_col++] = c;
    term_lines[term_row][term_col] = '\0';
    term_dirty = 1;
}

static void draw_cursor(int32_t x, int32_t y) {
    if (cursor_saved) {
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 16; col++)
                fb_put_pixel((uint32_t)(last_cx + col), (uint32_t)(last_cy + row),
                             cursor_backup[row][col]);
    }
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++)
            cursor_backup[row][col] = fb_get_pixel((uint32_t)(x + col), (uint32_t)(y + row));
    }
    cursor_saved = 1;
    last_cx = x;
    last_cy = y;

    uint32_t white = fb_rgb(0xFF, 0xFF, 0xFF);
    uint32_t black = fb_rgb(0x00, 0x00, 0x00);
    static const uint16_t shape[16] = {
        0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
        0xFF80, 0xFC00, 0xDC00, 0x8E00, 0x0700, 0x0300, 0x0100, 0x0000
    };
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            if (shape[row] & (0x8000 >> col)) {
                fb_put_pixel((uint32_t)(x + col), (uint32_t)(y + row), white);
                if (col + 1 < 16)
                    fb_put_pixel((uint32_t)(x + col + 1), (uint32_t)(y + row), black);
            }
        }
    }
}

static void draw_desktop_bg(void) {
    struct framebuffer *fb = fb_get();
    uint32_t h = (uint32_t)fb->height;
    uint32_t w = (uint32_t)fb->width;
    for (uint32_t y = 0; y < h - TASKBAR_H; y++) {
        uint8_t g = (uint8_t)(0x12 + (y * 20 / (h ? h : 1)));
        fb_fill_rect(0, y, w, 1, fb_rgb(0x0A, g, 0x14));
    }
    fb_draw_string(24, 24, "Peak OS", color_text(), color_bg());
    fb_draw_string(24, 44, "from-scratch desktop", fb_rgb(0x9A, 0xC4, 0xAE), color_bg());

    fb_fill_rect(32, 100, 72, 72, color_panel());
    fb_fill_rect(32, 100, 72, 4, color_accent());
    fb_draw_string(40, 128, "Term", color_text(), color_panel());
    fb_draw_string(36, 176, "Terminal", color_text(), color_bg());
}

static void draw_taskbar(void) {
    struct framebuffer *fb = fb_get();
    uint32_t y = (uint32_t)fb->height - TASKBAR_H;
    fb_fill_rect(0, y, (uint32_t)fb->width, TASKBAR_H, color_panel());
    fb_fill_rect(0, y, (uint32_t)fb->width, 2, color_accent());
    fb_draw_string(12, y + 10, "Peak OS", color_text(), color_panel());

    char tbuf[16];
    uint64_t secs = timer_uptime_secs();
    uint64_t m = secs / 60;
    uint64_t s = secs % 60;
    char n1[8], n2[8];
    itoa_u(m, n1, 10);
    itoa_u(s, n2, 10);
    int i = 0;
    for (char *p = n1; *p && i < 8; p++)
        tbuf[i++] = *p;
    tbuf[i++] = ':';
    if (s < 10)
        tbuf[i++] = '0';
    for (char *p = n2; *p && i < 14; p++)
        tbuf[i++] = *p;
    tbuf[i] = '\0';
    fb_draw_string((uint32_t)fb->width - 80, y + 10, tbuf, color_text(), color_panel());
}

static void draw_terminal_window(void) {
    if (!win_open)
        return;
    window_draw_frame(win_x, win_y, win_w, win_h, "Terminal", color_term());
    uint32_t tx = win_x + 12;
    uint32_t ty = win_y + TITLE_H + 8;
    for (uint32_t r = 0; r < TERM_ROWS; r++) {
        if (term_lines[r][0])
            fb_draw_string(tx, ty + r * 16, term_lines[r], color_text(), color_term());
    }
    /* caret */
    fb_fill_rect(tx + term_col * 8, ty + term_row * 16, 8, 16, color_accent());
}

void desktop_draw(void) {
    cursor_saved = 0;
    draw_desktop_bg();
    draw_taskbar();
    draw_terminal_window();
    term_dirty = 0;
}

void desktop_init(void) {
    struct framebuffer *fb = fb_get();
    win_w = 560;
    win_h = 300;
    win_x = (uint32_t)(fb->width / 2 - win_w / 2);
    win_y = (uint32_t)(fb->height / 2 - win_h / 2 - 20);
    win_open = 1;
    dragging = 0;
    cursor_saved = 0;
    gui_term_reset();
    gui_term_putc('p');
    gui_term_putc('e');
    gui_term_putc('a');
    gui_term_putc('k');
    gui_term_putc('>');
    gui_term_putc(' ');
}

static int point_in(int32_t px, int32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

void desktop_run(void) {
    desktop_init();
    desktop_draw();

    uint64_t last_tick = timer_ticks();

    for (;;) {
        char c = keyboard_try_getchar();
        if (c == 27)
            break;
        if (c && win_open)
            shell_feed_char(c);

        struct mouse_state m;
        mouse_poll(&m);

        if (m.left_pressed) {
            if (point_in(m.x, m.y, 32, 100, 72, 72)) {
                win_open = 1;
                term_dirty = 1;
            }
            if (win_open && point_in(m.x, m.y, win_x, win_y, win_w, TITLE_H)) {
                dragging = 1;
                drag_off_x = m.x - (int32_t)win_x;
                drag_off_y = m.y - (int32_t)win_y;
            }
            mouse_clear_clicks();
        }
        if (m.left_released) {
            dragging = 0;
            mouse_clear_clicks();
        }
        if (dragging && (m.buttons & 1)) {
            int32_t nx = m.x - drag_off_x;
            int32_t ny = m.y - drag_off_y;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            win_x = (uint32_t)nx;
            win_y = (uint32_t)ny;
            term_dirty = 1;
        }

        if (timer_ticks() - last_tick >= 50) {
            last_tick = timer_ticks();
            term_dirty = 1;
        }

        if (term_dirty)
            desktop_draw();
        draw_cursor(m.x, m.y);

        __asm__ volatile ("hlt");
    }
}
