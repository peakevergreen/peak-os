#include "browser.h"
#include "browser_internal.h"
#include "browser_js.h"
#include "css.h"
#include "dom.h"
#include "fb.h"
#include "util.h"

static int url_len_of(struct br_tab *t) {
    return (int)strlen(t->url);
}

int browser_is_local_host(const char *url) {
    const char *p = url;
    if (!strncmp(p, "http://", 7))
        p += 7;
    else if (!strncmp(p, "https://", 8))
        p += 8;
    if (!strncmp(p, "127.0.0.1", 9) || !strncmp(p, "localhost", 9))
        return 1;
    return 0;
}

void browser_normalize_url(const char *in, char *out, size_t out_cap) {
    while (*in == ' ' || *in == '\t')
        in++;
    if (!*in) {
        snprintf(out, out_cap, "http://127.0.0.1:18080/");
        return;
    }
    if (!strncmp(in, "http://", 7) || !strncmp(in, "https://", 8) ||
        !strncmp(in, "peak://", 7) || !strncmp(in, "peak:", 5) ||
        !strncmp(in, "about:", 6)) {
        snprintf(out, out_cap, "%s", in);
        return;
    }
    if (strchr(in, '.') && !strchr(in, ' ')) {
        if (!strncmp(in, "www.", 4))
            snprintf(out, out_cap, "http://%s/", in);
        else {
            const char *dot = strchr(in, '.');
            if (dot && strchr(dot + 1, '.'))
                snprintf(out, out_cap, "http://%s/", in);
            else
                snprintf(out, out_cap, "http://www.%s/", in);
        }
        return;
    }
    snprintf(out, out_cap, "%s", in);
}

void browser_select_tab(int i) {
    if (i < 0 || i >= ntabs || !tabs[i].used)
        return;
    active = i;
    editing = 0;
    needs_redraw = 1;
}

int browser_new_tab(const char *url) {
    if (ntabs >= BR_MAX_TABS)
        return -1;
    int i = ntabs++;
    memset(&tabs[i], 0, sizeof(tabs[i]));
    tabs[i].used = 1;
    dom_doc_init(&tabs[i].doc);
    css_sheet_init(&tabs[i].sheet);
    if (url)
        snprintf(tabs[i].url, sizeof(tabs[i].url), "%s", url);
    else
        snprintf(tabs[i].url, sizeof(tabs[i].url), "https://www.fark.com/");
    snprintf(tabs[i].title, sizeof(tabs[i].title), "New Tab");
    snprintf(tabs[i].status, sizeof(tabs[i].status), "Type a URL, press Enter");
    browser_init_page_colors(&tabs[i], "");
    active = i;
    editing = 1;
    needs_redraw = 1;
    return i;
}

void browser_close_tab(int i) {
    if (ntabs <= 1 || i < 0 || i >= ntabs)
        return;
    browser_tab_teardown_js(&tabs[i]);
    for (int j = i; j < ntabs - 1; j++)
        tabs[j] = tabs[j + 1];
    ntabs--;
    memset(&tabs[ntabs], 0, sizeof(tabs[ntabs]));
    if (active >= ntabs)
        active = ntabs - 1;
    needs_redraw = 1;
}

void browser_input(char c) {
    struct br_tab *t = browser_cur();

    if (c == '\n' || c == '\r') {
        browser_go(t->url);
        editing = 0;
        return;
    }

    if (c == '\t') {
        if (editing) {
            editing = 0;
        } else {
            int next = (active + 1) % ntabs;
            browser_select_tab(next);
        }
        needs_redraw = 1;
        return;
    }

    if (!editing) {
        if (c >= '1' && c <= '0' + BR_MAX_TABS) {
            browser_select_tab(c - '1');
            return;
        }
        if (c == ']' || c == '}') {
            browser_select_tab((active + 1) % ntabs);
            return;
        }
        if (c == '[' || c == '{') {
            browser_select_tab((active + ntabs - 1) % ntabs);
            return;
        }
        if (c == 't' || c == 'T') {
            if (browser_new_tab("https://www.fark.com/") >= 0)
                return;
            snprintf(t->status, sizeof(t->status), "Max %d tabs", BR_MAX_TABS);
            needs_redraw = 1;
            return;
        }
        if (c == 'w' || c == 'W') {
            browser_close_tab(active);
            return;
        }
        if (c == 'j' || c == 'J' || c == 14) {
            if (t->scroll_y + 1 < t->nblocks)
                t->scroll_y++;
            needs_redraw = 1;
            return;
        }
        if (c == 'k' || c == 'K' || c == 16) {
            if (t->scroll_y > 0)
                t->scroll_y--;
            needs_redraw = 1;
            return;
        }
        if (c == 'r' || c == 'R') {
            browser_go(t->url);
            return;
        }
        if (c == 'l' || c == 'L' || c == 'g' || c == 'G') {
            editing = 1;
            needs_redraw = 1;
            return;
        }
        return;
    }

    if (c == '\b' || c == 127) {
        int n = url_len_of(t);
        if (n > 0) {
            t->url[n - 1] = '\0';
            needs_redraw = 1;
        }
        return;
    }
    if (c >= 32 && c < 127) {
        int n = url_len_of(t);
        if (n + 1 < BR_URL_MAX) {
            t->url[n] = c;
            t->url[n + 1] = '\0';
            needs_redraw = 1;
        }
    }
}

void browser_click(int32_t lx, int32_t ly, uint32_t w, uint32_t h) {
    (void)h;
    if (ly < 0 || lx < 0)
        return;

    if ((uint32_t)ly >= hit_tab_y && (uint32_t)ly < hit_tab_y + hit_tab_h) {
        if ((uint32_t)lx >= hit_plus_x && (uint32_t)lx < hit_plus_x + hit_tab_w) {
            browser_new_tab("peak://demo");
            return;
        }
        int idx = (int)((uint32_t)lx / (hit_tab_w ? hit_tab_w : 1));
        if (idx >= 0 && idx < ntabs)
            browser_select_tab(idx);
        return;
    }

    if ((uint32_t)ly >= hit_bar_y && (uint32_t)ly < hit_bar_y + hit_bar_h) {
        if ((uint32_t)lx >= hit_go_x && (uint32_t)lx < hit_go_x + hit_go_w) {
            browser_go(browser_cur()->url);
            editing = 0;
            return;
        }
        editing = 1;
        needs_redraw = 1;
        return;
    }

    struct br_tab *t = browser_cur();
    if (t->use_layout && t->js_ok) {
        uint32_t ch = fb_cell_h();
        uint32_t pad = 6;
        uint32_t chrome_h = hit_tab_h + ch + pad * 2 + 8;
        int32_t py = ly - (int32_t)chrome_h - (int32_t)pad + t->scroll_y;
        int32_t px = lx - (int32_t)pad - 4;
        for (int i = 0; i < t->nboxes; i++) {
            struct css_box *b = &t->boxes[i];
            if (px >= b->x && px < b->x + b->w && py >= b->y && py < b->y + b->h) {
                struct dom_node *n = dom_node(&t->doc, b->node_id);
                if (n && n->type == DOM_ELEMENT) {
                    browser_js_dispatch_click(&t->jsh, b->node_id);
                    browser_rebuild_layout(t, (int)(w > 40 ? w - 24 : 640));
                    needs_redraw = 1;
                }
                break;
            }
        }
    }
    (void)w;
}
