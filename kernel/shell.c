#include "shell.h"
#include "console.h"
#include "console_scroll.h"
#include "fb.h"
#include "gui.h"
#include "keyboard.h"
#include "util.h"
#include "sched.h"
#include "vfs.h"

static enum os_mode mode = MODE_CLI;
static char line[256];
static uint32_t line_len;
static uint32_t caret;
static int sel_anchor; /* -1 = no selection; else selection is [min(anchor,caret), max) */
static char clipboard[256];
static char prompt_buf[VFS_PATH_MAX + 24];
static uint32_t edit_paint_len;
static int find_active, find_len, find_hit_line = -1, find_hit_col = -1, find_from_line, find_from_col;
static char find_needle[64];

enum os_mode shell_mode(void) {
    return mode;
}

void shell_set_mode(enum os_mode m) {
    mode = m;
}

static void build_prompt(void) {
    int rc = shell_last_status();
    if (rc != 0) {
        char num[8];
        itoa_u((uint64_t)(rc < 0 ? -rc : rc), num, 10);
        snprintf(prompt_buf, sizeof(prompt_buf), "peak:%s [%s]> ", shell_getcwd(), num);
    } else {
        snprintf(prompt_buf, sizeof(prompt_buf), "peak:%s> ", shell_getcwd());
    }
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
    shell_history_init();
    shell_alias_init();
    console_write("\n");
    console_write("  PeakOS 0.3.0-ai — arrows move  Ctrl+A select-all  Ctrl+C/X/V copy/cut/paste\n");
    console_write("  Up/Down history  !! / !n expand  Tab complete  alias  cd -\n");
    console_write("  Ctrl+F search scrollback  |  gui → desktop (Ctrl+Alt+Esc back to CLI)\n");
    console_write("  Workspace: /home/dev/workspace  |  ask \"...\"  |  theme\n\n");
    print_prompt();
}

static void find_reprint_edit(void) {
    console_putc('\r'); build_prompt(); console_write(prompt_buf); console_write(line);
    edit_paint_len = (uint32_t)strlen(prompt_buf) + line_len;
}
static void find_show_hit(void) {
    const char *hit = console_scrollback_line(find_hit_line);
    if (!hit) return;
    console_putc('\n');
    console_printf("  scrollback[%d]: %s\n", find_hit_line + 1, hit);
    find_reprint_edit();
}
static void find_close(void) {
    find_active = 0; find_len = 0; find_needle[0] = '\0';
    find_hit_line = find_hit_col = -1;
    find_reprint_edit();
}
static void find_next(void) {
    if (find_len <= 0) return;
    find_needle[find_len] = '\0';
    int li = find_hit_line, co = find_hit_col;
    if (li < 0) { li = find_from_line; co = find_from_col; }
    if (console_scrollback_find(find_needle, &find_hit_line, &find_hit_col, li, co) ||
        console_scrollback_find(find_needle, &find_hit_line, &find_hit_col, 0, -1))
        find_show_hit();
    else {
        console_putc('\n');
        console_printf("  scrollback: no match for \"%s\"\n", find_needle);
        find_reprint_edit();
    }
}

static void handle_key(int key) {
    if (!key) return;
    if (find_active) {
        if (key == 27) { find_close(); return; }
        if (key == '\n' || key == KEY_TAB) { find_next(); return; }
        if (key == '\b' || key == 127) {
            if (find_len > 0) find_needle[--find_len] = '\0';
            find_hit_line = find_hit_col = -1;
            console_putc('\r'); build_prompt(); console_write(prompt_buf);
            console_write("Find: "); console_write(find_needle); console_putc(' ');
            return;
        }
        if (key >= 32 && key < 127 && find_len + 1 < (int)sizeof(find_needle)) {
            find_needle[find_len++] = (char)key; find_needle[find_len] = '\0';
            find_hit_line = find_hit_col = -1; console_putc((char)key);
            return;
        }
        return;
    }
    if (key == 6 || (keyboard_ctrl_down() && (key == 'f' || key == 'F'))) {
        find_active = 1; find_len = 0; find_needle[0] = '\0';
        find_hit_line = find_hit_col = -1;
        find_from_line = console_scrollback_line_count() > 0 ? console_scrollback_line_count() - 1 : 0;
        find_from_col = -1;
        console_putc('\n'); build_prompt(); console_write(prompt_buf); console_write("Find: ");
        edit_paint_len = (uint32_t)strlen(prompt_buf) + 6;
        return;
    }

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
        shell_history_reset_browse();
        console_putc('\n');
        line[line_len] = '\0';
        if (line_len)
            shell_execute(line); /* records expanded line in history */
        line_len = 0;
        caret = 0;
        print_prompt();
        return;
    }

    if (key == KEY_UP || key == 16) { /* Up or Ctrl-P */
        if (shell_history_prev(line, sizeof(line))) {
            line_len = (uint32_t)strlen(line);
            caret = line_len;
            clear_sel();
            refresh_edit_display();
        }
        return;
    }
    if (key == KEY_DOWN || key == 14) { /* Down or Ctrl-N */
        if (shell_history_next(line, sizeof(line))) {
            line_len = (uint32_t)strlen(line);
            caret = line_len;
            clear_sel();
            refresh_edit_display();
        }
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

    if (key == KEY_TAB || key == '\t') {
        /* Complete token under caret: /bin names or path entries. */
        int tok_end = (int)caret;
        int tok_start = tok_end;
        while (tok_start > 0 && line[tok_start - 1] != ' ')
            tok_start--;
        char tok[VFS_PATH_MAX];
        size_t tn = (size_t)(tok_end - tok_start);
        if (tn >= sizeof(tok))
            tn = sizeof(tok) - 1;
        memcpy(tok, line + tok_start, tn);
        tok[tn] = '\0';

        int first_tok = 1;
        for (int i = 0; i < tok_start; i++) {
            if (line[i] != ' ') {
                first_tok = 0;
                break;
            }
        }

        char match[VFS_PATH_MAX];
        match[0] = '\0';
        int matches = 0;

        if (first_tok && !strchr(tok, '/')) {
            struct vfs_dirent ents[64];
            int n = vfs_readdir("/bin", ents, 64);
            for (int i = 0; i < n; i++) {
                if (strncmp(ents[i].name, tok, tn) != 0)
                    continue;
                matches++;
                if (matches == 1) {
                    size_t j = 0;
                    for (; ents[i].name[j] && j + 1 < sizeof(match); j++)
                        match[j] = ents[i].name[j];
                    match[j] = '\0';
                } else {
                    if (matches == 2) {
                        console_putc('\n');
                        console_write(match);
                        console_putc(' ');
                    }
                    console_write(ents[i].name);
                    console_putc(' ');
                }
            }
        } else {
            char dir[VFS_PATH_MAX], prefix[VFS_NAME_MAX];
            const char *slash = NULL;
            for (const char *p = tok; *p; p++)
                if (*p == '/')
                    slash = p;
            if (slash) {
                size_t dlen = (size_t)(slash - tok);
                if (dlen >= sizeof(dir))
                    dlen = sizeof(dir) - 1;
                memcpy(dir, tok, dlen);
                dir[dlen] = '\0';
                if (!dir[0]) {
                    dir[0] = '/';
                    dir[1] = '\0';
                }
                size_t pi = 0;
                for (const char *p = slash + 1; *p && pi + 1 < sizeof(prefix); p++)
                    prefix[pi++] = *p;
                prefix[pi] = '\0';
            } else {
                size_t j = 0;
                const char *cwd = shell_getcwd();
                for (; cwd[j] && j + 1 < sizeof(dir); j++)
                    dir[j] = cwd[j];
                dir[j] = '\0';
                size_t pi = 0;
                for (; tok[pi] && pi + 1 < sizeof(prefix); pi++)
                    prefix[pi] = tok[pi];
                prefix[pi] = '\0';
            }
            char abs[VFS_PATH_MAX];
            if (shell_resolve_path(dir, abs, sizeof(abs)) == 0) {
                struct vfs_dirent ents[64];
                int n = vfs_readdir(abs, ents, 64);
                size_t plen = strlen(prefix);
                for (int i = 0; i < n; i++) {
                    if (strncmp(ents[i].name, prefix, plen) != 0)
                        continue;
                    matches++;
                    char cand[VFS_PATH_MAX];
                    if (slash) {
                        size_t o = 0;
                        size_t dlen = (size_t)(slash - tok + 1);
                        memcpy(cand, tok, dlen);
                        o = dlen;
                        for (size_t j = 0; ents[i].name[j] && o + 1 < sizeof(cand); j++)
                            cand[o++] = ents[i].name[j];
                        if (ents[i].type == VFS_DIR && o + 1 < sizeof(cand))
                            cand[o++] = '/';
                        cand[o] = '\0';
                    } else {
                        size_t o = 0;
                        for (; ents[i].name[o] && o + 1 < sizeof(cand); o++)
                            cand[o] = ents[i].name[o];
                        if (ents[i].type == VFS_DIR && o + 1 < sizeof(cand))
                            cand[o++] = '/';
                        cand[o] = '\0';
                    }
                    if (matches == 1) {
                        size_t j = 0;
                        for (; cand[j] && j + 1 < sizeof(match); j++)
                            match[j] = cand[j];
                        match[j] = '\0';
                    } else {
                        if (matches == 2) {
                            console_putc('\n');
                            console_write(match);
                            console_putc(' ');
                        }
                        console_write(cand);
                        console_putc(' ');
                    }
                }
            }
        }

        if (matches == 1 && match[0]) {
            /* Replace token with match */
            size_t ml = strlen(match);
            uint32_t new_len = line_len - (uint32_t)tn + (uint32_t)ml;
            if (new_len + 1 < sizeof(line)) {
                memmove(line + tok_start + ml, line + tok_end, line_len - (uint32_t)tok_end + 1);
                memcpy(line + tok_start, match, ml);
                line_len = new_len;
                caret = (uint32_t)tok_start + (uint32_t)ml;
                clear_sel();
            }
        } else if (matches > 1) {
            console_putc('\n');
            print_prompt();
            /* reprint line after listing */
            for (uint32_t i = 0; i < line_len; i++)
                console_putc(line[i]);
            edit_paint_len = (uint32_t)strlen(prompt_buf) + line_len;
            return;
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
