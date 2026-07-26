#include "desktop_internal.h"
#include "fb.h"
#include "keyboard.h"
#include "theme.h"
#include "vfs.h"
#include "clipboard.h"
#include "notify.h"
#include "util.h"

static int np_a11y_btn;

static void np_draw_focus_ring(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t c = desktop_color_accent();
    uint32_t t = desktop_u(2);
    if (t < 2)
        t = 2;
    fb_fill_rect(x, y, w, t, c);
    fb_fill_rect(x, y + h - t, w, t, c);
    fb_fill_rect(x, y, t, h, c);
    fb_fill_rect(x + w - t, y, t, h, c);
}

#define NOTEPAD_MAX 16384
#define NOTEPAD_VIS   20
#define NP_FIND_MAX   48

static char np_buf[NOTEPAD_MAX];
static char np_path[VFS_PATH_MAX];
static int np_len, np_dirty, np_caret, np_scroll, np_sel_a, np_sel_b;
static int np_find_open, np_find_repl, np_find_qlen, np_find_rlen, np_find_pos;
static char np_find_q[NP_FIND_MAX], np_find_r[NP_FIND_MAX];

static void np_clear_sel(void) { np_sel_a = np_sel_b = -1; }
static int np_has_sel(void) { return np_sel_a >= 0 && np_sel_b >= np_sel_a; }
static void np_mark_dirty(void) { dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); }
static void np_insert(const char *s, int slen);
static void np_delete_range(int a, int b);

static int np_load_path(const char *path) {
    np_len = 0; np_buf[0] = '\0'; np_caret = 0; np_scroll = 0; np_dirty = 0;
    np_clear_sel(); np_path[0] = '\0';
    if (!path || !path[0]) return 0;
    size_t n = 0;
    if (vfs_read_file(path, np_buf, NOTEPAD_MAX - 1, &n) != 0) { np_buf[0] = '\0'; return -1; }
    np_len = (int)n; np_buf[np_len] = '\0';
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(np_path); i++) np_path[i] = path[i];
    np_path[i] = '\0';
    return 0;
}

void desktop_notepad_init(void) {
    np_find_open = np_find_repl = 0; np_find_qlen = np_find_rlen = 0;
    np_find_q[0] = np_find_r[0] = '\0'; np_find_pos = -1;
    np_load_path(NULL);
}

void desktop_notepad_open(const char *path) {
    if (desktop_open_app(APP_NOTEPAD) < 0) return;
    if (path && path[0] && np_load_path(path) != 0) {
        notify_push("Could not open file"); dirty_bits |= DIRTY_TOAST; return;
    }
    if (!path || !path[0]) np_load_path(NULL);
    np_mark_dirty();
}

static int np_row_count(void) { int rows = 1; for (int i = 0; i < np_len; i++) if (np_buf[i] == '\n') rows++; return rows; }
static int np_row_start(int row) { int r = 0, start = 0; for (int i = 0; i < np_len && r < row; i++) if (np_buf[i] == '\n') { r++; start = i + 1; } return start; }
static int np_row_end(int row) { int start = np_row_start(row); for (int i = start; i < np_len; i++) if (np_buf[i] == '\n') return i; return np_len; }
static int np_gutter_cols(int rows) { int cols = 2; while (rows >= 10) { rows /= 10; cols++; } return cols; }
static void np_clamp_caret(void) { if (np_caret < 0) np_caret = 0; if (np_caret > np_len) np_caret = np_len; }
static void np_clamp_scroll(int vis) {
    int caret_row = 0; for (int i = 0; i < np_caret; i++) if (np_buf[i] == '\n') caret_row++;
    if (caret_row < np_scroll) np_scroll = caret_row;
    if (caret_row >= np_scroll + vis) np_scroll = caret_row - vis + 1;
    if (np_scroll < 0) np_scroll = 0;
}
static void np_insert(const char *s, int slen) {
    if (!s || slen <= 0 || np_len + slen >= NOTEPAD_MAX - 1) return;
    memmove(np_buf + np_caret + slen, np_buf + np_caret, (size_t)(np_len - np_caret));
    memcpy(np_buf + np_caret, s, (size_t)slen);
    np_len += slen; np_buf[np_len] = '\0'; np_caret += slen; np_dirty = 1;
}
static void np_delete_range(int a, int b) {
    if (a < 0) a = 0; if (b > np_len) b = np_len; if (a >= b) return;
    int n = b - a; memmove(np_buf + a, np_buf + b, (size_t)(np_len - b));
    np_len -= n; np_buf[np_len] = '\0';
    if (np_caret > b) np_caret -= n; else if (np_caret > a) np_caret = a;
    np_dirty = 1;
}
static int np_match_at(int pos, const char *q, int qlen) {
    if (qlen <= 0 || pos < 0 || pos + qlen > np_len) return 0;
    return memcmp(np_buf + pos, q, (size_t)qlen) == 0;
}
static int np_find_next(int from) {
    if (np_find_qlen <= 0) return -1; if (from < 0) from = 0;
    for (int i = from; i + np_find_qlen <= np_len; i++) if (np_match_at(i, np_find_q, np_find_qlen)) return i;
    for (int i = 0; i + np_find_qlen <= np_len && i < from; i++) if (np_match_at(i, np_find_q, np_find_qlen)) return i;
    return -1;
}
static int np_find_count(void) {
    if (np_find_qlen <= 0) return 0;
    int n = 0;
    for (int i = 0; i + np_find_qlen <= np_len; i++)
        if (np_match_at(i, np_find_q, np_find_qlen))
            n++;
    return n;
}
static int np_find_match_no(void) {
    if (np_find_pos < 0 || np_find_qlen <= 0) return 0;
    int n = 0;
    for (int i = 0; i + np_find_qlen <= np_len; i++) {
        if (np_match_at(i, np_find_q, np_find_qlen)) {
            n++;
            if (i == np_find_pos)
                return n;
        }
    }
    return 0;
}
static void np_find_do(void) {
    int hit = np_find_next(np_find_pos < 0 ? np_caret : np_find_pos + 1);
    if (hit < 0) { notify_push("Not found"); dirty_bits |= DIRTY_TOAST; return; }
    np_find_pos = hit; np_sel_a = hit; np_sel_b = hit + np_find_qlen - 1; np_caret = hit + np_find_qlen; np_mark_dirty();
}
static void np_replace_one(void) {
    if (!np_has_sel() || np_find_qlen <= 0 || !np_match_at(np_sel_a, np_find_q, np_find_qlen)) return;
    np_delete_range(np_sel_a, np_sel_b + 1); np_caret = np_sel_a; np_clear_sel();
    if (np_find_rlen > 0) np_insert(np_find_r, np_find_rlen);
    np_find_pos = np_caret - 1; np_dirty = 1; np_find_do();
}
static void np_replace_all(void) {
    if (np_find_qlen <= 0) return;
    int n = 0;
    for (int i = 0; i + np_find_qlen <= np_len; ) {
        if (np_match_at(i, np_find_q, np_find_qlen)) {
            np_delete_range(i, i + np_find_qlen);
            if (np_find_rlen > 0)
                np_insert(np_find_r, np_find_rlen);
            n++;
            if (np_find_rlen > 0)
                i += np_find_rlen;
        } else {
            i++;
        }
    }
    np_find_pos = -1;
    np_clear_sel();
    np_dirty = 1;
    char msg[48];
    snprintf(msg, sizeof(msg), "Replaced %d", n);
    notify_push(msg);
    dirty_bits |= DIRTY_TOAST;
    np_mark_dirty();
}
static void np_save_as(const char *path) {
    if (!path || !path[0]) return;
    if (vfs_write_file(path, np_buf, (size_t)np_len) != 0) { notify_push("Save failed"); dirty_bits |= DIRTY_TOAST; return; }
    size_t i = 0; for (; path[i] && i + 1 < sizeof(np_path); i++) np_path[i] = path[i];
    np_path[i] = '\0'; np_dirty = 0; notify_push("Saved"); dirty_bits |= DIRTY_TOAST;
}
static void np_save(void) {
    if (!np_path[0]) {
        char path[VFS_PATH_MAX];
        for (int n = 1; n < 100; n++) {
            snprintf(path, sizeof(path), "/home/dev/workspace/untitled%d.txt", n);
            if (!vfs_exists(path)) { np_save_as(path); return; }
        }
        notify_push("Save failed"); dirty_bits |= DIRTY_TOAST; return;
    }
    np_save_as(np_path);
}
static void np_save_as_copy(void) {
    if (!np_path[0]) { np_save(); return; }
    char path[VFS_PATH_MAX]; snprintf(path, sizeof(path), "%s~", np_path); np_save_as(path);
}
static void np_cut(void) {
    if (!np_has_sel()) return;
    clipboard_set(np_buf + np_sel_a, (size_t)(np_sel_b - np_sel_a + 1));
    np_delete_range(np_sel_a, np_sel_b + 1); np_caret = np_sel_a; np_clear_sel(); np_mark_dirty();
}
static void np_copy(void) {
    if (!np_has_sel()) return;
    clipboard_set(np_buf + np_sel_a, (size_t)(np_sel_b - np_sel_a + 1));
    notify_push("Copied"); dirty_bits |= DIRTY_TOAST;
}
void desktop_notepad_paste_previous(void) {
    char clip[CLIPBOARD_MAX];
    size_t n = clipboard_get_previous(clip, sizeof(clip));
    if (!n) {
        notify_push("No previous clipboard");
        dirty_bits |= DIRTY_TOAST;
        return;
    }
    if (np_has_sel()) { np_delete_range(np_sel_a, np_sel_b + 1); np_caret = np_sel_a; np_clear_sel(); }
    np_insert(clip, (int)n);
    np_mark_dirty();
    notify_push("Pasted previous clipboard");
    dirty_bits |= DIRTY_TOAST;
}

static void np_paste(void) {
    char clip[CLIPBOARD_MAX]; size_t n = clipboard_get(clip, sizeof(clip));
    if (!n) return;
    if (np_has_sel()) { np_delete_range(np_sel_a, np_sel_b + 1); np_caret = np_sel_a; np_clear_sel(); }
    np_insert(clip, (int)n); np_mark_dirty();
}
static int np_find_key(int key) {
    if (key == 27) { np_find_open = np_find_repl = 0; np_mark_dirty(); return 1; }
    if (key == '\n') { if (np_find_repl) np_replace_one(); else np_find_do(); return 1; }
    if ((key == 'a' || key == 'A') && np_find_repl) { np_replace_all(); return 1; }
    if (key == '\b') {
        if (np_find_repl && np_find_rlen > 0) np_find_r[--np_find_rlen] = '\0';
        else if (np_find_qlen > 0) { np_find_q[--np_find_qlen] = '\0'; np_find_pos = -1; }
        np_mark_dirty(); return 1;
    }
    if (key == KEY_TAB && !np_find_repl) { np_find_repl = 1; np_find_rlen = 0; np_find_r[0] = '\0'; np_mark_dirty(); return 1; }
    if (key >= 32 && key < 127) {
        if (np_find_repl) {
            if (np_find_rlen + 1 >= NP_FIND_MAX) return 1;
            np_find_r[np_find_rlen++] = (char)key; np_find_r[np_find_rlen] = '\0';
        } else {
            if (np_find_qlen + 1 >= NP_FIND_MAX) return 1;
            np_find_q[np_find_qlen++] = (char)key; np_find_q[np_find_qlen] = '\0'; np_find_pos = -1;
        }
        np_mark_dirty(); return 1;
    }
    return 0;
}

void desktop_notepad_draw(struct win *w) {
    uint32_t ch = fb_cell_h(), cw = fb_cell_w(), th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(8), ty = w->y + th + desktop_u(6);
    uint32_t inner = w->w > desktop_u(16) ? w->w - desktop_u(16) : w->w;
    const char *show = np_path[0] ? np_path : "Untitled";
    for (const char *p = np_path; *p; p++) if (*p == '/') show = p + 1;
    char hdr[96]; snprintf(hdr, sizeof(hdr), "%s%s", show, np_dirty ? " *" : "");
    fb_draw_string_fit(tx, ty, inner, hdr, desktop_color_accent(), desktop_color_bg());
    ty += ch + desktop_u(4);
    {
        uint32_t bw = desktop_u(56);
        uint32_t by = ty;
        fb_draw_string(tx, by, "Save", np_a11y_btn == 1 ? desktop_color_bg() : desktop_color_fg(),
                       np_a11y_btn == 1 ? desktop_color_accent() : desktop_color_surface());
        if (np_a11y_btn == 1)
            np_draw_focus_ring(tx, by, bw, ch);
        uint32_t fx = tx + bw + desktop_u(8);
        fb_draw_string(fx, by, "Find", np_a11y_btn == 2 ? desktop_color_bg() : desktop_color_fg(),
                       np_a11y_btn == 2 ? desktop_color_accent() : desktop_color_surface());
        if (np_a11y_btn == 2)
            np_draw_focus_ring(fx, by, bw, ch);
        ty += ch + desktop_u(4);
    }
    uint32_t find_h = np_find_open ? ch + desktop_u(6) : 0;
    uint32_t area_h = w->h > th + ch * 2 + desktop_u(20) + find_h ? w->h - th - ch * 2 - desktop_u(20) - find_h : ch;
    int vis = (int)(area_h / ch); if (vis > NOTEPAD_VIS) vis = NOTEPAD_VIS; if (vis < 1) vis = 1;
    np_clamp_scroll(vis);
    int rows = np_row_count(), gutter_cols = np_gutter_cols(rows);
    uint32_t gutter_w = (uint32_t)gutter_cols * cw + desktop_u(8);
    uint32_t text_x = tx + gutter_w, text_w = inner > gutter_w ? inner - gutter_w : cw;
    for (int vr = 0; vr < vis; vr++) {
        int row = np_scroll + vr; if (row >= rows) break;
        int rs = np_row_start(row), re = np_row_end(row);
        char line[TERM_COLS + 1]; int n = re - rs; if (n > TERM_COLS) n = TERM_COLS;
        if (n > 0) memcpy(line, np_buf + rs, (size_t)n); line[n] = '\0';
        uint32_t rowy = ty + (uint32_t)vr * ch;
        char lnum[8]; snprintf(lnum, sizeof(lnum), "%*d", gutter_cols, row + 1);
        fb_draw_string_fit(tx, rowy, gutter_w, lnum, desktop_color_dim(), desktop_color_bg());
        uint32_t bg = desktop_color_bg();
        if (np_has_sel() && np_sel_b >= rs && np_sel_a <= re) {
            fb_fill_rect(text_x, rowy, text_w, ch, desktop_color_title()); bg = desktop_color_title();
        }
        fb_draw_string_fit(text_x, rowy, text_w, line, desktop_color_fg(), bg);
        if (np_has_sel() && np_sel_b >= rs && np_sel_a <= re) {
            int sel_lo = np_sel_a > rs ? np_sel_a : rs, sel_hi = np_sel_b < re ? np_sel_b : re;
            if (sel_lo <= sel_hi)
                fb_fill_rect(text_x + (uint32_t)(sel_lo - rs) * cw, rowy, (uint32_t)(sel_hi - sel_lo + 1) * cw, ch, theme_get()->accent);
        }
        int caret_row = 0; for (int i = 0; i < np_caret; i++) if (np_buf[i] == '\n') caret_row++;
        if (caret_row == row) {
            int col = np_caret - rs; if (col > TERM_COLS) col = TERM_COLS;
            fb_fill_rect(text_x + (uint32_t)col * cw, rowy, desktop_u(2), ch, desktop_color_accent());
        }
    }
    if (np_find_open) {
        uint32_t fy = ty + (uint32_t)vis * ch + desktop_u(4);
        fb_fill_rect(tx, fy, inner, ch + desktop_u(4), desktop_color_surface());
        char fbar[128];
        int ftotal = np_find_count();
        int fcur = np_find_match_no();
        if (np_find_repl)
            snprintf(fbar, sizeof(fbar),
                     "Find: %s  Replace: %s  %d/%d  Enter=one A=all Esc",
                     np_find_q, np_find_r, fcur, ftotal);
        else
            snprintf(fbar, sizeof(fbar),
                     "Find: %s  %d/%d  Enter=next  Tab=replace  Esc=close",
                     np_find_q, fcur, ftotal);
        fb_draw_string_fit(tx + desktop_u(4), fy + desktop_u(2), inner - desktop_u(8), fbar, desktop_color_fg(), desktop_color_surface());
    }
}

int desktop_notepad_key(int key) {
    if (key == KEY_TAB || key == '\t') {
        np_a11y_btn = (np_a11y_btn + 1) % 3;
        np_mark_dirty();
        return 1;
    }
    if (np_a11y_btn != 0) {
        if (key == KEY_LEFT || key == 'h' || key == 'H') {
            if (np_a11y_btn > 0) np_a11y_btn--;
            np_mark_dirty();
            return 1;
        }
        if (key == KEY_RIGHT || key == 'l' || key == 'L') {
            if (np_a11y_btn < 2) np_a11y_btn++;
            np_mark_dirty();
            return 1;
        }
        if (key == '\n') {
            if (np_a11y_btn == 1) { np_save(); np_mark_dirty(); return 1; }
            if (np_a11y_btn == 2) { np_find_open = 1; np_find_repl = 0; np_find_pos = -1; np_mark_dirty(); return 1; }
        }
        if (key != KEY_UP && key != KEY_DOWN)
            return 1;
    }
    if (np_find_open && np_find_key(key)) return 1;
    np_clamp_caret();
    if (keyboard_ctrl_down()) {
        if (key == 's' || key == 'S') { if (keyboard_shift_down()) np_save_as_copy(); else np_save(); np_mark_dirty(); return 1; }
        if (key == 'c' || key == 'C') { np_copy(); return 1; }
        if (key == 'x' || key == 'X') { np_cut(); return 1; }
        if (key == 'v' || key == 'V') { np_paste(); return 1; }
        if (key == 'f' || key == 'F') { np_find_open = 1; np_find_repl = 0; np_find_pos = -1; np_mark_dirty(); return 1; }
        if (key == 'h' || key == 'H') { np_find_open = 1; np_find_repl = 1; np_find_pos = -1; np_mark_dirty(); return 1; }
    }
    if (key == KEY_UP) {
        int row = 0; for (int i = 0; i < np_caret; i++) if (np_buf[i] == '\n') row++;
        if (row > 0) { int col = np_caret - np_row_start(row), ps = np_row_start(row - 1), pe = np_row_end(row - 1); np_caret = ps + col; if (np_caret > pe) np_caret = pe; }
        np_clear_sel(); np_mark_dirty(); return 1;
    }
    if (key == KEY_DOWN) {
        int row = 0; for (int i = 0; i < np_caret; i++) if (np_buf[i] == '\n') row++;
        if (row + 1 < np_row_count()) { int col = np_caret - np_row_start(row), ns = np_row_start(row + 1), ne = np_row_end(row + 1); np_caret = ns + col; if (np_caret > ne) np_caret = ne; }
        np_clear_sel(); np_mark_dirty(); return 1;
    }
    if (key == KEY_LEFT && np_caret > 0) { np_caret--; np_clear_sel(); np_mark_dirty(); return 1; }
    if (key == KEY_RIGHT && np_caret < np_len) { np_caret++; np_clear_sel(); np_mark_dirty(); return 1; }
    if (key == KEY_HOME) { int row = 0; for (int i = 0; i < np_caret; i++) if (np_buf[i] == '\n') row++; np_caret = np_row_start(row); np_clear_sel(); np_mark_dirty(); return 1; }
    if (key == KEY_END) { int row = 0; for (int i = 0; i < np_caret; i++) if (np_buf[i] == '\n') row++; np_caret = np_row_end(row); np_clear_sel(); np_mark_dirty(); return 1; }
    if (key == '\b') {
        if (np_has_sel()) { np_delete_range(np_sel_a, np_sel_b + 1); np_caret = np_sel_a; np_clear_sel(); }
        else if (np_caret > 0) { np_delete_range(np_caret - 1, np_caret); np_caret--; }
        np_mark_dirty(); return 1;
    }
    if (key == KEY_DELETE) {
        if (np_has_sel()) { np_delete_range(np_sel_a, np_sel_b + 1); np_caret = np_sel_a; np_clear_sel(); }
        else if (np_caret < np_len) np_delete_range(np_caret, np_caret + 1);
        np_mark_dirty(); return 1;
    }
    if (key == '\n') { np_insert("\n", 1); np_mark_dirty(); return 1; }
    if (key >= 32 && key < 127 && np_len + 1 < NOTEPAD_MAX - 1) {
        if (np_has_sel()) { np_delete_range(np_sel_a, np_sel_b + 1); np_caret = np_sel_a; np_clear_sel(); }
        char c = (char)key; np_insert(&c, 1); np_mark_dirty(); return 1;
    }
    return 0;
}

int desktop_notepad_click(struct win *w, int32_t mx, int32_t my) {
    (void)mx;
    int row = (int)((my - (int32_t)(w->y + desktop_title_h() + desktop_u(6) + fb_cell_h() + desktop_u(4))) / (int32_t)fb_cell_h());
    if (row < 0) return 0;
    row += np_scroll; if (row >= np_row_count()) row = np_row_count() - 1;
    np_caret = np_row_start(row); np_clear_sel(); np_mark_dirty(); return 1;
}

int desktop_notepad_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 2) return 0;
    int n = 0;
#define NADD(lbl, en, sep, act) do { if (n >= max_items) return n; \
    items[n].label=(lbl); items[n].enabled=(en); items[n].separator=(sep); items[n].action_id=(act); n++; } while (0)
    NADD("Cut", np_has_sel(), 0, CTX_ACT_NPAD_CUT);
    NADD("Copy", np_has_sel(), 0, CTX_ACT_NPAD_COPY);
    NADD("Paste", clipboard_has(), 0, CTX_ACT_NPAD_PASTE);
    NADD(NULL, 0, 1, CTX_ACT_NONE);
    NADD("Find", 1, 0, CTX_ACT_NPAD_FIND);
    NADD("Save", 1, 0, CTX_ACT_NPAD_SAVE);
    NADD("Save As", 1, 0, CTX_ACT_NPAD_SAVEAS);
    NADD(NULL, 0, 1, CTX_ACT_NONE);
    NADD("Close window", 1, 0, CTX_ACT_CLOSE);
    return n;
#undef NADD
}

int desktop_notepad_ctx_action(int action_id) {
    switch (action_id) {
    case CTX_ACT_NPAD_CUT: np_cut(); return 1;
    case CTX_ACT_NPAD_COPY: np_copy(); return 1;
    case CTX_ACT_NPAD_PASTE: np_paste(); return 1;
    case CTX_ACT_NPAD_FIND: np_find_open = 1; np_find_repl = 0; np_find_pos = -1; np_mark_dirty(); return 1;
    case CTX_ACT_NPAD_SAVE: np_save(); np_mark_dirty(); return 1;
    case CTX_ACT_NPAD_SAVEAS: np_save_as_copy(); np_mark_dirty(); return 1;
    default: return 0;
    }
}
