#include "desktop_internal.h"
#include "gui.h"
#include "fb.h"
#include "shell.h"
#include "keyboard.h"
#include "util.h"

struct term_state {
    char lines[TERM_ROWS][TERM_COLS + 1];
    uint32_t row, col;
    int scroll;
    int sel_a, sel_b;
    uint32_t caret_col;
    int inited;
    int full_redraw;
    int cell_dirty;
    uint32_t dirty_col, dirty_row;
    uint32_t prev_caret_col;
};

static struct term_state terms[MAX_WINS];
static int active_term;

static void term_mark_cell_surf_dirty(int slot, struct term_state *t) {
    if (slot < 0 || slot >= MAX_WINS || !wins[slot].open)
        return;
    struct win *w = &wins[slot];
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = desktop_u(12);
    uint32_t ty = th + desktop_u(8);
    uint32_t area_h = w->h > th + desktop_u(16) ? w->h - th - desktop_u(16) : ch;
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
    if (t->full_redraw || t->cell_dirty == 0 || t->scroll != 0 || t->sel_a >= 0)
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
        term_mark_surf_dirty(slot, &terms[slot]);
    else
        desktop_mark_focus_surf_dirty();
}

static struct term_state *term_active(void) {
    if (active_term >= 0 && active_term < MAX_WINS &&
        wins[active_term].open && wins[active_term].kind == APP_TERM)
        return &terms[active_term];
    for (int i = 0; i < MAX_WINS; i++) {
        if (wins[i].open && wins[i].kind == APP_TERM) {
            active_term = i;
            return &terms[i];
        }
    }
    active_term = 0;
    return &terms[0];
}

void desktop_term_reset_slot(int slot) {
    if (slot < 0 || slot >= MAX_WINS)
        return;
    memset(&terms[slot], 0, sizeof(terms[slot]));
    terms[slot].sel_a = terms[slot].sel_b = -1;
    terms[slot].inited = 1;
    terms[slot].full_redraw = 1;
}

void desktop_term_activate(int slot) {
    if (slot < 0 || slot >= MAX_WINS)
        return;
    if (!wins[slot].open || wins[slot].kind != APP_TERM)
        return;
    active_term = slot;
    if (!terms[slot].inited)
        desktop_term_reset_slot(slot);
}

void gui_term_reset(void) {
    struct term_state *t = term_active();
    memset(t->lines, 0, sizeof(t->lines));
    t->row = 0;
    t->col = 0;
    t->scroll = 0;
    t->sel_a = t->sel_b = -1;
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
        t->sel_a = (int)prompt_len + sel_a;
        t->sel_b = (int)prompt_len + sel_b;
    } else {
        t->sel_a = t->sel_b = -1;
    }
    t->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}

int desktop_active_term_index(void) {
    return active_term;
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
    memset(terms, 0, sizeof(terms));
}

void desktop_terminal_draw(struct win *w) {
    int slot = (int)(w - wins);
    if (slot < 0 || slot >= MAX_WINS)
        return;
    struct term_state *t = &terms[slot];
    if (!t->inited)
        desktop_term_reset_slot(slot);
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(12);
    uint32_t ty = w->y + th + desktop_u(8);
    uint32_t bg = desktop_color_bg();
    uint32_t area_h = w->h > th + desktop_u(16) ? w->h - th - desktop_u(16) : ch;
    uint32_t vis = area_h / ch;
    if (vis > TERM_VIEW)
        vis = TERM_VIEW;
    if (vis < 1)
        vis = 1;
    int start = (int)t->row - (int)vis + 1 - t->scroll;
    if (start < 0)
        start = 0;

    if (!t->full_redraw && t->cell_dirty && t->scroll == 0 && t->sel_a < 0) {
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
            if (lr == (int)t->row && t->sel_a >= 0 &&
                (int)c >= t->sel_a && (int)c <= t->sel_b) {
                cell_bg = desktop_color_accent();
                fg = desktop_color_bg();
            }
            fb_draw_char(tx + c * cw, ty + r * ch, s[c], fg, cell_bg);
        }
    }
    int caret_row = (int)t->row - start;
    if (t->scroll == 0 && caret_row >= 0 && caret_row < (int)vis) {
        uint32_t cx = t->caret_col < TERM_COLS ? t->caret_col : t->col;
        fb_fill_rect(tx + cx * cw, ty + (uint32_t)caret_row * ch, desktop_u(2), ch, desktop_color_accent());
    }
    t->full_redraw = 0;
    t->cell_dirty = 0;
}

int desktop_terminal_key(int key) {
    struct term_state *tt = term_active();
    if (key == KEY_TAB)
        key = '\t';
    if (key == KEY_UP) {
        tt->scroll++;
        tt->full_redraw = 1;
        dirty_bits |= DIRTY_TERM;
        term_mark_active_surf_dirty();
    } else if (key == KEY_DOWN) {
        if (tt->scroll > 0)
            tt->scroll--;
        tt->full_redraw = 1;
        dirty_bits |= DIRTY_TERM;
        term_mark_active_surf_dirty();
    } else {
        shell_feed_key(key);
        dirty_bits |= DIRTY_TERM;
        term_mark_active_surf_dirty();
    }
    return 1;
}

void desktop_terminal_wheel(int wheel) {
    struct term_state *tt = term_active();
    tt->scroll += wheel > 0 ? 3 : -3;
    if (tt->scroll < 0)
        tt->scroll = 0;
    tt->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    term_mark_active_surf_dirty();
}
