#include "browser.h"
#include "browser_internal.h"
#include "privacy.h"
#include "browser_js.h"
#include "webapi.h"
#include "ctr.h"
#include "net.h"
#include "fb.h"
#include "theme.h"
#include "util.h"
#include "dom.h"
#include "css.h"
#include "js.h"
#include "heap.h"

static struct br_tab tabs[BR_MAX_TABS];
static int ntabs;
static int active;
static int editing;
static int needs_redraw = 1;
static char *body_cache;

static uint32_t hit_tab_y, hit_tab_h, hit_tab_w;
static uint32_t hit_plus_x, hit_go_x, hit_go_w, hit_bar_y, hit_bar_h;

static char *ensure_body_cache(void) {
    if (!body_cache)
        body_cache = (char *)kmalloc(BR_BODY_MAX);
    return body_cache;
}

static struct br_tab *cur(void) {
    if (active < 0 || active >= ntabs || !tabs[active].used)
        return &tabs[0];
    return &tabs[active];
}

static int is_local_host(const char *url) {
    const char *p = url;
    if (!strncmp(p, "http://", 7))
        p += 7;
    else if (!strncmp(p, "https://", 8))
        p += 8;
    if (!strncmp(p, "127.0.0.1", 9) || !strncmp(p, "localhost", 9))
        return 1;
    return 0;
}

static void normalize_url(const char *in, char *out, size_t out_cap) {
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

static int url_len_of(struct br_tab *t) {
    return (int)strlen(t->url);
}

static void select_tab(int i) {
    if (i < 0 || i >= ntabs || !tabs[i].used)
        return;
    active = i;
    editing = 0;
    needs_redraw = 1;
}

static void tab_teardown_js(struct br_tab *t) {
    if (!t)
        return;
    if (t->js) {
        js_rt_destroy(t->js);
        t->js = NULL;
    }
    dom_doc_clear(&t->doc);
    css_sheet_init(&t->sheet);
    t->nboxes = 0;
    t->js_ok = 0;
    t->use_layout = 0;
    memset(&t->jsh, 0, sizeof(t->jsh));
}

static int new_tab(const char *url) {
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

static void close_tab(int i) {
    if (ntabs <= 1 || i < 0 || i >= ntabs)
        return;
    tab_teardown_js(&tabs[i]);
    for (int j = i; j < ntabs - 1; j++)
        tabs[j] = tabs[j + 1];
    ntabs--;
    memset(&tabs[ntabs], 0, sizeof(tabs[ntabs]));
    if (active >= ntabs)
        active = ntabs - 1;
    needs_redraw = 1;
}

static const char *peak_js_demo_html(void) {
    return "<!doctype html><html><head><title>Peak JS Demo</title>"
           "<style>"
           "h1{color:#3DA36A} button{color:#9AC4AE} #out{color:#E8F0EC}"
           "</style></head><body>"
           "<h1 id='title'>Peak Browser JavaScript</h1>"
           "<p id='msg'>Click the button — scripts run in-guest.</p>"
           "<button id='btn'>Count</button>"
           "<p id='out'>count: 0</p>"
           "<script>"
           "var n=0;"
           "var btn=document.getElementById('btn');"
           "var out=document.getElementById('out');"
           "on(btn,'click',function(){"
           "  n=n+1;"
           "  textContent(out,'count: '+n);"
           "  console.log('click',n);"
           "});"
           "setTimeout(function(){"
           "  textContent(document.getElementById('msg'),'Timers work. Try the button.');"
           "},400);"
           "</script></body></html>";
}

static void extract_styles(struct br_tab *t, const char *html) {
    css_sheet_init(&t->sheet);
    const char *p = html;
    while (*p) {
        if ((p[0] == '<' && (p[1] == 's' || p[1] == 'S') &&
             (p[2] == 't' || p[2] == 'T') && (p[3] == 'y' || p[3] == 'Y'))) {
            while (*p && *p != '>')
                p++;
            if (*p == '>')
                p++;
            const char *start = p;
            while (*p && !(p[0] == '<' && p[1] == '/'))
                p++;
            size_t len = (size_t)(p - start);
            if (len > 0 && len < 4096) {
                char *buf = kmalloc(len + 1);
                if (buf) {
                    memcpy(buf, start, len);
                    buf[len] = '\0';
                    css_parse_stylesheet(&t->sheet, buf);
                    kfree(buf);
                }
            }
            continue;
        }
        p++;
    }
}

static void rebuild_layout(struct br_tab *t, int content_w) {
    t->nboxes = css_layout(&t->doc, &t->sheet, t->boxes, BR_MAX_BOXES, content_w);
    t->use_layout = t->nboxes > 0;
}

static void load_document(struct br_tab *t, const char *html) {
    tab_teardown_js(t);
    browser_extract_title(t, html, active);
    browser_init_page_colors(t, html);
    extract_styles(t, html);

    if (dom_parse_html(&t->doc, html, t->url) != 0) {
        browser_reader_fallback(t, html, active);
        snprintf(t->status, sizeof(t->status), "DOM parse failed — reader mode");
        return;
    }
    if (t->doc.title[0])
        snprintf(t->title, sizeof(t->title), "%s", t->doc.title);

    t->js = js_rt_create();
    if (!t->js) {
        browser_reader_fallback(t, html, active);
        snprintf(t->status, sizeof(t->status), "JS runtime OOM — reader mode");
        return;
    }
    js_rt_set_budgets(t->js, JS_INS_BUDGET_DEFAULT, JS_HEAP_OBJS_DEFAULT);
    t->dom_dirty = 0;
    browser_js_host_init(&t->jsh, t->js, &t->doc, &t->dom_dirty);
    browser_js_install_dom(&t->jsh);
    webapi_set_tab(active, 0);
    webapi_install(t->js, t->url);
    int script_rc = browser_js_run_scripts(&t->jsh);
    if (webapi_load_classic_scripts(t->js, &t->doc, t->url) != 0)
        script_rc = -1;
    t->js_ok = (script_rc == 0);

    rebuild_layout(t, 640);
    if (t->use_layout && !t->js_ok && t->nboxes < 8)
        t->use_layout = 0;
    if (!t->use_layout) {
        browser_parse_html(t, html, active);
        if (t->js_ok)
            snprintf(t->status, sizeof(t->status), "HTTP %d | JS ok | %d blocks",
                     t->http_status, browser_content_blocks(t));
        else
            snprintf(t->status, sizeof(t->status), "HTTP %d | JS budget/err | reader",
                     t->http_status);
    } else {
        browser_clear_blocks(t);
        snprintf(t->status, sizeof(t->status), "HTTP %d | JS %s | %d boxes | Peak DOM",
                 t->http_status, t->js_ok ? "ok" : "err", t->nboxes);
    }
    needs_redraw = 1;
}

void browser_reset(void) {
    for (int i = 0; i < ntabs; i++)
        tab_teardown_js(&tabs[i]);
    memset(tabs, 0, sizeof(tabs));
    ntabs = 0;
    active = 0;
    editing = 0;
    needs_redraw = 1;

    new_tab("peak://demo");
    editing = 0;
    load_document(&tabs[0], peak_js_demo_html());
    snprintf(tabs[0].title, sizeof(tabs[0].title), "Peak JS Demo");
    snprintf(tabs[0].status, sizeof(tabs[0].status),
             "Local Peak JS — click Count (no network)");

    new_tab("http://127.0.0.1:18080/");
    snprintf(tabs[1].title, sizeof(tabs[1].title), "ctr demo");
    snprintf(tabs[1].status, sizeof(tabs[1].status),
             "Local container — ctr build && ctr run, then Enter");

    new_tab("https://peakevergreen.com/");
    snprintf(tabs[2].title, sizeof(tabs[2].title), "Peak Evergreen");
    snprintf(tabs[2].status, sizeof(tabs[2].status),
             "In-guest TCP+TLS — Enter to fetch");
    active = 0;
    editing = 0;
}

void browser_go(const char *url) {
    privacy_grant_net_client(0);
    struct br_tab *t = cur();
    char norm[BR_URL_MAX];
    normalize_url(url, norm, sizeof(norm));
    snprintf(t->url, sizeof(t->url), "%s", norm);
    t->scroll_y = 0;

    if (!strcmp(t->url, "peak://demo") || !strcmp(t->url, "about:js") ||
        !strcmp(t->url, "peak:demo")) {
        t->http_status = 200;
        load_document(t, peak_js_demo_html());
        snprintf(t->title, sizeof(t->title), "Peak JS Demo");
        return;
    }

    snprintf(t->status, sizeof(t->status), "Fetching...");
    needs_redraw = 1;

    char *body = ensure_body_cache();
    if (!body) {
        snprintf(t->status, sizeof(t->status), "Out of memory");
        needs_redraw = 1;
        return;
    }

    int st = 0;
    body[0] = '\0';
    int ok;

    if (is_local_host(t->url)) {
        ctr_init();
        ok = (ctr_http_get(t->url, body, BR_BODY_MAX, &st) == 0);
        if (!ok) {
            t->http_status = st;
            snprintf(t->status, sizeof(t->status), "Local fetch failed (HTTP %d)", st);
            browser_clear_blocks(t);
            tab_teardown_js(t);
            browser_add_block(t, BR_H1, "Local page not found");
            browser_add_block(t, BR_SPACER, "");
            browser_add_block(t, BR_P, "Start the in-guest demo container:");
            browser_add_block(t, BR_LI, "ctr build");
            browser_add_block(t, BR_LI, "ctr run");
            browser_add_block(t, BR_LI, "Reload this tab (Enter)");
            browser_init_page_colors(t, "");
            needs_redraw = 1;
            return;
        }
    } else {
        if (!net_ready()) {
            snprintf(t->status, sizeof(t->status), "Network down");
            browser_clear_blocks(t);
            tab_teardown_js(t);
            browser_add_block(t, BR_H1, "No network");
            browser_add_block(t, BR_P, "e1000 did not initialize. Check QEMU -device e1000.");
            browser_init_page_colors(t, "");
            needs_redraw = 1;
            return;
        }
        snprintf(t->status, sizeof(t->status), "DNS + TCP/TLS...");
        ok = (net_http_get(t->url, body, BR_BODY_MAX, &st) == 0);

        if (!ok) {
            t->http_status = st;
            snprintf(t->status, sizeof(t->status), "Fetch failed (HTTP %d)", st);
            if (body[0]) {
                load_document(t, body);
            } else {
                browser_clear_blocks(t);
                tab_teardown_js(t);
                browser_add_block(t, BR_H1, "Could not load page");
                browser_add_block(t, BR_P, "Check: ifconfig / ping / wget");
                browser_init_page_colors(t, "");
            }
            needs_redraw = 1;
            return;
        }
    }

    t->http_status = st;
    load_document(t, body);
}

void browser_input(char c) {
    struct br_tab *t = cur();

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
            select_tab(next);
        }
        needs_redraw = 1;
        return;
    }

    if (!editing) {
        if (c >= '1' && c <= '0' + BR_MAX_TABS) {
            select_tab(c - '1');
            return;
        }
        if (c == ']' || c == '}') {
            select_tab((active + 1) % ntabs);
            return;
        }
        if (c == '[' || c == '{') {
            select_tab((active + ntabs - 1) % ntabs);
            return;
        }
        if (c == 't' || c == 'T') {
            if (new_tab("https://www.fark.com/") >= 0)
                return;
            snprintf(t->status, sizeof(t->status), "Max %d tabs", BR_MAX_TABS);
            needs_redraw = 1;
            return;
        }
        if (c == 'w' || c == 'W') {
            close_tab(active);
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
            new_tab("peak://demo");
            return;
        }
        int idx = (int)((uint32_t)lx / (hit_tab_w ? hit_tab_w : 1));
        if (idx >= 0 && idx < ntabs)
            select_tab(idx);
        return;
    }

    if ((uint32_t)ly >= hit_bar_y && (uint32_t)ly < hit_bar_y + hit_bar_h) {
        if ((uint32_t)lx >= hit_go_x && (uint32_t)lx < hit_go_x + hit_go_w) {
            browser_go(cur()->url);
            editing = 0;
            return;
        }
        editing = 1;
        needs_redraw = 1;
        return;
    }

    struct br_tab *t = cur();
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
                    rebuild_layout(t, (int)(w > 40 ? w - 24 : 640));
                    needs_redraw = 1;
                }
                break;
            }
        }
    }
    (void)w;
}

int browser_wants_redraw(void) {
    return needs_redraw;
}

void browser_tick(void) {
    for (int i = 0; i < ntabs; i++) {
        if (!tabs[i].used || !tabs[i].js)
            continue;
        js_tick(tabs[i].js);
        if (tabs[i].dom_dirty) {
            tabs[i].dom_dirty = 0;
            rebuild_layout(&tabs[i], 640);
            needs_redraw = 1;
        }
        if (js_pending_work(tabs[i].js))
            needs_redraw = 1;
    }
}

void browser_js_metrics(uint32_t *tabs_with_js, uint32_t *objs, uint32_t *timers,
                        uint32_t *gc_runs) {
    uint32_t tj = 0, o = 0, t = 0, g = 0;
    for (int i = 0; i < ntabs; i++) {
        if (!tabs[i].used || !tabs[i].js)
            continue;
        tj++;
        uint32_t oo = 0, ii = 0, tt = 0, gg = 0;
        js_rt_stats(tabs[i].js, &oo, &ii, &tt, &gg);
        o += oo;
        t += tt;
        g += gg;
        (void)ii;
    }
    if (tabs_with_js)
        *tabs_with_js = tj;
    if (objs)
        *objs = o;
    if (timers)
        *timers = t;
    if (gc_runs)
        *gc_runs = g;
}

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

void browser_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const struct peak_theme *th = theme_get();
    struct br_tab *t = cur();
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
    char show[BR_URL_MAX + 2];
    snprintf(show, sizeof(show), "%s%s", t->url, editing ? "_" : "");
    uint32_t max_chars = aw / fb_cell_w();
    if (max_chars > 3 && strlen(show) > max_chars) {
        show[max_chars - 1] = '\0';
        show[max_chars - 2] = '.';
        show[max_chars - 3] = '.';
    }
    fb_draw_string(ax + 4, bar_y + 2, show, th->fg, editing ? th->title : th->surface);

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
