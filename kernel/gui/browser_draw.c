#include "browser.h"
#include "browser_internal.h"
#include "fb.h"
#include "theme.h"
#include "util.h"
#include "css.h"

static uint32_t block_fg(struct br_tab *t, enum br_kind k) {
    switch (k) {
    case BR_H1:
    case BR_H2:
        return t->page_accent;
    case BR_H3:
        return t->page_muted;
    case BR_LINK:
        return t->page_link;
    case BR_CODE:
        return t->page_accent;
    case BR_QUOTE:
        return t->page_muted;
    default:
        return t->page_fg;
    }
}

static uint32_t block_gap(enum br_kind k, uint32_t ch) {
    switch (k) {
    case BR_H1:
        return ch / 2 + 6;
    case BR_H2:
        return ch / 3 + 4;
    case BR_HR:
        return ch / 2 + 4;
    case BR_SPACER:
        return ch / 3;
    default:
        return 3;
    }
}

static void draw_wrapped(struct br_tab *t, uint32_t x, uint32_t *cy, uint32_t max_w,
                         uint32_t max_y, const char *text, uint32_t fg, uint32_t bg,
                         uint32_t ch, int underline, int code_bg) {
    uint32_t cw = fb_cell_w();
    int cols = (int)(max_w / cw);
    if (cols < 8)
        cols = 8;
    if (cols > 80)
        cols = 80;

    const char *p = text;
    while (*p && *cy + ch <= max_y) {
        char line[81];
        int n = 0;
        int last_sp = -1;
        while (p[n] && n < cols && n < 80) {
            if (p[n] == ' ')
                last_sp = n;
            line[n] = p[n];
            n++;
        }
        if (p[n] && last_sp > cols / 3) {
            n = last_sp;
            line[n] = '\0';
            p += n + 1;
        } else {
            line[n] = '\0';
            p += n;
        }
        while (*p == ' ')
            p++;

        uint32_t tw = (uint32_t)strlen(line) * cw;
        if (code_bg && tw > 0)
            fb_fill_rect(x - 2, *cy - 1, tw + 4, ch + 2, t->page_surface);
        fb_draw_string(x, *cy, line, fg, code_bg ? t->page_surface : bg);
        if (underline && tw > 0)
            fb_fill_rect(x, *cy + ch - 2, tw, 1, fg);
        *cy += ch + 2;
    }
}

static void tab_label(struct br_tab *t, char *out, size_t cap) {
    const char *src = t->title[0] ? t->title : t->url;
    size_t i = 0;
    for (; src[i] && i + 1 < cap && i < 12; i++)
        out[i] = src[i];
    out[i] = '\0';
}

static void draw_tls_padlock(uint32_t x, uint32_t y, uint32_t bg, uint32_t color) {
    fb_fill_rect(x + 1, y, 6, 2, color);
    fb_fill_rect(x + 1, y + 2, 2, 3, color);
    fb_fill_rect(x + 5, y + 2, 2, 3, color);
    fb_fill_rect(x + 1, y + 2, 6, 3, bg);
    fb_fill_rect(x, y + 5, 8, 6, color);
    fb_fill_rect(x + 2, y + 7, 4, 2, bg);
}

void browser_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const struct peak_theme *th = theme_get();
    struct br_tab *t = browser_cur();
    uint32_t ch = fb_cell_h();
    uint32_t pad = 6;
    if (!t->colors_set)
        browser_init_page_colors(t, "");

    fb_fill_rect(x, y, w, h, th->bg);

    hit_tab_y = 0;
    hit_tab_h = ch + 6;
    hit_tab_w = (w > 40) ? (w - 40) / BR_MAX_TABS : 40;
    if (hit_tab_w < 48)
        hit_tab_w = 48;
    fb_fill_rect(x, y, w, hit_tab_h, th->border);

    for (int i = 0; i < ntabs; i++) {
        uint32_t tx = x + (uint32_t)i * hit_tab_w;
        uint32_t bg = (i == active) ? th->surface : th->border;
        fb_fill_rect(tx + 1, y + 1, hit_tab_w - 2, hit_tab_h - 2, bg);
        if (i == active)
            fb_fill_rect(tx + 1, y + hit_tab_h - 3, hit_tab_w - 2, 2, th->accent);
        char lab[16];
        tab_label(&tabs[i], lab, sizeof(lab));
        fb_draw_string(tx + 4, y + 3, lab, (i == active) ? th->fg : th->dim, bg);
    }
    hit_plus_x = x + (uint32_t)ntabs * hit_tab_w;
    if (ntabs < BR_MAX_TABS) {
        fb_fill_rect(hit_plus_x + 2, y + 2, hit_tab_h - 2, hit_tab_h - 4, th->surface);
        fb_draw_string(hit_plus_x + 6, y + 3, "+", th->accent, th->surface);
    }

    uint32_t bar_y = y + hit_tab_h + 4;
    hit_bar_y = hit_tab_h + 4;
    hit_bar_h = ch + 4;
    hit_go_x = pad;
    hit_go_w = fb_cell_w() * 4;
    fb_fill_rect(x + pad, bar_y, hit_go_w, ch + 4, th->accent);
    fb_draw_string(x + pad + 4, bar_y + 2, "Go", th->bg, th->accent);

    uint32_t ax = x + pad + hit_go_w + 6;
    uint32_t aw = w - (ax - x) - pad;
    if ((int)aw < 40)
        aw = 40;
    fb_fill_rect(ax, bar_y, aw, ch + 4, editing ? th->title : th->surface);
    uint32_t url_x = ax + 4;
    uint32_t bar_bg = editing ? th->title : th->surface;
    if (t->tls_secure) {
        if (t->tls_verified) {
            draw_tls_padlock(url_x, bar_y + 2, bar_bg, th->accent);
            url_x += 10;
        } else {
            fb_draw_string(url_x, bar_y + 2, "!", th->danger, bar_bg);
            url_x += fb_cell_w() + 2;
        }
    }
    char show[BR_URL_MAX + 2];
    snprintf(show, sizeof(show), "%s%s", t->url, editing ? "_" : "");
    uint32_t max_chars = (aw > (url_x - ax)) ? (aw - (url_x - ax)) / fb_cell_w() : 0;
    if (max_chars > 3 && strlen(show) > max_chars) {
        show[max_chars - 1] = '\0';
        show[max_chars - 2] = '.';
        show[max_chars - 3] = '.';
    }
    fb_draw_string(url_x, bar_y + 2, show, th->fg, editing ? th->title : th->surface);

    uint32_t chrome_h = hit_tab_h + ch + pad * 2 + 8;

    uint32_t page_y = y + chrome_h;
    uint32_t page_h = h - chrome_h - ch - pad - 2;
    if ((int)page_h < (int)ch * 3)
        page_h = ch * 3;
    fb_fill_rect(x + 2, page_y, w - 4, page_h, t->page_bg);

    uint32_t cx = x + pad + 4;
    uint32_t cy = page_y + pad;
    uint32_t content_w = w - pad * 2 - 12;
    uint32_t max_y = page_y + page_h - pad;

    if (t->use_layout && t->nboxes > 0) {
        int scroll = t->scroll_y;
        for (int i = 0; i < t->nboxes && cy < max_y; i++) {
            struct css_box *b = &t->boxes[i];
            int by = b->y - scroll;
            if (by + b->h < 0)
                continue;
            uint32_t draw_y = cy + (uint32_t)(by < 0 ? 0 : by);
            if (draw_y >= max_y)
                break;
            uint32_t fg = css_to_rgb(&b->style.color, t->page_fg);
            if (b->style.background.set) {
                uint32_t bgc = css_to_rgb(&b->style.background, t->page_surface);
                fb_fill_rect(cx + (uint32_t)b->x, page_y + pad + (uint32_t)(by < 0 ? 0 : by),
                             (uint32_t)(b->w > 0 ? b->w : (int)content_w), (uint32_t)b->h, bgc);
            }
            fb_draw_string(cx + (uint32_t)b->x, page_y + pad + (uint32_t)(by < 0 ? 0 : by),
                           b->text, fg, t->page_bg);
            (void)draw_y;
        }
        cy = max_y;
    } else {
        int start = t->scroll_y;
        if (start < 0)
            start = 0;
        if (start > t->nblocks)
            start = t->nblocks;

        for (int i = start; i < t->nblocks && cy < max_y; i++) {
            enum br_kind k = (enum br_kind)t->blocks[i].kind;
            if (k == BR_SPACER) {
                cy += block_gap(k, ch);
                continue;
            }
            if (k == BR_HR) {
                uint32_t ly = cy + ch / 3;
                if (ly + 2 < max_y)
                    fb_fill_rect(cx, ly, content_w, 2, t->page_muted);
                cy += block_gap(k, ch) + 4;
                continue;
            }

            char prefix[8];
            prefix[0] = '\0';
            if (k == BR_LI)
                snprintf(prefix, sizeof(prefix), "  * ");
            else if (k == BR_QUOTE) {
                snprintf(prefix, sizeof(prefix), "  | ");
                fb_fill_rect(cx, cy, 3, ch, t->page_accent);
            }

            char line[BR_TEXT_MAX + 8];
            if (prefix[0])
                snprintf(line, sizeof(line), "%s%s", prefix, t->blocks[i].text);
            else
                snprintf(line, sizeof(line), "%s", t->blocks[i].text);

            uint32_t fg = block_fg(t, k);
            if (k == BR_H1) {
                draw_wrapped(t, cx, &cy, content_w, max_y, line, fg, t->page_bg, ch, 0, 0);
                uint32_t uw = content_w / 3;
                if (uw < 40)
                    uw = 40;
                if (cy > page_y + pad)
                    fb_fill_rect(cx, cy - 2, uw, 2, t->page_accent);
                cy += block_gap(k, ch);
            } else {
                draw_wrapped(t, cx, &cy, content_w, max_y, line, fg, t->page_bg, ch,
                             k == BR_LINK, k == BR_CODE);
                cy += block_gap(k, ch) / 2;
            }
        }
    }

    if ((t->nblocks > 0 || t->nboxes > 0) && cy >= max_y - ch)
        fb_draw_string(cx, max_y - ch, "j/k scroll  [/] tabs  t new  w close",
                       t->page_muted, t->page_bg);

    uint32_t st_y = y + h - ch - 4;
    fb_fill_rect(x, st_y - 2, w, ch + 6, th->surface);
    fb_draw_string(x + pad, st_y, t->status, th->dim, th->surface);

    needs_redraw = 0;
}
