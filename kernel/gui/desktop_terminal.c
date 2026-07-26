#include "desktop_internal.h"
#include "gui.h"
#include "fb.h"
#include "shell.h"
#include "keyboard.h"
#include "clipboard.h"
#include "notify.h"
#include "settings.h"
#include "util.h"

struct term_state {
    char lines[TERM_ROWS][TERM_COLS + 1];
    uint32_t row, col;
    int scroll;
    int sel_row;
    int sel_a, sel_b;
    char find_needle[TERM_FIND_MAX + 1];
    int find_len;
    int find_active;
    int find_hit_row;
    int find_hit_col;
    int find_match_total;
    int find_match_idx;
    uint32_t caret_col;
    int inited;
    int full_redraw;
    int cell_dirty;
    uint32_t dirty_col, dirty_row;
    uint32_t prev_caret_col;
};


#define TERM_MAX_TABS 4

struct term_win_tabs {
    struct term_state tabs[TERM_MAX_TABS];
    char labels[TERM_MAX_TABS][10];
    int ntabs;
    int cur;
};

static struct term_win_tabs term_wins[MAX_WINS];

static uint32_t term_tab_bar_h(void) {
    return fb_cell_h() + desktop_u(6);
}

static struct term_state *term_slot(int slot) {
    if (slot < 0 || slot >= MAX_WINS)
        slot = 0;
    struct term_win_tabs *tw = &term_wins[slot];
    if (tw->ntabs < 1) {
        tw->ntabs = 1;
        tw->cur = 0;
        tw->labels[0][0] = '1';
        tw->labels[0][1] = '\0';
    }
    if (tw->cur < 0 || tw->cur >= tw->ntabs)
        tw->cur = 0;
    return &tw->tabs[tw->cur];
}

static void term_reset_state(struct term_state *t) {
    memset(t, 0, sizeof(*t));
    t->sel_row = t->sel_a = t->sel_b = -1;
    t->find_hit_row = t->find_hit_col = -1;
    t->find_match_total = t->find_match_idx = 0;
    t->inited = 1;
    t->full_redraw = 1;
}

static void term_mark_surf_dirty(int slot, struct term_state *t);
static void term_draw_tab_strip(struct win *w, int slot);
static void term_new_tab(int slot);
static int term_tab_click(struct win *w, int slot, int32_t mx, int32_t my);
static void term_switch_tab(int slot, int idx);

static void term_switch_tab(int slot, int idx) {
    struct term_win_tabs *tw = &term_wins[slot];
    if (idx < 0 || idx >= tw->ntabs || idx == tw->cur)
        return;
    tw->cur = idx;
    tw->tabs[tw->cur].full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_surf_dirty(slot, term_slot(slot));
}

static void term_new_tab(int slot) {
    struct term_win_tabs *tw = &term_wins[slot];
    if (tw->ntabs >= TERM_MAX_TABS) {
        notify_push("Max terminal tabs");
        dirty_bits |= DIRTY_TOAST;
        return;
    }
    tw->ntabs++;
    tw->cur = tw->ntabs - 1;
    term_reset_state(&tw->tabs[tw->cur]);
    snprintf(tw->labels[tw->cur], sizeof(tw->labels[0]), "%d", tw->ntabs);
    dirty_bits |= DIRTY_TERM;
    term_mark_surf_dirty(slot, term_slot(slot));
}

static void term_draw_tab_strip(struct win *w, int slot) {
    struct term_win_tabs *tw = &term_wins[slot];
    uint32_t th = desktop_title_h();
    uint32_t y = w->y + th;
    uint32_t x = w->x + desktop_u(8);
    uint32_t inner = w->w > desktop_u(16) ? w->w - desktop_u(16) : w->w;
    uint32_t tab_w = inner / TERM_MAX_TABS;
    if (tab_w < desktop_u(36))
        tab_w = desktop_u(36);
    uint32_t bar_h = term_tab_bar_h();
    fb_fill_rect(x, y, inner, bar_h, desktop_color_surface());
    for (int i = 0; i < tw->ntabs; i++) {
        uint32_t tx = x + (uint32_t)i * tab_w;
        uint32_t bg = (i == tw->cur) ? desktop_color_bg() : desktop_color_surface();
        fb_fill_rect(tx + 1, y + 1, tab_w - 2, bar_h - 2, bg);
        if (i == tw->cur)
            fb_fill_rect(tx + 1, y + bar_h - 3, tab_w - 2, 2, desktop_color_accent());
        fb_draw_string_fit(tx + desktop_u(4), y + desktop_u(3), tab_w - desktop_u(8), tw->labels[i],
                           desktop_color_fg(), bg);
    }
    if (tw->ntabs < TERM_MAX_TABS) {
        uint32_t px = x + (uint32_t)tw->ntabs * tab_w + desktop_u(4);
        fb_draw_string(px, y + desktop_u(3), "+", desktop_color_accent(), desktop_color_surface());
    }
}

static int term_tab_click(struct win *w, int slot, int32_t mx, int32_t my) {
    struct term_win_tabs *tw = &term_wins[slot];
    uint32_t th = desktop_title_h();
    uint32_t y = w->y + th;
    uint32_t bar_h = term_tab_bar_h();
    uint32_t x = w->x + desktop_u(8);
    uint32_t inner = w->w > desktop_u(16) ? w->w - desktop_u(16) : w->w;
    uint32_t tab_w = inner / TERM_MAX_TABS;
    if (tab_w < desktop_u(36))
        tab_w = desktop_u(36);
    if ((uint32_t)my < y || (uint32_t)my >= y + bar_h)
        return 0;
    if ((uint32_t)mx < x || (uint32_t)mx >= x + inner)
        return 0;
    int idx = (int)(((uint32_t)mx - x) / tab_w);
    if (idx >= 0 && idx < tw->ntabs) {
        term_switch_tab(slot, idx);
        return 1;
    }
    if (tw->ntabs < TERM_MAX_TABS) {
        uint32_t px = x + (uint32_t)tw->ntabs * tab_w;
        if ((uint32_t)mx >= px && (uint32_t)mx < px + tab_w) {
            term_new_tab(slot);
            return 1;
        }
    }
    return 0;
}

static int active_term;
static int term_copy_on_select;

static void term_clamp_scroll(struct term_state *t, uint32_t vis);
static void term_mark_surf_dirty(int slot, struct term_state *t);
static void term_paste_buf(const char *buf, size_t n);

static void term_sync_ui_scale(void) {
    if (fb_ui_scale() != settings_gui_scale())
        fb_set_ui_scale(settings_gui_scale());
}

static void term_mark_cell_surf_dirty(int slot, struct term_state *t) {
    if (slot < 0 || slot >= MAX_WINS || !wins[slot].open)
        return;
    struct win *w = &wins[slot];
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = desktop_u(12);
    uint32_t ty = th + term_tab_bar_h() + desktop_u(8);
    uint32_t tab_h = term_tab_bar_h();
    uint32_t area_h = w->h > th + tab_h + desktop_u(16) ? w->h - th - tab_h - desktop_u(16) : ch;
    uint32_t vis = area_h / ch;
    if (vis > TERM_VIEW)
        vis = TERM_VIEW;
    if (vis < 1)
        vis = 1;
    int start = (int)t->row - (int)vis + 1 - t->scroll;
    if (start < 0)
        start = 0;
    int dirty_vis = (int)t->dirty_row - start;
    if (dirty_vis < 0 || dirty_vis >= (int)vis) {
        desktop_mark_win_surf_dirty(slot);
        return;
    }
    uint32_t c0 = t->dirty_col;
    uint32_t c1 = t->prev_caret_col;
    uint32_t c2 = t->caret_col < TERM_COLS ? t->caret_col : t->col;
    if (c1 > c0)
        c0 = c1;
    if (c2 > c0)
        c0 = c2;
    uint32_t cmin = t->dirty_col;
    if (t->prev_caret_col < cmin)
        cmin = t->prev_caret_col;
    if (c2 < cmin)
        cmin = c2;
    uint32_t rx = tx + cmin * cw;
    uint32_t ry = ty + (uint32_t)dirty_vis * ch;
    uint32_t rw = (c0 - cmin + 2) * cw;
    if (rx + rw > w->w)
        rw = w->w > rx ? w->w - rx : cw;
    desktop_mark_win_surf_dirty_rect(slot, rx, ry, rw, ch);
}

static void term_mark_surf_dirty(int slot, struct term_state *t) {
    if (t->full_redraw || t->cell_dirty == 0 || t->scroll != 0 || t->sel_a >= 0 ||
        t->find_len > 0 || t->find_active)
        desktop_mark_win_surf_dirty(slot);
    else
        term_mark_cell_surf_dirty(slot, t);
}

static void term_mark_active_surf_dirty(void) {
    int slot = active_term;
    if (slot < 0 || slot >= MAX_WINS || !wins[slot].open ||
        wins[slot].kind != APP_TERM) {
        for (int i = 0; i < MAX_WINS; i++) {
            if (wins[i].open && wins[i].kind == APP_TERM) {
                slot = i;
                break;
            }
        }
    }
    if (slot >= 0 && slot < MAX_WINS)
        term_mark_surf_dirty(slot, term_slot(slot));
    else
        desktop_mark_focus_surf_dirty();
}

static struct term_state *term_active(void) {
    if (active_term >= 0 && active_term < MAX_WINS &&
        wins[active_term].open && wins[active_term].kind == APP_TERM)
        return term_slot(active_term);
    for (int i = 0; i < MAX_WINS; i++) {
        if (wins[i].open && wins[i].kind == APP_TERM) {
            active_term = i;
            return term_slot(i);
        }
    }
    active_term = 0;
    return term_slot(0);
}

void desktop_term_reset_slot(int slot) {
    if (slot < 0 || slot >= MAX_WINS)
        return;
    struct term_win_tabs *tw = &term_wins[slot];
    if (tw->ntabs < 1) {
        tw->ntabs = 1;
        tw->cur = 0;
        tw->labels[0][0] = '1';
        tw->labels[0][1] = '\0';
    }
    term_reset_state(&tw->tabs[tw->cur]);
}

void desktop_term_activate(int slot) {
    if (slot < 0 || slot >= MAX_WINS)
        return;
    if (!wins[slot].open || wins[slot].kind != APP_TERM)
        return;
    active_term = slot;
    if (!term_slot(slot)->inited)
        desktop_term_reset_slot(slot);
}

void gui_term_reset(void) {
    struct term_state *t = term_active();
    memset(t->lines, 0, sizeof(t->lines));
    t->row = 0;
    t->col = 0;
    t->scroll = 0;
    t->sel_row = t->sel_a = t->sel_b = -1;
    t->find_active = 0;
    t->find_len = 0;
    t->find_needle[0] = '\0';
    t->find_hit_row = t->find_hit_col = -1;
    t->find_match_total = t->find_match_idx = 0;
    t->caret_col = 0;
    t->inited = 1;
    t->full_redraw = 1;
    t->cell_dirty = 0;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}

void gui_term_set_edit(const char *prompt, const char *text, uint32_t caret,
                       int sel_a, int sel_b) {
    struct term_state *t = term_active();
    char buf[TERM_COLS + 1];
    size_t o = 0;
    if (prompt) {
        for (size_t i = 0; prompt[i] && o + 1 < sizeof(buf); i++)
            buf[o++] = prompt[i];
    }
    uint32_t prompt_len = (uint32_t)o;
    if (text) {
        for (size_t i = 0; text[i] && o + 1 < sizeof(buf); i++)
            buf[o++] = text[i];
    }
    buf[o] = '\0';

    uint32_t row = t->row;
    if (row >= TERM_ROWS)
        row = TERM_ROWS - 1;
    memset(t->lines[row], 0, TERM_COLS + 1);
    for (size_t i = 0; buf[i] && i < TERM_COLS; i++)
        t->lines[row][i] = buf[i];

    t->caret_col = prompt_len + caret;
    if (t->caret_col > TERM_COLS)
        t->caret_col = TERM_COLS;
    t->col = t->caret_col;
    t->row = row;

    if (sel_a >= 0 && sel_b >= sel_a) {
        t->sel_row = (int)row;
        t->sel_a = (int)prompt_len + sel_a;
        t->sel_b = (int)prompt_len + sel_b;
    } else {
        t->sel_row = t->sel_a = t->sel_b = -1;
    }
    t->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}

int desktop_active_term_index(void) {
    return active_term;
}

static int term_has_selection(struct term_state *t) {
    return t->sel_a >= 0 && t->sel_b >= t->sel_a;
}

static void term_copy_selection(struct term_state *t) {
    if (!term_has_selection(t))
        return;
    int row = t->sel_row;
    if (row < 0 || row >= (int)TERM_ROWS)
        return;
    int a = t->sel_a;
    int b = t->sel_b;
    if (a < 0)
        a = 0;
    if (b >= TERM_COLS)
        b = TERM_COLS - 1;
    char buf[TERM_COLS + 1];
    int n = b - a + 1;
    if (n <= 0 || n > TERM_COLS)
        return;
    memcpy(buf, t->lines[row] + (size_t)a, (size_t)n);
    buf[n] = '\0';
    clipboard_set(buf, (size_t)n);
}

static char term_fold(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

static int term_find_match_at(const char *line, int col, const char *needle, int nlen) {
    if (nlen <= 0 || col < 0)
        return 0;
    for (int i = 0; i < nlen; i++) {
        char a = line[col + i];
        if (!a)
            return 0;
        if (term_fold(a) != term_fold(needle[i]))
            return 0;
    }
    return 1;
}

static void term_find_update_meta(struct term_state *t) {
    t->find_match_total = 0;
    t->find_match_idx = 0;
    if (t->find_len <= 0)
        return;
    int seen = 0;
    for (int row = 0; row < (int)TERM_ROWS; row++) {
        const char *line = t->lines[row];
        for (int col = 0; col < TERM_COLS; col++) {
            if (!term_find_match_at(line, col, t->find_needle, t->find_len))
                continue;
            t->find_match_total++;
            if (row < t->find_hit_row || (row == t->find_hit_row && col <= t->find_hit_col))
                t->find_match_idx = t->find_match_total;
        }
    }
    if (t->find_hit_row < 0)
        t->find_match_idx = 0;
}

static void term_find_from(struct term_state *t, int start_row, int start_col) {
    if (t->find_len <= 0) {
        t->find_hit_row = t->find_hit_col = -1;
        t->find_match_total = t->find_match_idx = 0;
        return;
    }
    int row = start_row;
    int col = start_col;
    if (row < 0)
        row = 0;
    if (row >= (int)TERM_ROWS)
        row = 0;
    if (col < 0)
        col = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (; row < (int)TERM_ROWS; row++) {
            const char *line = t->lines[row];
            for (; col < TERM_COLS; col++) {
                if (term_find_match_at(line, col, t->find_needle, t->find_len)) {
                    t->find_hit_row = row;
                    t->find_hit_col = col;
                    term_find_update_meta(t);
                    return;
                }
            }
            col = 0;
        }
        row = 0;
        col = 0;
    }
    t->find_hit_row = t->find_hit_col = -1;
    term_find_update_meta(t);
}

static void term_find_next(struct term_state *t) {
    if (t->find_len <= 0)
        return;
    int row = t->find_hit_row;
    int col = t->find_hit_col + 1;
    if (row < 0) {
        row = 0;
        col = 0;
    }
    term_find_from(t, row, col);
}

static void term_visible_rows(struct win *w, uint32_t *vis_out) {
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tab_h = term_tab_bar_h();
    uint32_t area_h = w->h > th + tab_h + desktop_u(16) ? w->h - th - tab_h - desktop_u(16) : ch;
    uint32_t vis = area_h / ch;
    if (vis > TERM_VIEW)
        vis = TERM_VIEW;
    if (vis < 1)
        vis = 1;
    if (vis_out)
        *vis_out = vis;
}

static void term_clamp_scroll(struct term_state *t, uint32_t vis) {
    int max_scroll = (int)t->row - (int)vis + 1;
    if (max_scroll < 0)
        max_scroll = 0;
    if (t->scroll > max_scroll)
        t->scroll = max_scroll;
    if (t->scroll < 0)
        t->scroll = 0;
}

static void term_scroll_to_row(struct term_state *t, uint32_t vis, int target_row) {
    if (target_row < 0)
        return;
    int max_scroll = (int)t->row - (int)vis + 1;
    if (max_scroll < 0)
        max_scroll = 0;
    int want = (int)t->row - target_row;
    if (want < 0)
        want = 0;
    if (want > max_scroll)
        want = max_scroll;
    t->scroll = want;
    term_clamp_scroll(t, vis);
}

static int term_mouse_cell(struct win *w, struct term_state *t, int32_t mx, int32_t my,
                           int *lr_out, int *col_out) {
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(12);
    uint32_t ty = w->y + th + term_tab_bar_h() + desktop_u(8);
    uint32_t vis = 0;
    term_visible_rows(w, &vis);
    term_clamp_scroll(t, vis);
    int start = (int)t->row - (int)vis + 1 - t->scroll;
    if (start < 0)
        start = 0;
    if (mx < (int32_t)tx || my < (int32_t)ty)
        return 0;
    int col = (int)((mx - (int32_t)tx) / (int32_t)cw);
    int lr = start + (int)((my - (int32_t)ty) / (int32_t)ch);
    if (col < 0 || col >= TERM_COLS || lr < 0 || lr >= (int)TERM_ROWS)
        return 0;
    if (lr_out)
        *lr_out = lr;
    if (col_out)
        *col_out = col;
    return 1;
}

static void term_paste_clipboard(void) {
    char buf[CLIPBOARD_MAX];
    size_t n = clipboard_get(buf, sizeof(buf));
    if (!n)
        return;
    term_paste_buf(buf, n);
}

void desktop_terminal_clear(void) {
    struct term_state *t = term_active();
    memset(t->lines, 0, sizeof(t->lines));
    t->row = 0;
    t->col = 0;
    t->scroll = 0;
    t->sel_row = t->sel_a = t->sel_b = -1;
    t->find_active = 0;
    t->find_len = 0;
    t->find_needle[0] = '\0';
    t->find_hit_row = t->find_hit_col = -1;
    t->find_match_total = t->find_match_idx = 0;
    t->caret_col = 0;
    t->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}

void term_clear_scrollback(void) {
    struct term_state *t = term_active();
    for (uint32_t i = 0; i < t->row && i < TERM_ROWS; i++)
        memset(t->lines[i], 0, sizeof(t->lines[i]));
    t->scroll = 0;
    t->sel_row = t->sel_a = t->sel_b = -1;
    t->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
    notify_push("Scrollback cleared");
    dirty_bits |= DIRTY_TOAST;
}

void desktop_terminal_copy(void) {
    struct term_state *t = term_active();
    if (!term_has_selection(t))
        return;
    term_copy_selection(t);
    notify_push_clipboard("selection");
    dirty_bits |= DIRTY_TOAST;
}

static void term_paste_buf(const char *buf, size_t n) {
    if (!buf || !n)
        return;
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\r')
            c = '\n';
        if (c >= 32 || c == '\n' || c == '\t' || c == '\b')
            shell_feed_key((unsigned char)c);
    }
}

void desktop_terminal_paste_previous(void) {
    char buf[CLIPBOARD_MAX];
    size_t n = clipboard_get_previous(buf, sizeof(buf));
    if (!n) {
        notify_push("No previous clipboard");
        dirty_bits |= DIRTY_TOAST;
        return;
    }
    term_paste_buf(buf, n);
    term_active()->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
    notify_push("Pasted previous clipboard");
    dirty_bits |= DIRTY_TOAST;
}

void desktop_terminal_paste(void) {
    term_paste_clipboard();
    term_active()->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}

int desktop_terminal_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 9)
        return 0;
    struct term_state *t = term_active();
    int has_sel = term_has_selection(t);
    static char copysel_lbl[28];
    snprintf(copysel_lbl, sizeof(copysel_lbl), "Copy on select: %s",
             term_copy_on_select ? "on" : "off");
    items[0].label = "Copy selection";
    items[0].enabled = has_sel;
    items[0].separator = 0;
    items[0].action_id = CTX_ACT_TERM_COPY;
    items[1].label = "Paste";
    items[1].enabled = clipboard_has();
    items[1].separator = 0;
    items[1].action_id = CTX_ACT_TERM_PASTE;
    items[2].label = "Find in buffer (Ctrl+F)";
    items[2].enabled = 1;
    items[2].separator = 0;
    items[2].action_id = CTX_ACT_TERM_FIND;
    items[3].label = copysel_lbl;
    items[3].enabled = 1;
    items[3].separator = 0;
    items[3].action_id = CTX_ACT_TERM_COPYSEL;
    items[4].label = "Clear scrollback";
    items[4].enabled = 1;
    items[4].separator = 0;
    items[4].action_id = CTX_ACT_TERM_CLEAR_SB;
    items[5].label = "Clear all";
    items[5].enabled = 1;
    items[5].separator = 0;
    items[5].action_id = CTX_ACT_TERM_CLEAR;
    items[6].label = "New Terminal";
    items[6].enabled = 1;
    items[6].separator = 0;
    items[6].action_id = CTX_ACT_TERM_NEW;
    items[7].label = NULL;
    items[7].enabled = 0;
    items[7].separator = 1;
    items[7].action_id = CTX_ACT_NONE;
    items[8].label = "Close window";
    items[8].enabled = 1;
    items[8].separator = 0;
    items[8].action_id = CTX_ACT_CLOSE;
    return 9;
}

int desktop_terminal_ctx_action(int action_id) {
    switch (action_id) {
    case CTX_ACT_TERM_NEW:
        desktop_open_app(APP_TERM);
        return 1;
    case CTX_ACT_TERM_COPY:
        desktop_terminal_copy();
        return 1;
    case CTX_ACT_TERM_PASTE:
        desktop_terminal_paste();
        return 1;
    case CTX_ACT_TERM_CLEAR:
        desktop_terminal_clear();
        shell_redraw_prompt();
        return 1;
    case CTX_ACT_TERM_CLEAR_SB:
        term_clear_scrollback();
        return 1;
    case CTX_ACT_TERM_FIND: {
        struct term_state *ts = term_active();
        ts->find_active = 1;
        ts->full_redraw = 1;
        dirty_bits |= DIRTY_TERM;
        term_mark_active_surf_dirty();
        return 1;
    }
    case CTX_ACT_TERM_COPYSEL:
        term_copy_on_select = !term_copy_on_select;
        return 1;
    default:
        return 0;
    }
}

int desktop_terminal_find_active(void) {
    return term_active()->find_active;
}

void desktop_terminal_find_close(void) {
    struct term_state *t = term_active();
    if (!t->find_active)
        return;
    t->find_active = 0;
    t->find_len = 0;
    t->find_needle[0] = '\0';
    t->find_hit_row = t->find_hit_col = -1;
    t->find_match_total = t->find_match_idx = 0;
    t->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}

void desktop_terminal_select_end(void) {
    if (!term_copy_on_select || !term_has_selection(term_active()))
        return;
    term_copy_selection(term_active());
    notify_push_clipboard("selection");
    dirty_bits |= DIRTY_TOAST;
}

static void term_scroll_up_buf(struct term_state *t) {
    for (uint32_t r = 1; r < TERM_ROWS; r++)
        memcpy(t->lines[r - 1], t->lines[r], TERM_COLS + 1);
    memset(t->lines[TERM_ROWS - 1], 0, TERM_COLS + 1);
    if (t->row > 0)
        t->row--;
}

void gui_term_putc(char c) {
    struct term_state *t = term_active();
    t->scroll = 0;
    if (c == '\n') {
        t->col = 0;
        t->row++;
        if (t->row >= TERM_ROWS)
            term_scroll_up_buf(t);
        t->full_redraw = 1;
        dirty_bits |= DIRTY_TERM;
        term_mark_active_surf_dirty();
        return;
    }
    if (c == '\b') {
        if (t->col > 0) {
            t->col--;
            t->lines[t->row][t->col] = '\0';
            t->dirty_col = t->col;
            t->dirty_row = t->row;
            t->cell_dirty = 1;
            t->prev_caret_col = t->col + 1;
        }
        dirty_bits |= DIRTY_TERM;
        term_mark_active_surf_dirty();
        return;
    }
    if (c < 32)
        return;
    if (t->col >= TERM_COLS) {
        t->col = 0;
        t->row++;
        if (t->row >= TERM_ROWS)
            term_scroll_up_buf(t);
        t->full_redraw = 1;
    }
    t->dirty_col = t->col;
    t->dirty_row = t->row;
    t->prev_caret_col = t->col;
    t->lines[t->row][t->col++] = c;
    t->lines[t->row][t->col] = '\0';
    t->cell_dirty = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}

void desktop_terminal_init(void) {
    active_term = -1;
    memset(term_wins, 0, sizeof(term_wins));
}

void desktop_terminal_draw(struct win *w) {
    term_sync_ui_scale();
    int slot = (int)(w - wins);
    if (slot < 0 || slot >= MAX_WINS)
        return;
    struct term_state *t = term_slot(slot);
    if (!t->inited)
        desktop_term_reset_slot(slot);
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    term_draw_tab_strip(w, slot);
    uint32_t tx = w->x + desktop_u(12);
    uint32_t ty = w->y + th + term_tab_bar_h() + desktop_u(8);
    uint32_t bg = desktop_color_bg();
    uint32_t tab_h = term_tab_bar_h();
    uint32_t area_h = w->h > th + tab_h + desktop_u(16) ? w->h - th - tab_h - desktop_u(16) : ch;
    uint32_t vis = area_h / ch;
    if (vis > TERM_VIEW)
        vis = TERM_VIEW;
    if (vis < 1)
        vis = 1;
    term_clamp_scroll(t, vis);
    int start = (int)t->row - (int)vis + 1 - t->scroll;
    if (start < 0)
        start = 0;

    if (!t->full_redraw && t->cell_dirty && t->scroll == 0 && t->sel_a < 0 &&
        t->find_len == 0 && !t->find_active) {
        int lr = (int)t->dirty_row;
        int caret_row = (int)t->row - start;
        int dirty_vis = lr - start;
        if (dirty_vis >= 0 && dirty_vis < (int)vis) {
            uint32_t c = t->dirty_col;
            char chv = (c < TERM_COLS) ? t->lines[lr][c] : '\0';
            if (chv)
                fb_draw_char(tx + c * cw, ty + (uint32_t)dirty_vis * ch, chv, desktop_color_fg(), bg);
            else
                fb_fill_rect(tx + c * cw, ty + (uint32_t)dirty_vis * ch, cw, ch, bg);
            if (t->prev_caret_col < TERM_COLS)
                fb_fill_rect(tx + t->prev_caret_col * cw,
                             ty + (uint32_t)caret_row * ch, desktop_u(2), ch, bg);
            if (caret_row >= 0 && caret_row < (int)vis) {
                uint32_t cx = t->caret_col < TERM_COLS ? t->caret_col : t->col;
                fb_fill_rect(tx + cx * cw, ty + (uint32_t)caret_row * ch, desktop_u(2), ch, desktop_color_accent());
            }
            t->cell_dirty = 0;
            return;
        }
        t->full_redraw = 1;
    }

    fb_fill_rect(tx, ty, w->w > desktop_u(24) ? w->w - desktop_u(24) : w->w, vis * ch + desktop_u(4), bg);
    for (uint32_t r = 0; r < vis; r++) {
        int lr = start + (int)r;
        if (lr < 0 || lr >= (int)TERM_ROWS)
            continue;
        const char *s = t->lines[lr];
        for (uint32_t c = 0; s[c] && c < TERM_COLS; c++) {
            uint32_t fg = desktop_color_fg();
            uint32_t cell_bg = bg;
            int selected = term_has_selection(t) && lr == t->sel_row &&
                           (int)c >= t->sel_a && (int)c <= t->sel_b;
            int find_hit = t->find_len > 0 &&
                           term_find_match_at(s, (int)c, t->find_needle, t->find_len);
            int find_cur = find_hit && lr == t->find_hit_row &&
                           (int)c >= t->find_hit_col &&
                           (int)c < t->find_hit_col + t->find_len;
            if (selected) {
                cell_bg = desktop_color_accent();
                fg = desktop_color_bg();
            } else if (find_cur) {
                cell_bg = desktop_color_accent();
                fg = desktop_color_bg();
            } else if (find_hit) {
                cell_bg = desktop_color_title();
                fg = desktop_color_fg();
            }
            fb_draw_char(tx + c * cw, ty + r * ch, s[c], fg, cell_bg);
        }
    }
    int caret_row = (int)t->row - start;
    if (t->scroll == 0 && caret_row >= 0 && caret_row < (int)vis) {
        uint32_t cx = t->caret_col < TERM_COLS ? t->caret_col : t->col;
        fb_fill_rect(tx + cx * cw, ty + (uint32_t)caret_row * ch, desktop_u(2), ch, desktop_color_accent());
    }
    uint32_t hint_y = ty + vis * ch + desktop_u(2);
    if (hint_y + ch <= w->y + w->h) {
        uint32_t hint_w = w->w > desktop_u(24) ? w->w - desktop_u(24) : w->w;
        if (t->find_active) {
            char fbar[72];
            if (t->find_len > 0 && t->find_match_total > 0)
                snprintf(fbar, sizeof(fbar), "Find: %s  %d/%d  Enter=next  Esc=close",
                         t->find_needle, t->find_match_idx, t->find_match_total);
            else
                snprintf(fbar, sizeof(fbar), "Find: %s  Enter=next  Esc=close",
                         t->find_needle);
            fb_draw_string_fit(tx, hint_y, hint_w, fbar, desktop_color_accent(), bg);
        } else if (term_has_selection(t)) {
            fb_draw_string(tx, hint_y, "Ctrl+C copy  Ctrl+V paste", desktop_color_dim(), bg);
        } else if (t->scroll > 0) {
            char sb[48];
            snprintf(sb, sizeof(sb), "^ scrollback %d lines (Dn/latest)", t->scroll);
            fb_draw_string(tx, hint_y, sb, desktop_color_dim(), bg);
        }
    }
    t->full_redraw = 0;
    t->cell_dirty = 0;
}

int desktop_terminal_key(int key) {
    if (keyboard_ctrl_down() && keyboard_shift_down() && (key == 't' || key == 'T')) {
        if (active_term >= 0)
            term_new_tab(active_term);
        return 1;
    }
    struct term_state *tt = term_active();
    uint32_t vis = 0;
    if (active_term >= 0 && active_term < MAX_WINS && wins[active_term].open)
        term_visible_rows(&wins[active_term], &vis);

    if (key == 6 || (keyboard_ctrl_down() && (key == 'f' || key == 'F'))) {
        if (!tt->find_active) {
            tt->find_active = 1;
            tt->find_len = 0;
            tt->find_needle[0] = '\0';
            tt->find_hit_row = tt->find_hit_col = -1;
        } else if (tt->find_len > 0) {
            term_find_next(tt);
            if (tt->find_hit_row >= 0)
                term_scroll_to_row(tt, vis, tt->find_hit_row);
        }
        tt->full_redraw = 1;
        dirty_bits |= DIRTY_TERM;
        term_mark_active_surf_dirty();
        return 1;
    }

    if (tt->find_active) {
        if (key == '\n' || key == KEY_TAB) {
            if (tt->find_len > 0) {
                term_find_next(tt);
                if (tt->find_hit_row >= 0)
                    term_scroll_to_row(tt, vis, tt->find_hit_row);
            }
        } else if (key == '\b') {
            if (tt->find_len > 0) {
                tt->find_needle[--tt->find_len] = '\0';
                term_find_from(tt, 0, 0);
                if (tt->find_hit_row >= 0)
                    term_scroll_to_row(tt, vis, tt->find_hit_row);
            }
        } else if (key >= 32 && key < 127 && tt->find_len < TERM_FIND_MAX) {
            tt->find_needle[tt->find_len++] = (char)key;
            tt->find_needle[tt->find_len] = '\0';
            term_find_from(tt, 0, 0);
            if (tt->find_hit_row >= 0)
                term_scroll_to_row(tt, vis, tt->find_hit_row);
        } else if (key == KEY_UP) {
            tt->scroll++;
            term_clamp_scroll(tt, vis);
        } else if (key == KEY_DOWN) {
            if (tt->scroll > 0)
                tt->scroll--;
            term_clamp_scroll(tt, vis);
        }
        tt->full_redraw = 1;
        dirty_bits |= DIRTY_TERM;
        term_mark_active_surf_dirty();
        return 1;
    }

    if (key == KEY_TAB)
        key = '\t';
    if (key == KEY_UP) {
        tt->scroll++;
        term_clamp_scroll(tt, vis);
    } else if (key == KEY_DOWN) {
        if (tt->scroll > 0)
            tt->scroll--;
        term_clamp_scroll(tt, vis);
    } else if (key == 3 && term_has_selection(tt)) {
        desktop_terminal_copy();
    } else if (key == 22) {
        desktop_terminal_paste();
    } else if (key == 12) {
        desktop_terminal_clear();
        shell_redraw_prompt();
        return 1;
    } else {
        if (key >= 32 || key == '\b' || key == '\n' || key == '\t')
            tt->scroll = 0;
        shell_feed_key(key);
    }
    tt->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
    return 1;
}

void desktop_terminal_wheel(int wheel) {
    struct term_state *tt = term_active();
    uint32_t vis = 0;
    if (active_term >= 0 && active_term < MAX_WINS && wins[active_term].open)
        term_visible_rows(&wins[active_term], &vis);
    tt->scroll += wheel > 0 ? 3 : -3;
    term_clamp_scroll(tt, vis);
    tt->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}

int desktop_terminal_click(struct win *w, int32_t mx, int32_t my, int drag) {
    int slot = (int)(w - wins);
    if (slot < 0 || slot >= MAX_WINS)
        return 0;
    if (term_tab_click(w, slot, mx, my))
        return 1;
    struct term_state *t = term_slot(slot);
    if (!t->inited)
        desktop_term_reset_slot(slot);
    desktop_term_activate(slot);
    int lr = 0, col = 0;
    if (!term_mouse_cell(w, t, mx, my, &lr, &col))
        return 0;
    if (drag) {
        if (t->sel_a < 0 || lr != t->sel_row) {
            t->sel_row = lr;
            t->sel_a = col;
            t->sel_b = col;
        } else if (col < t->sel_a) {
            t->sel_a = col;
        } else {
            t->sel_b = col;
        }
    } else {
        t->sel_row = lr;
        t->sel_a = col;
        t->sel_b = col;
    }
    t->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_surf_dirty(slot, t);
    return 1;
}
