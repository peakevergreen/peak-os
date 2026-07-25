#ifdef PEAK_HOST_TEST
#include "include/console_scroll.h"
#include <string.h>
#else
#include "console_scroll.h"
#include "util.h"
#endif

int console_scroll_plan(uint32_t fb_height, uint32_t glyph_h, uint32_t *copy_rows) {
    if (glyph_h == 0 || fb_height <= glyph_h) return 0;
    if (copy_rows) *copy_rows = fb_height - glyph_h;
    return 1;
}

uint64_t console_scroll_bytes(uint32_t pitch, uint32_t copy_rows) {
    return (uint64_t)pitch * (uint64_t)copy_rows;
}

int console_scroll_line_find(const char *line, const char *needle, int *col_out) {
    if (!line || !needle || !needle[0]) return 0;
    size_t nlen = strlen(needle);
    for (const char *p = line; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] == needle[i]) i++;
        if (i == nlen) { if (col_out) *col_out = (int)(p - line); return 1; }
    }
    return 0;
}

int console_scroll_find_next(const char *const *lines, int nlines, const char *needle,
                             int start_line, int start_col, int *out_line, int *out_col) {
    if (!lines || nlines <= 0 || !needle || !needle[0]) return 0;
    if (start_line < 0) start_line = 0;
    if (start_col < 0) start_col = 0;
    size_t nlen = strlen(needle);
    for (int li = start_line; li < nlines; li++) {
        const char *line = lines[li];
        if (!line) continue;
        const char *p = line;
        if (li == start_line && start_col >= 0) {
            p = line + start_col;
            if (*p) p++; else continue;
        }
        for (; *p; p++) {
            size_t i = 0;
            while (i < nlen && p[i] == needle[i]) i++;
            if (i == nlen) {
                if (out_line) *out_line = li;
                if (out_col) *out_col = (int)(p - line);
                return 1;
            }
        }
    }
    return 0;
}

#ifndef PEAK_HOST_TEST
static char scrollback[CONSOLE_SCROLLBACK_LINES][CONSOLE_SCROLLBACK_COLS];
static int sb_count, sb_head, sb_cur_len;
static char sb_cur[CONSOLE_SCROLLBACK_COLS];

void console_scrollback_reset(void) {
    sb_count = sb_head = sb_cur_len = 0;
    sb_cur[0] = '\0';
}

static void scrollback_commit_line(void) {
    if (sb_cur_len >= CONSOLE_SCROLLBACK_COLS) sb_cur_len = CONSOLE_SCROLLBACK_COLS - 1;
    sb_cur[sb_cur_len] = '\0';
    if (sb_count < CONSOLE_SCROLLBACK_LINES) {
        memcpy(scrollback[(sb_head + sb_count) % CONSOLE_SCROLLBACK_LINES], sb_cur, CONSOLE_SCROLLBACK_COLS);
        sb_count++;
    } else {
        memcpy(scrollback[sb_head], sb_cur, CONSOLE_SCROLLBACK_COLS);
        sb_head = (sb_head + 1) % CONSOLE_SCROLLBACK_LINES;
    }
    sb_cur_len = 0; sb_cur[0] = '\0';
}

void console_scrollback_note_char(char c) {
    if (c == '\r') return;
    if (c == '\n') { scrollback_commit_line(); return; }
    if (sb_cur_len + 1 >= CONSOLE_SCROLLBACK_COLS) scrollback_commit_line();
    if (sb_cur_len + 1 < CONSOLE_SCROLLBACK_COLS) sb_cur[sb_cur_len++] = c;
}

int console_scrollback_line_count(void) { return sb_count; }

const char *console_scrollback_line(int idx) {
    if (idx < 0 || idx >= sb_count) return 0;
    return scrollback[(sb_head + idx) % CONSOLE_SCROLLBACK_LINES];
}

int console_scrollback_find(const char *needle, int *out_line, int *out_col,
                            int start_line, int start_col) {
    if (sb_count <= 0) return 0;
    const char *lines[CONSOLE_SCROLLBACK_LINES];
    for (int i = 0; i < sb_count; i++) lines[i] = console_scrollback_line(i);
    return console_scroll_find_next(lines, sb_count, needle, start_line, start_col, out_line, out_col);
}
#endif
