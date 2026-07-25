#include "desktop_internal.h"
#include "fb.h"
#include "keyboard.h"
#include "vfs.h"
#include "clipboard.h"
#include "notify.h"
#include "util.h"

#define NOTEPAD_MAX 16384
#define NOTEPAD_VIS   20

static char np_buf[NOTEPAD_MAX];
static char np_path[VFS_PATH_MAX];
static int np_len;
static int np_dirty;
static int np_caret;
static int np_scroll;
static int np_sel_a;
static int np_sel_b;

static void np_clear_sel(void) {
    np_sel_a = np_sel_b = -1;
}

static int np_has_sel(void) {
    return np_sel_a >= 0 && np_sel_b >= np_sel_a;
}

static void np_mark_dirty(void) {
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static void np_load_path(const char *path) {
    np_len = 0;
    np_buf[0] = '\0';
    np_caret = 0;
    np_scroll = 0;
    np_dirty = 0;
    np_clear_sel();
    np_path[0] = '\0';
    if (!path || !path[0])
        return;
    size_t n = 0;
    if (vfs_read_file(path, np_buf, NOTEPAD_MAX - 1, &n) != 0)
        n = 0;
    np_len = (int)n;
    np_buf[np_len] = '\0';
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(np_path); i++)
        np_path[i] = path[i];
    np_path[i] = '\0';
}

void desktop_notepad_init(void) {
    np_load_path(NULL);
}

void desktop_notepad_open(const char *path) {
    int slot = desktop_open_app(APP_NOTEPAD);
    if (slot < 0)
        return;
    np_load_path(path);
    np_mark_dirty();
}

static int np_row_count(void) {
    int rows = 1;
    for (int i = 0; i < np_len; i++)
        if (np_buf[i] == '\n')
            rows++;
    return rows;
}

static int np_row_start(int row) {
    int r = 0;
    int start = 0;
    for (int i = 0; i < np_len && r < row; i++)
        if (np_buf[i] == '\n') {
            r++;
            start = i + 1;
        }
    return start;
}

static int np_row_end(int row) {
    int start = np_row_start(row);
    for (int i = start; i < np_len; i++)
        if (np_buf[i] == '\n')
            return i;
    return np_len;
}

static void np_clamp_caret(void) {
    if (np_caret < 0)
        np_caret = 0;
    if (np_caret > np_len)
        np_caret = np_len;
}

static void np_clamp_scroll(int vis) {
    int caret_row = 0;
    for (int i = 0; i < np_caret; i++)
        if (np_buf[i] == '\n')
            caret_row++;
    if (caret_row < np_scroll)
        np_scroll = caret_row;
    if (caret_row >= np_scroll + vis)
        np_scroll = caret_row - vis + 1;
    if (np_scroll < 0)
        np_scroll = 0;
}

static void np_insert(const char *s, int slen) {
    if (!s || slen <= 0 || np_len + slen >= NOTEPAD_MAX - 1)
        return;
    memmove(np_buf + np_caret + slen, np_buf + np_caret, (size_t)(np_len - np_caret));
    memcpy(np_buf + np_caret, s, (size_t)slen);
    np_len += slen;
    np_buf[np_len] = '\0';
    np_caret += slen;
    np_dirty = 1;
}

static void np_delete_range(int a, int b) {
    if (a < 0)
        a = 0;
    if (b > np_len)
        b = np_len;
    if (a >= b)
        return;
    int n = b - a;
    memmove(np_buf + a, np_buf + b, (size_t)(np_len - b));
    np_len -= n;
    np_buf[np_len] = '\0';
    if (np_caret > b)
        np_caret -= n;
    else if (np_caret > a)
        np_caret = a;
    np_dirty = 1;
}

static void np_save_as(const char *path) {
    if (!path || !path[0])
        return;
    if (vfs_write_file(path, np_buf, (size_t)np_len) != 0) {
        notify_push("Save failed");
        dirty_bits |= DIRTY_TOAST;
        return;
    }
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(np_path); i++)
        np_path[i] = path[i];
    np_path[i] = '\0';
    np_dirty = 0;
    notify_push("Saved");
    dirty_bits |= DIRTY_TOAST;
}

static void np_save(void) {
    if (!np_path[0]) {
        char path[VFS_PATH_MAX];
        for (int n = 1; n < 100; n++) {
            snprintf(path, sizeof(path), "/home/dev/workspace/untitled%d.txt", n);
            if (!vfs_exists(path)) {
                np_save_as(path);
                return;
            }
        }
        notify_push("Save failed");
        dirty_bits |= DIRTY_TOAST;
        return;
    }
    np_save_as(np_path);
}

static void np_save_as_copy(void) {
    if (!np_path[0]) {
        np_save();
        return;
    }
    char path[VFS_PATH_MAX];
    snprintf(path, sizeof(path), "%s~", np_path);
    np_save_as(path);
}

static void np_cut(void) {
    if (!np_has_sel())
        return;
    clipboard_set(np_buf + np_sel_a, (size_t)(np_sel_b - np_sel_a + 1));
    np_delete_range(np_sel_a, np_sel_b + 1);
    np_caret = np_sel_a;
    np_clear_sel();
    np_mark_dirty();
}

static void np_copy(void) {
    if (!np_has_sel())
        return;
    clipboard_set(np_buf + np_sel_a, (size_t)(np_sel_b - np_sel_a + 1));
    notify_push("Copied");
    dirty_bits |= DIRTY_TOAST;
}

static void np_paste(void) {
    char clip[CLIPBOARD_MAX];
    size_t n = clipboard_get(clip, sizeof(clip));
    if (!n)
        return;
    if (np_has_sel()) {
        np_delete_range(np_sel_a, np_sel_b + 1);
        np_caret = np_sel_a;
        np_clear_sel();
    }
    np_insert(clip, (int)n);
    np_mark_dirty();
}

void desktop_notepad_draw(struct win *w) {
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(8);
    uint32_t ty = w->y + th + desktop_u(6);
    uint32_t inner = w->w > desktop_u(16) ? w->w - desktop_u(16) : w->w;
    const char *title = np_path[0] ? np_path : "Untitled";
    const char *show = title;
    for (const char *p = title; *p; p++)
        if (*p == '/')
            show = p + 1;
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "%s%s", show, np_dirty ? " *" : "");
    fb_draw_string_fit(tx, ty, inner, hdr, desktop_color_accent(), desktop_color_bg());
    ty += ch + desktop_u(4);
    uint32_t area_h = w->h > th + ch * 2 + desktop_u(20) ? w->h - th - ch * 2 - desktop_u(20) : ch;
    int vis = (int)(area_h / ch);
    if (vis > NOTEPAD_VIS)
        vis = NOTEPAD_VIS;
    if (vis < 1)
        vis = 1;
    np_clamp_scroll(vis);
    int rows = np_row_count();
    for (int vr = 0; vr < vis; vr++) {
        int row = np_scroll + vr;
        if (row >= rows)
            break;
        int rs = np_row_start(row);
        int re = np_row_end(row);
        char line[TERM_COLS + 1];
        int n = re - rs;
        if (n > TERM_COLS)
            n = TERM_COLS;
        if (n > 0)
            memcpy(line, np_buf + rs, (size_t)n);
        line[n] = '\0';
        uint32_t rowy = ty + (uint32_t)vr * ch;
        fb_draw_string_fit(tx, rowy, inner, line, desktop_color_fg(), desktop_color_bg());
        if (row == 0 || np_caret >= rs) {
            int caret_row = 0;
            for (int i = 0; i < np_caret; i++)
                if (np_buf[i] == '\n')
                    caret_row++;
            if (caret_row == row) {
                int col = np_caret - rs;
                if (col > TERM_COLS)
                    col = TERM_COLS;
                uint32_t cx = tx + (uint32_t)col * fb_cell_w();
                fb_fill_rect(cx, rowy, desktop_u(2), ch, desktop_color_accent());
            }
        }
    }
}

int desktop_notepad_key(int key) {
    np_clamp_caret();
    if (keyboard_ctrl_down()) {
        if (key == 's' || key == 'S') {
            if (keyboard_shift_down())
                np_save_as_copy();
            else
                np_save();
            np_mark_dirty();
            return 1;
        }
        if (key == 'c' || key == 'C') {
            np_copy();
            return 1;
        }
        if (key == 'x' || key == 'X') {
            np_cut();
            return 1;
        }
        if (key == 'v' || key == 'V') {
            np_paste();
            return 1;
        }
    }
    if (key == KEY_UP) {
        int row = 0;
        for (int i = 0; i < np_caret; i++)
            if (np_buf[i] == '\n')
                row++;
        if (row > 0) {
            int col = np_caret - np_row_start(row);
            int prev_end = np_row_end(row - 1);
            int prev_start = np_row_start(row - 1);
            np_caret = prev_start + col;
            if (np_caret > prev_end)
                np_caret = prev_end;
        }
        np_clear_sel();
        np_mark_dirty();
        return 1;
    }
    if (key == KEY_DOWN) {
        int row = 0;
        for (int i = 0; i < np_caret; i++)
            if (np_buf[i] == '\n')
                row++;
        if (row + 1 < np_row_count()) {
            int col = np_caret - np_row_start(row);
            int next_end = np_row_end(row + 1);
            int next_start = np_row_start(row + 1);
            np_caret = next_start + col;
            if (np_caret > next_end)
                np_caret = next_end;
        }
        np_clear_sel();
        np_mark_dirty();
        return 1;
    }
    if (key == KEY_LEFT && np_caret > 0) {
        np_caret--;
        np_clear_sel();
        np_mark_dirty();
        return 1;
    }
    if (key == KEY_RIGHT && np_caret < np_len) {
        np_caret++;
        np_clear_sel();
        np_mark_dirty();
        return 1;
    }
    if (key == KEY_HOME) {
        int row = 0;
        for (int i = 0; i < np_caret; i++)
            if (np_buf[i] == '\n')
                row++;
        np_caret = np_row_start(row);
        np_clear_sel();
        np_mark_dirty();
        return 1;
    }
    if (key == KEY_END) {
        int row = 0;
        for (int i = 0; i < np_caret; i++)
            if (np_buf[i] == '\n')
                row++;
        np_caret = np_row_end(row);
        np_clear_sel();
        np_mark_dirty();
        return 1;
    }
    if (key == '\b') {
        if (np_has_sel()) {
            np_delete_range(np_sel_a, np_sel_b + 1);
            np_caret = np_sel_a;
            np_clear_sel();
        } else if (np_caret > 0) {
            np_delete_range(np_caret - 1, np_caret);
            np_caret--;
        }
        np_mark_dirty();
        return 1;
    }
    if (key == KEY_DELETE) {
        if (np_has_sel()) {
            np_delete_range(np_sel_a, np_sel_b + 1);
            np_caret = np_sel_a;
            np_clear_sel();
        } else if (np_caret < np_len)
            np_delete_range(np_caret, np_caret + 1);
        np_mark_dirty();
        return 1;
    }
    if (key == '\n') {
        np_insert("\n", 1);
        np_mark_dirty();
        return 1;
    }
    if (key >= 32 && key < 127 && np_len + 1 < NOTEPAD_MAX - 1) {
        if (np_has_sel()) {
            np_delete_range(np_sel_a, np_sel_b + 1);
            np_caret = np_sel_a;
            np_clear_sel();
        }
        char c = (char)key;
        np_insert(&c, 1);
        np_mark_dirty();
        return 1;
    }
    return 0;
}

int desktop_notepad_click(struct win *w, int32_t mx, int32_t my) {
    (void)mx;
    uint32_t ch = fb_cell_h();
    uint32_t ty = w->y + desktop_title_h() + desktop_u(6) + ch + desktop_u(4);
    int row = (int)((my - (int32_t)ty) / (int32_t)ch);
    if (row < 0)
        return 0;
    row += np_scroll;
    if (row >= np_row_count())
        row = np_row_count() - 1;
    np_caret = np_row_start(row);
    np_clear_sel();
    np_mark_dirty();
    return 1;
}

int desktop_notepad_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 2)
        return 0;
    int n = 0;
#define NADD(lbl, en, sep, act) do { \
    if (n >= max_items) return n; \
    items[n].label = (lbl); items[n].enabled = (en); items[n].separator = (sep); \
    items[n].action_id = (act); n++; } while (0)
    NADD("Cut", np_has_sel(), 0, CTX_ACT_NPAD_CUT);
    NADD("Copy", np_has_sel(), 0, CTX_ACT_NPAD_COPY);
    NADD("Paste", clipboard_has(), 0, CTX_ACT_NPAD_PASTE);
    NADD(NULL, 0, 1, CTX_ACT_NONE);
    NADD("Save", 1, 0, CTX_ACT_NPAD_SAVE);
    NADD("Save As", 1, 0, CTX_ACT_NPAD_SAVEAS);
    NADD(NULL, 0, 1, CTX_ACT_NONE);
    NADD("Close window", 1, 0, CTX_ACT_CLOSE);
    return n;
#undef NADD
}

int desktop_notepad_ctx_action(int action_id) {
    switch (action_id) {
    case CTX_ACT_NPAD_CUT:
        np_cut();
        return 1;
    case CTX_ACT_NPAD_COPY:
        np_copy();
        return 1;
    case CTX_ACT_NPAD_PASTE:
        np_paste();
        return 1;
    case CTX_ACT_NPAD_SAVE:
        np_save();
        np_mark_dirty();
        return 1;
    case CTX_ACT_NPAD_SAVEAS:
        np_save_as_copy();
        np_mark_dirty();
        return 1;
    default:
        return 0;
    }
}
