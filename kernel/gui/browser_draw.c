#include "browser.h"
#include "browser_internal.h"
#include "browser_isolation.h"
#include "fb.h"
#include "theme.h"
#include "util.h"
#include "css.h"
#include "timer.h"

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
    if (t->fetching && cap > 2) {
        out[i++] = '~';
        out[i++] = ' ';
    }
    for (size_t si = 0; src[si] && i + 1 < cap; si++) {
        char c = src[si];
        if (c == '\n' || c == '\r')
            continue;
        out[i++] = c;
    }
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

    uint32_t cw = fb_cell_w();
    uint32_t close_w = (ntabs > 1) ? cw + 4 : 0;
    memset(hit_tab_close_x, 0, sizeof(hit_tab_close_x));
    memset(hit_tab_close_w, 0, sizeof(hit_tab_close_w));

    for (int i = 0; i < ntabs; i++) {
        uint32_t tx = x + (uint32_t)i * hit_tab_w;
        uint32_t bg = (i == active) ? th->surface : th->border;
        fb_fill_rect(tx + 1, y + 1, hit_tab_w - 2, hit_tab_h - 2, bg);
        if (i == active)
            fb_fill_rect(tx + 1, y + hit_tab_h - 3, hit_tab_w - 2, 2, th->accent);
        char lab[BR_TITLE_MAX];
        tab_label(&tabs[i], lab, sizeof(lab));
        uint32_t label_max = hit_tab_w > close_w + 16 ? hit_tab_w - close_w - 16 : cw * 4;
        fb_draw_string_fit(tx + 4, y + 3, label_max, lab,
                           (i == active) ? th->fg : th->dim, bg);
        if (close_w > 0) {
            uint32_t cx = tx + hit_tab_w - close_w - 4;
            hit_tab_close_x[i] = cx - x;
            hit_tab_close_w[i] = close_w + 4;
            fb_draw_string(cx, y + 3, "x", th->dim, bg);
        }
    }
    hit_plus_x = (uint32_t)ntabs * hit_tab_w;
    if (ntabs < BR_MAX_TABS) {
        fb_fill_rect(x + hit_plus_x + 2, y + 2, hit_tab_h - 2, hit_tab_h - 4, th->surface);
        fb_draw_string(x + hit_plus_x + 6, y + 3, "+", th->accent, th->surface);
    }

    uint32_t bar_y = y + hit_tab_h + 4;
    hit_bar_y = hit_tab_h + 4;
    hit_bar_h = ch + 4;
    uint32_t nav_btn_w = cw * 2 + 8;

    hit_back_x = pad;
    hit_back_w = nav_btn_w;
    hit_fwd_x = hit_back_x + hit_back_w + 4;
    hit_fwd_w = nav_btn_w;
    hit_go_x = hit_fwd_x + hit_fwd_w + 4;
    hit_go_w = cw * 4;

    int can_back = t->nhist_back > 0 || t->prev_url[0];
    int can_fwd = t->nhist_fwd > 0 || t->forward_url[0];
    uint32_t back_bg = can_back ? th->surface : th->border;
    uint32_t fwd_bg = can_fwd ? th->surface : th->border;
    uint32_t back_fg = can_back ? th->fg : th->dim;
    uint32_t fwd_fg = can_fwd ? th->fg : th->dim;

    fb_fill_rect(x + hit_back_x, bar_y, hit_back_w, ch + 4, back_bg);
    fb_draw_string(x + hit_back_x + 6, bar_y + 2, "<", back_fg, back_bg);
    fb_fill_rect(x + hit_fwd_x, bar_y, hit_fwd_w, ch + 4, fwd_bg);
    fb_draw_string(x + hit_fwd_x + 6, bar_y + 2, ">", fwd_fg, fwd_bg);
    fb_fill_rect(x + hit_go_x, bar_y, hit_go_w, ch + 4, th->accent);
    fb_draw_string(x + hit_go_x + 4, bar_y + 2, "Go", th->bg, th->accent);

    uint32_t ax = x + hit_go_x + hit_go_w + 6;
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

    if (t->fetching) {
        uint32_t bar_x = ax;
        uint32_t bar_w = aw;
        uint32_t bar_h = ch + 2;
        uint32_t prog_y = bar_y + ch + 6;
        fb_fill_rect(bar_x, prog_y, bar_w, bar_h, th->border);
        /* Honest spinner (not a fake % bar). */
        static const char *frames[] = { "|", "/", "-", "\\" };
        uint64_t elapsed = timer_ticks() - t->fetch_start;
        const char *spin = frames[(unsigned)(elapsed / 8) % 4];
        char spinbuf[32];
        snprintf(spinbuf, sizeof(spinbuf), "%s fetching...", spin);
        fb_draw_string(bar_x + 4, prog_y + 1, spinbuf, th->accent, th->border);
    }

    hit_bm_y = hit_bm_h = 0;
    memset(hit_bm_x, 0, sizeof(hit_bm_x));
    memset(hit_bm_w, 0, sizeof(hit_bm_w));
    uint32_t bm_extra = 0;
    int nbm = browser_bookmark_count();
    if (nbm > 0) {
        uint32_t bm_y = bar_y + ch + 8;
        hit_bm_y = bm_y - y;
        hit_bm_h = ch + 2;
        bm_extra = hit_bm_h + 4;
        fb_fill_rect(x + pad, bm_y, w - pad * 2, hit_bm_h, th->border);
        uint32_t bx = x + pad + 4;
        int shown = 0;
        for (int i = 0; i < nbm && shown < 4; i++) {
            const char *title = browser_bookmark_title(i);
            if (!title)
                continue;
            char lab[14];
            size_t li = 0;
            for (; title[li] && li + 1 < sizeof(lab) && li < 10; li++)
                lab[li] = title[li];
            lab[li] = '\0';
            uint32_t chip_w = (uint32_t)strlen(lab) * cw + 12;
            if (bx + chip_w > x + w - pad - cw * 3)
                break;
            hit_bm_x[shown] = bx - x;
            hit_bm_w[shown] = chip_w;
            fb_fill_rect(bx, bm_y + 1, chip_w, ch, th->surface);
            fb_draw_string(bx + 4, bm_y + 1, lab, th->accent, th->surface);
            bx += chip_w + 4;
            shown++;
        }
        if (nbm > shown) {
            char ov[8];
            snprintf(ov, sizeof(ov), "+%d", nbm - shown);
            uint32_t ow = (uint32_t)strlen(ov) * cw + 10;
            if (bx + ow <= x + w - pad) {
                fb_fill_rect(bx, bm_y + 1, ow, ch, th->border);
                fb_draw_string(bx + 4, bm_y + 1, ov, th->dim, th->border);
            }
        }
    }

    uint32_t chrome_h = hit_tab_h + ch + pad * 2 + 8 + bm_extra;

    uint32_t console_extra = 0;
    if (t->jsh.console_n > 0 && (t->show_console || t->js_ok)) {
        int nlines = t->jsh.console_n < 4 ? t->jsh.console_n : 4;
        console_extra = (uint32_t)(nlines + 1) * (ch + 2) + 6;
    }

    uint32_t page_y = y + chrome_h;
    uint32_t page_h = h - chrome_h - ch - pad - 2 - console_extra;
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
            uint32_t draw_y = page_y + pad + (uint32_t)(by < 0 ? 0 : by);
            if (draw_y >= max_y)
                break;
            uint32_t fg = css_to_rgb(&b->style.color, t->page_fg);
            uint32_t bx = cx + (uint32_t)b->x;
            uint32_t bw = (uint32_t)(b->w > 0 ? b->w : (int)content_w);
            uint32_t bh = (uint32_t)(b->h > 0 ? b->h : (int)ch);
            if (b->kind == 1) {
                /* Image: blit RGB if decoded, else placeholder. */
                int painted = 0;
                for (int im = 0; im < t->nimages; im++) {
                    if (t->images[im].node_id != b->node_id || !t->images[im].rgb)
                        continue;
                    uint32_t iw = t->images[im].pw;
                    uint32_t ih = t->images[im].ph;
                    if (iw > bw)
                        iw = bw;
                    if (ih > bh)
                        ih = bh;
                    if (draw_y + ih > max_y)
                        ih = max_y > draw_y ? max_y - draw_y : 0;
                    for (uint32_t py = 0; py < ih; py++) {
                        for (uint32_t px = 0; px < iw; px++) {
                            const uint8_t *p =
                                t->images[im].rgb + ((size_t)py * t->images[im].pw + px) * 3;
                            fb_put_pixel(bx + px, draw_y + py, fb_rgb(p[0], p[1], p[2]));
                        }
                    }
                    painted = 1;
                    break;
                }
                if (!painted) {
                    fb_fill_rect(bx, draw_y, bw, bh, t->page_surface);
                    fb_draw_string(bx + 2, draw_y + 2, b->text, t->page_muted, t->page_surface);
                }
                continue;
            }
            if (b->kind == 2 || b->kind == 3) {
                uint32_t bgc = (b->node_id == t->focus_node) ? t->page_accent : t->page_surface;
                fb_fill_rect(bx, draw_y, bw, bh, bgc);
                const char *label = b->text;
                if (b->node_id == t->focus_node && t->focus_value[0])
                    label = t->focus_value;
                fb_draw_string(bx + 4, draw_y + 4, label, t->page_fg, bgc);
                continue;
            }
            if (b->style.background.set) {
                uint32_t bgc = css_to_rgb(&b->style.background, t->page_surface);
                fb_fill_rect(bx, draw_y, bw, bh, bgc);
            }
            if (b->style.border)
                fb_fill_rect(bx, draw_y + bh - 1, bw, 1, t->page_muted);
            fb_draw_string(bx, draw_y, b->text, fg, t->page_bg);
            (void)cy;
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
        fb_draw_string(cx, max_y - ch, "j/k scroll  b/f back/fwd  c console",
                       t->page_muted, t->page_bg);

    hit_retry_w = hit_retry_h = 0;
    hit_accept_w = hit_accept_h = 0;
    hit_forget_w = hit_forget_h = 0;
    if (t->show_retry) {
        uint32_t btn_w = fb_cell_w() * 8;
        uint32_t btn_h = ch + 6;
        uint32_t gap = fb_cell_w();
        /* Store client-relative (matching tabs/nav); draw in absolute. */
        uint32_t ay = cy + ch;
        if (ay + btn_h > max_y - ch)
            ay = max_y - ch - btn_h - pad;
        hit_retry_x = cx - x;
        hit_retry_y = ay - y;
        hit_retry_w = btn_w;
        hit_retry_h = btn_h;
        fb_fill_rect(x + hit_retry_x, y + hit_retry_y, btn_w, btn_h, th->accent);
        fb_draw_string(x + hit_retry_x + 8, y + hit_retry_y + 3, "Retry", th->bg, th->accent);
        if (t->show_tls_accept) {
            uint32_t aw = fb_cell_w() * 10;
            uint32_t fw = fb_cell_w() * 10;
            hit_accept_x = hit_retry_x + btn_w + gap;
            hit_accept_y = hit_retry_y;
            hit_accept_w = aw;
            hit_accept_h = btn_h;
            fb_fill_rect(x + hit_accept_x, y + hit_accept_y, aw, btn_h, th->accent);
            fb_draw_string(x + hit_accept_x + 8, y + hit_accept_y + 3, "Accept", th->bg, th->accent);
            hit_forget_x = hit_accept_x + aw + gap;
            hit_forget_y = hit_retry_y;
            hit_forget_w = fw;
            hit_forget_h = btn_h;
            fb_fill_rect(x + hit_forget_x, y + hit_forget_y, fw, btn_h, t->page_muted);
            fb_draw_string(x + hit_forget_x + 8, y + hit_forget_y + 3, "Forget", th->bg,
                           t->page_muted);
        }
    }

    if (t->jsh.console_n > 0 && (t->show_console || t->js_ok)) {
        int nlines = 0;
        int start = t->jsh.console_n - (t->jsh.console_n < 4 ? t->jsh.console_n : 4);
        if (start < 0)
            start = 0;
        for (int i = start; i < t->jsh.console_n; i++) {
            int slot = i % 8;
            if (browser_console_line_visible(&t->jsh, t->jsh.console_log[slot]))
                nlines++;
        }
        if (nlines == 0)
            nlines = 1;
        console_extra = (uint32_t)(nlines + 1) * (ch + 2) + 6;
        uint32_t con_y = y + h - ch - 4 - console_extra;
        fb_fill_rect(x + 2, con_y, w - 4, console_extra, th->border);
        char con_hdr[48];
        if (t->jsh.console_filter[0])
            snprintf(con_hdr, sizeof(con_hdr), "Console filter:%s", t->jsh.console_filter);
        else
            snprintf(con_hdr, sizeof(con_hdr), "Console");
        fb_draw_string(x + pad, con_y + 2, con_hdr, th->accent, th->border);
        uint32_t ly = con_y + ch + 4;
        for (int i = start; i < t->jsh.console_n; i++) {
            int slot = i % 8;
            if (!browser_console_line_visible(&t->jsh, t->jsh.console_log[slot]))
                continue;
            fb_draw_string(x + pad + 4, ly, t->jsh.console_log[slot], th->dim, th->border);
            ly += ch + 2;
        }
    } else if (t->jsh.console_n > 0 && !t->show_console) {
        fb_draw_string(x + w - pad - cw * 10, y + h - ch - 4,
                       "c:console", th->dim, th->surface);
    }

    uint32_t st_y = y + h - ch - 4 - console_extra;
    fb_fill_rect(x, st_y - ch - 4, w, ch * 2 + 8, th->surface);
    char iso[80];
    browser_isolation_status_line(iso, sizeof(iso));
    fb_draw_string(x + pad, st_y - ch - 2, iso, th->dim, th->surface);
    fb_draw_string(x + pad, st_y, t->status, th->dim, th->surface);

    needs_redraw = 0;
}
