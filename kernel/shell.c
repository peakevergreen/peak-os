#include "shell.h"
#include "console.h"
#include "fb.h"
#include "gui.h"
#include "keyboard.h"
#include "util.h"
#include "sched.h"

static enum os_mode mode = MODE_CLI;
static char line[256];
static uint32_t line_len;
static uint32_t caret;
static int sel_anchor; /* -1 = no selection; else selection is [min(anchor,caret), max) */
static char clipboard[256];
static char prompt_buf[VFS_PATH_MAX + 16];
static uint32_t edit_paint_len; /* last MODE_CLI painted prompt+line width */

enum os_mode shell_mode(void) {
    return mode;
}

void shell_set_mode(enum os_mode m) {
    mode = m;
}

static void build_prompt(void) {
    snprintf(prompt_buf, sizeof(prompt_buf), "peak:%s> ", shell_getcwd());
}

static void print_prompt(void) {
    build_prompt();
    caret = 0;
    line_len = 0;
    sel_anchor = -1;
    line[0] = '\0';
    edit_paint_len = (uint32_t)strlen(prompt_buf);
    if (mode == MODE_GUI)
        gui_term_set_edit(prompt_buf, line, caret, -1, -1);
    else
        console_write(prompt_buf);
}

static void refresh_edit_display(void) {
    build_prompt();
    if (mode == MODE_GUI) {
        int a = -1, b = -1;
        if (sel_anchor >= 0) {
            a = (int)(sel_anchor < (int)caret ? (uint32_t)sel_anchor : caret);
            b = (int)(sel_anchor < (int)caret ? caret : (uint32_t)sel_anchor);
            if (b > a)
                b--; /* inclusive end for paint */
            else
                a = b = -1;
        }
        gui_term_set_edit(prompt_buf, line, caret, a, b);
        return;
    }
    line[line_len] = '\0';
    uint32_t cur = (uint32_t)strlen(prompt_buf) + line_len;
    console_putc('\r');
    console_write(prompt_buf);
    console_write(line);
    if (edit_paint_len > cur) {
        for (uint32_t i = 0; i < edit_paint_len - cur; i++)
            console_putc(' ');
        console_putc('\r');
        console_write(prompt_buf);
        console_write(line);
    }
    edit_paint_len = cur;
}

static int sel_lo(void) {
    if (sel_anchor < 0)
        return -1;
    return sel_anchor < (int)caret ? sel_anchor : (int)caret;
}

static int sel_hi(void) {
    if (sel_anchor < 0)
        return -1;
    return sel_anchor < (int)caret ? (int)caret : sel_anchor;
}

static void clear_sel(void) { sel_anchor = -1; }

static void delete_selection(void) {
    int a = sel_lo(), b = sel_hi();
    if (a < 0 || b <= a)
        return;
    uint32_t n = line_len - (uint32_t)b;
    memmove(line + a, line + b, n + 1);
    line_len -= (uint32_t)(b - a);
    caret = (uint32_t)a;
    clear_sel();
}

static void copy_selection(void) {
    int a = sel_lo(), b = sel_hi();
    if (a < 0 || b <= a) {
        size_t n = line_len;
        if (n >= sizeof(clipboard))
            n = sizeof(clipboard) - 1;
        memcpy(clipboard, line, n);
        clipboard[n] = '\0';
        return;
    }
    size_t n = (size_t)(b - a);
    if (n >= sizeof(clipboard))
        n = sizeof(clipboard) - 1;
    memcpy(clipboard, line + a, n);
    clipboard[n] = '\0';
}

static void paste_clipboard(void) {
    size_t n = strlen(clipboard);
    if (!n)
        return;
    delete_selection();
    if (line_len + n >= sizeof(line))
        n = sizeof(line) - 1 - line_len;
    memmove(line + caret + n, line + caret, line_len - caret + 1);
    memcpy(line + caret, clipboard, n);
    line_len += (uint32_t)n;
    caret += (uint32_t)n;
}

void shell_redraw_prompt(void) {
    print_prompt();
}

void shell_init(void) {
    line_len = 0;
    caret = 0;
    sel_anchor = -1;
    clipboard[0] = '\0';
    shell_builtins_init();
    console_write("\n");
    console_write("  PeakOS 0.2 — arrows move  Ctrl+A select-all  Ctrl+C/X/V copy/cut/paste\n");
    console_write("  Workspace: /home/dev/workspace  |  ask \"...\"  |  gui  |  theme\n\n");
    print_prompt();
}

static void handle_key(int key) {
    if (!key)
        return;

    if (key == 1) { /* Ctrl+A — select all */
        if (line_len) {
            sel_anchor = 0;
            caret = line_len;
        }
        refresh_edit_display();
        return;
    }
    if (key == 3) { /* Ctrl+C — copy */
        copy_selection();
        return;
    }
    if (key == 24) { /* Ctrl+X — cut */
        copy_selection();
        delete_selection();
        refresh_edit_display();
        return;
    }
    if (key == 22) { /* Ctrl+V — paste */
        paste_clipboard();
        refresh_edit_display();
        return;
    }

    if (key == '\n' || key == '\r') {
        clear_sel();
        console_putc('\n');
        line[line_len] = '\0';
        shell_execute(line);
        line_len = 0;
        caret = 0;
        print_prompt();
        return;
    }

    if (key == KEY_LEFT) {
        if (keyboard_shift_down()) {
            if (sel_anchor < 0)
                sel_anchor = (int)caret;
        } else {
            clear_sel();
        }
        if (caret > 0)
            caret--;
        refresh_edit_display();
        return;
    }
    if (key == KEY_RIGHT) {
        if (keyboard_shift_down()) {
            if (sel_anchor < 0)
                sel_anchor = (int)caret;
        } else {
            clear_sel();
        }
        if (caret < line_len)
            caret++;
        refresh_edit_display();
        return;
    }
    if (key == KEY_HOME) {
        if (keyboard_shift_down()) {
            if (sel_anchor < 0)
                sel_anchor = (int)caret;
        } else {
            clear_sel();
        }
        caret = 0;
        refresh_edit_display();
        return;
    }
    if (key == KEY_END) {
        if (keyboard_shift_down()) {
            if (sel_anchor < 0)
                sel_anchor = (int)caret;
        } else {
            clear_sel();
        }
        caret = line_len;
        refresh_edit_display();
        return;
    }
    if (key == KEY_DELETE) {
        if (sel_anchor >= 0 && sel_hi() > sel_lo()) {
            delete_selection();
        } else if (caret < line_len) {
            memmove(line + caret, line + caret + 1, line_len - caret);
            line_len--;
        }
        refresh_edit_display();
        return;
    }

    if (key == '\b' || key == 127) {
        if (sel_anchor >= 0 && sel_hi() > sel_lo()) {
            delete_selection();
        } else if (caret > 0) {
            memmove(line + caret - 1, line + caret, line_len - caret + 1);
            caret--;
            line_len--;
            clear_sel();
        }
        refresh_edit_display();
        return;
    }

    if (key >= 32 && key < 127) {
        if (sel_anchor >= 0 && sel_hi() > sel_lo())
            delete_selection();
        if (line_len + 1 < sizeof(line)) {
            memmove(line + caret + 1, line + caret, line_len - caret + 1);
            line[caret] = (char)key;
            caret++;
            line_len++;
            clear_sel();
        }
        refresh_edit_display();
        return;
    }
}

void shell_feed_char(char c) {
    handle_key((unsigned char)c);
}

void shell_feed_key(int key) {
    handle_key(key);
}

void shell_run_once(void) {
    int k = keyboard_try_getkey();
    if (k)
        handle_key(k);
    sched_maybe_preempt();
}
