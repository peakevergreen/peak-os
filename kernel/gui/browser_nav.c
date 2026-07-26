#include "browser.h"
#include "desktop_internal.h"
#include "browser_internal.h"
#include "browser_js.h"
#include "css.h"
#include "dom.h"
#include "fb.h"
#include "util.h"
#include "clipboard.h"
#include "notify.h"
#include "keyboard.h"
#include "desktop_internal.h"

static struct {
    int valid;
    char url[BR_URL_MAX];
    char title[BR_TITLE_MAX];
} last_closed;

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
    last_closed.valid = 1;
    snprintf(last_closed.url, sizeof(last_closed.url), "%s", tabs[i].url);
    if (tabs[i].title[0])
        snprintf(last_closed.title, sizeof(last_closed.title), "%s", tabs[i].title);
    else
        snprintf(last_closed.title, sizeof(last_closed.title), "Tab");
    browser_tab_teardown_js(&tabs[i]);
    for (int j = i; j < ntabs - 1; j++)
        tabs[j] = tabs[j + 1];
    ntabs--;
    memset(&tabs[ntabs], 0, sizeof(tabs[ntabs]));
    if (active >= ntabs)
        active = ntabs - 1;
    editing = 0;
    needs_redraw = 1;
}

int browser_has_closed_tab(void) {
    return last_closed.valid != 0;
}

void browser_closed_clear(void) {
    last_closed.valid = 0;
    last_closed.url[0] = '\0';
    last_closed.title[0] = '\0';
}

void browser_restore_closed_tab(void) {
    struct br_tab *t = browser_cur();
    if (!last_closed.valid) {
        snprintf(t->status, sizeof(t->status), "No closed tab to restore");
        needs_redraw = 1;
        return;
    }
    if (ntabs >= BR_MAX_TABS) {
        snprintf(t->status, sizeof(t->status), "Max %d tabs", BR_MAX_TABS);
        needs_redraw = 1;
        return;
    }
    char url[BR_URL_MAX];
    char title[BR_TITLE_MAX];
    snprintf(url, sizeof(url), "%s", last_closed.url);
    snprintf(title, sizeof(title), "%s", last_closed.title);
    last_closed.valid = 0;
    if (browser_new_tab(url) < 0)
        return;
    snprintf(tabs[active].title, sizeof(tabs[active].title), "%s", title);
    editing = 0;
    browser_go(url);
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
            if (keyboard_shift_down()) {
                browser_restore_closed_tab();
                return;
            }
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
        if (c == 'b' || c == 'B') {
            browser_back();
            return;
        }
        if (c == 'f' || c == 'F') {
            browser_forward();
            return;
        }
        if (c == 'c' || c == 'C') {
            t->show_console = !t->show_console;
            needs_redraw = 1;
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
        if (ntabs < BR_MAX_TABS &&
            (uint32_t)lx >= hit_plus_x && (uint32_t)lx < hit_plus_x + hit_tab_w) {
            browser_new_tab("peak://demo");
            return;
        }
        for (int i = 0; i < ntabs; i++) {
            if (hit_tab_close_w[i] > 0 &&
                (uint32_t)lx >= hit_tab_close_x[i] &&
                (uint32_t)lx < hit_tab_close_x[i] + hit_tab_close_w[i]) {
                browser_close_tab(i);
                return;
            }
        }
        int idx = (int)((uint32_t)lx / (hit_tab_w ? hit_tab_w : 1));
        if (idx >= 0 && idx < ntabs)
            browser_select_tab(idx);
        return;
    }

    if ((uint32_t)ly >= hit_bar_y && (uint32_t)ly < hit_bar_y + hit_bar_h) {
        if (hit_back_w > 0 && (uint32_t)lx >= hit_back_x &&
            (uint32_t)lx < hit_back_x + hit_back_w) {
            browser_back();
            return;
        }
        if (hit_fwd_w > 0 && (uint32_t)lx >= hit_fwd_x &&
            (uint32_t)lx < hit_fwd_x + hit_fwd_w) {
            browser_forward();
            return;
        }
        if ((uint32_t)lx >= hit_go_x && (uint32_t)lx < hit_go_x + hit_go_w) {
            browser_go(browser_cur()->url);
            editing = 0;
            return;
        }
        editing = 1;
        needs_redraw = 1;
        return;
    }

    if (hit_bm_h > 0 && (uint32_t)ly >= hit_bm_y && (uint32_t)ly < hit_bm_y + hit_bm_h) {
        for (int i = 0; i < 4; i++) {
            if (hit_bm_w[i] > 0 && (uint32_t)lx >= hit_bm_x[i] &&
                (uint32_t)lx < hit_bm_x[i] + hit_bm_w[i]) {
                browser_bookmark_go(i);
                return;
            }
        }
    }

    struct br_tab *t = browser_cur();
    if (t->show_retry && hit_retry_w > 0 &&
        (uint32_t)lx >= hit_retry_x && (uint32_t)lx < hit_retry_x + hit_retry_w &&
        (uint32_t)ly >= hit_retry_y && (uint32_t)ly < hit_retry_y + hit_retry_h) {
        browser_reload();
        return;
    }
    if (t->use_layout && t->js_ok) {
        uint32_t ch = fb_cell_h();
        uint32_t pad = 6;
        uint32_t chrome_h = hit_tab_h + ch + pad * 2 + 8;
        if (hit_bm_h > 0)
            chrome_h += hit_bm_h + 4;
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

void browser_back(void) {
    struct br_tab *t = browser_cur();
    if (t->prev_url[0]) {
        char back[BR_URL_MAX];
        snprintf(back, sizeof(back), "%s", t->prev_url);
        snprintf(t->forward_url, sizeof(t->forward_url), "%s", t->url);
        t->prev_url[0] = '\0';
        browser_go(back);
    } else {
        snprintf(t->status, sizeof(t->status), "No previous page");
        needs_redraw = 1;
    }
}

void browser_forward(void) {
    struct br_tab *t = browser_cur();
    if (t->forward_url[0]) {
        char fwd[BR_URL_MAX];
        snprintf(fwd, sizeof(fwd), "%s", t->forward_url);
        snprintf(t->prev_url, sizeof(t->prev_url), "%s", t->url);
        t->forward_url[0] = '\0';
        browser_go(fwd);
    } else {
        snprintf(t->status, sizeof(t->status), "No forward page");
        needs_redraw = 1;
    }
}

void browser_reload(void) {
    struct br_tab *t = browser_cur();
    t->show_retry = 0;
    browser_go(t->url);
}

int browser_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 8)
        return 0;
    struct br_tab *t = browser_cur();
    items[0].label = "Back";
    items[0].enabled = t->prev_url[0] != '\0';
    items[0].separator = 0;
    items[0].action_id = CTX_ACT_BROWSER_BACK;
    items[1].label = "Forward";
    items[1].enabled = t->forward_url[0] != '\0';
    items[1].separator = 0;
    items[1].action_id = CTX_ACT_BROWSER_FORWARD;
    items[2].label = "Reload";
    items[2].enabled = 1;
    items[2].separator = 0;
    items[2].action_id = CTX_ACT_BROWSER_RELOAD;
    items[3].label = "Add bookmark";
    items[3].enabled = t->url[0] != '\0';
    items[3].separator = 0;
    items[3].action_id = CTX_ACT_BROWSER_BOOKMARK;
    items[4].label = "Save to Downloads";
    items[4].enabled = t->url[0] != '\0' && t->last_body_len > 0;
    items[4].separator = 0;
    items[4].action_id = CTX_ACT_BROWSER_DOWNLOAD;
    items[5].label = "Copy URL";
    items[5].enabled = t->url[0] != '\0';
    items[5].separator = 0;
    items[5].action_id = CTX_ACT_BROWSER_COPY_URL;
    items[6].label = "New tab";
    items[6].enabled = ntabs < BR_MAX_TABS;
    items[6].separator = 0;
    items[6].action_id = CTX_ACT_BROWSER_NEW_TAB;
    items[7].label = "Reopen closed tab";
    items[7].enabled = browser_has_closed_tab() && ntabs < BR_MAX_TABS;
    items[7].separator = 0;
    items[7].action_id = CTX_ACT_BROWSER_RESTORE;
    int n = 8;
    int bm = browser_bookmark_count();
    if (bm > 0 && n + 1 < max_items) {
        items[n].label = NULL;
        items[n].enabled = 0;
        items[n].separator = 1;
        items[n].action_id = CTX_ACT_NONE;
        n++;
    }
    for (int i = 0; i < bm && i < 4 && n < max_items - 4; i++) {
        const char *title = browser_bookmark_title(i);
        items[n].label = title ? title : "Bookmark";
        items[n].enabled = 1;
        items[n].separator = 0;
        items[n].action_id = CTX_ACT_BROWSER_BM_BASE + i;
        n++;
        if (n >= max_items - 2)
            break;
        items[n].label = "Remove bookmark";
        items[n].enabled = 1;
        items[n].separator = 0;
        items[n].action_id = CTX_ACT_BROWSER_BM_RM_BASE + i;
    }
    if (n < max_items) {
        items[n].label = NULL;
        items[n].enabled = 0;
        items[n].separator = 1;
        items[n].action_id = CTX_ACT_NONE;
        n++;
        items[n].label = "Close window";
        items[n].enabled = 1;
        items[n].separator = 0;
        items[n].action_id = CTX_ACT_CLOSE;
        n++;
    }
    return n;
}

int browser_ctx_action(int action_id) {
    struct br_tab *t = browser_cur();
    switch (action_id) {
    case CTX_ACT_BROWSER_BACK:
        browser_back();
        return 1;
    case CTX_ACT_BROWSER_FORWARD:
        browser_forward();
        return 1;
    case CTX_ACT_BROWSER_RELOAD:
        browser_reload();
        return 1;
    case CTX_ACT_BROWSER_DOWNLOAD: {
        size_t bl = 0;
        const char *body = browser_page_body(&bl);
        if (body && bl) {
            if (browser_download_save(t->url, body, bl, t->status, sizeof(t->status)) == 0)
                notify_push_clipboard("Saved to Downloads");
        } else
            snprintf(t->status, sizeof(t->status), "Nothing to save");
        needs_redraw = 1;
        return 1;
    }
    case CTX_ACT_BROWSER_BOOKMARK:
        if (browser_bookmark_add(t->url, t->title[0] ? t->title : NULL) == 0) {
            snprintf(t->status, sizeof(t->status), "Bookmark saved");
            needs_redraw = 1;
        } else {
            snprintf(t->status, sizeof(t->status), "Bookmark list full");
            needs_redraw = 1;
        }
        return 1;
    case CTX_ACT_BROWSER_COPY_URL:
        if (t->url[0]) {
            clipboard_set(t->url, strlen(t->url));
            notify_push_clipboard("URL");
        }
        return 1;
    case CTX_ACT_BROWSER_NEW_TAB:
        if (browser_new_tab("peak://demo") >= 0)
            return 1;
        snprintf(t->status, sizeof(t->status), "Max %d tabs", BR_MAX_TABS);
        needs_redraw = 1;
        return 1;
    case CTX_ACT_BROWSER_RESTORE:
        browser_restore_closed_tab();
        return 1;
    default:
        if (action_id >= CTX_ACT_BROWSER_BM_RM_BASE &&
            action_id < CTX_ACT_BROWSER_BM_RM_BASE + 16) {
            if (browser_bookmark_remove(action_id - CTX_ACT_BROWSER_BM_RM_BASE) == 0)
                snprintf(t->status, sizeof(t->status), "Bookmark removed");
            needs_redraw = 1;
            return 1;
        }
        if (action_id >= CTX_ACT_BROWSER_BM_BASE &&
            action_id < CTX_ACT_BROWSER_BM_BASE + 16) {
            browser_bookmark_go(action_id - CTX_ACT_BROWSER_BM_BASE);
            return 1;
        }
        return 0;
    }
}
