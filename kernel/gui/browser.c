#include "browser.h"
#include "browser_internal.h"
#include "privacy.h"
#include "browser_js.h"
#include "webapi.h"
#include "ctr.h"
#include "net.h"
#include "util.h"
#include "dom.h"
#include "css.h"
#include "js.h"
#include "heap.h"

struct br_tab tabs[BR_MAX_TABS];
int ntabs;
int active;
int editing;
int needs_redraw = 1;
static char *body_cache;

uint32_t hit_tab_y, hit_tab_h, hit_tab_w;
uint32_t hit_plus_x, hit_go_x, hit_go_w, hit_bar_y, hit_bar_h;

static char *ensure_body_cache(void) {
    if (!body_cache)
        body_cache = (char *)kmalloc(BR_BODY_MAX);
    return body_cache;
}

struct br_tab *browser_cur(void) {
    if (active < 0 || active >= ntabs || !tabs[active].used)
        return &tabs[0];
    return &tabs[active];
}

void browser_tab_teardown_js(struct br_tab *t) {
    if (!t)
        return;
    browser_js_invalidate_handles(&t->jsh);
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

void browser_rebuild_layout(struct br_tab *t, int content_w) {
    t->nboxes = css_layout(&t->doc, &t->sheet, t->boxes, BR_MAX_BOXES, content_w);
    t->use_layout = t->nboxes > 0;
}

static void load_document(struct br_tab *t, const char *html) {
    browser_tab_teardown_js(t);
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

    browser_rebuild_layout(t, 640);
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
        browser_tab_teardown_js(&tabs[i]);
    memset(tabs, 0, sizeof(tabs));
    ntabs = 0;
    active = 0;
    editing = 0;
    needs_redraw = 1;

    browser_new_tab("peak://demo");
    editing = 0;
    load_document(&tabs[0], peak_js_demo_html());
    snprintf(tabs[0].title, sizeof(tabs[0].title), "Peak JS Demo");
    snprintf(tabs[0].status, sizeof(tabs[0].status),
             "Local Peak JS — click Count (no network)");

    browser_new_tab("http://127.0.0.1:18080/");
    snprintf(tabs[1].title, sizeof(tabs[1].title), "ctr demo");
    snprintf(tabs[1].status, sizeof(tabs[1].status),
             "Local container — ctr build && ctr run, then Enter");

    browser_new_tab("https://peakevergreen.com/");
    snprintf(tabs[2].title, sizeof(tabs[2].title), "Peak Evergreen");
    snprintf(tabs[2].status, sizeof(tabs[2].status),
             "In-guest TCP+TLS — Enter to fetch");
    active = 0;
    editing = 0;
}

void browser_error_page(struct br_tab *t, enum br_err_kind kind,
                        const char *detail, int http_st) {
    const char *title = "Could not load page";
    const char *hint = "Try again or check network settings.";

    browser_clear_blocks(t);
    browser_tab_teardown_js(t);
    t->tls_secure = 0;
    t->tls_verified = 0;

    switch (kind) {
    case BR_ERR_NETWORK:
        title = "No network connection";
        hint = "e1000 did not initialize. Check QEMU -device e1000.";
        break;
    case BR_ERR_DNS:
        title = "Could not resolve host";
        hint = detail && detail[0]
                   ? "DNS lookup failed for this URL. Check dns in ifconfig."
                   : "DNS lookup failed — verify hostname and DNS server.";
        break;
    case BR_ERR_TLS:
        if (detail && !strcmp(detail, "fetch: tls-expired"))
            title = "Certificate expired";
        else if (detail && !strcmp(detail, "fetch: tls-mismatch"))
            title = "Certificate hostname mismatch";
        else if (detail && !strcmp(detail, "fetch: tls-untrusted"))
            title = "Untrusted certificate";
        else if (detail && !strcmp(detail, "fetch: tls-rng"))
            title = "Secure connection unavailable";
        else if (detail && !strcmp(detail, "fetch: tls-alert"))
            title = "Server rejected the connection";
        else
            title = "Secure connection failed";
        hint = "Settings → Network: trust-on-first-use or forget saved certificates.";
        break;
    case BR_ERR_HTTP:
        title = "Page request failed";
        hint = http_st > 0
                   ? "The server returned an error. Try again later."
                   : "No response from server — check link and hostname.";
        break;
    case BR_ERR_LOCAL:
        title = "Local page not found";
        hint = "Start the in-guest demo container, then reload.";
        break;
    }

    browser_add_block(t, BR_H1, title);
    browser_add_block(t, BR_SPACER, "");
    browser_add_block(t, BR_P, hint);
    if (kind == BR_ERR_LOCAL) {
        browser_add_block(t, BR_LI, "ctr build");
        browser_add_block(t, BR_LI, "ctr run");
        browser_add_block(t, BR_LI, "Press Enter to reload");
    } else if (detail && detail[0] && kind != BR_ERR_TLS) {
        char line[BR_TEXT_MAX];
        snprintf(line, sizeof(line), "%s", detail);
        browser_add_block(t, BR_P, line);
    }

    if (kind == BR_ERR_TLS && detail)
        snprintf(t->status, sizeof(t->status), "%s", detail);
    else if (kind == BR_ERR_DNS)
        snprintf(t->status, sizeof(t->status), "DNS failed");
    else if (kind == BR_ERR_NETWORK)
        snprintf(t->status, sizeof(t->status), "Network down");
    else if (http_st > 0)
        snprintf(t->status, sizeof(t->status), "HTTP %d — fetch failed", http_st);
    else
        snprintf(t->status, sizeof(t->status), "Fetch failed");

    browser_init_page_colors(t, "");
    needs_redraw = 1;
}

void browser_go(const char *url) {
    privacy_grant_net_client(0);
    struct br_tab *t = browser_cur();
    char norm[BR_URL_MAX];
    browser_normalize_url(url, norm, sizeof(norm));
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

    if (browser_is_local_host(t->url)) {
        ctr_init();
        t->tls_secure = 0;
        t->tls_verified = 0;
        ok = (ctr_http_get(t->url, body, BR_BODY_MAX, &st) == 0);
        if (!ok) {
            t->http_status = st;
            browser_error_page(t, BR_ERR_LOCAL, t->url, st);
            return;
        }
    } else {
        if (!net_ready()) {
            browser_error_page(t, BR_ERR_NETWORK, NULL, 0);
            return;
        }
        snprintf(t->status, sizeof(t->status), "DNS + TCP/TLS...");
        ok = (net_http_get(t->url, body, BR_BODY_MAX, &st) == 0);

        if (!ok) {
            t->http_status = st;
            if (net_http_needs_tls()) {
                browser_error_page(t, BR_ERR_TLS, net_http_tls_reject_name(), st);
            } else if (body[0] && strstr(body, "DNS failed")) {
                browser_error_page(t, BR_ERR_DNS, t->url, st);
            } else {
                browser_error_page(t, BR_ERR_HTTP, NULL, st);
            }
            return;
        }
        t->tls_secure = net_http_last_tls_secure();
        t->tls_verified = net_http_last_tls_verified();
        if (t->tls_secure) {
            snprintf(t->status, sizeof(t->status),
                     t->tls_verified ? "HTTPS verified" : "HTTPS (trust limited)");
        }
    }

    t->http_status = st;
    load_document(t, body);
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
            browser_rebuild_layout(&tabs[i], 640);
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
