#include "browser.h"
#include "browser_isolation.h"
#include "browser_internal.h"
#include "privacy.h"
#include "browser_js.h"
#include "webapi.h"
#include "ctr.h"
#include "net.h"
#include "tls.h"
#include "util.h"
#include "dom.h"
#include "css.h"
#include "js.h"
#include "heap.h"
#include "timer.h"
#include "http_util.h"
#include "img_decode.h"

struct br_tab tabs[BR_MAX_TABS];
int ntabs;
int active;
int editing;
int needs_redraw = 1;
static char *body_cache;
int browser_hist_navigating; /* skip hist_push during back/forward */

uint32_t hit_tab_y, hit_tab_h, hit_tab_w;
uint32_t hit_tab_close_x[BR_MAX_TABS], hit_tab_close_w[BR_MAX_TABS];
uint32_t hit_plus_x, hit_go_x, hit_go_w, hit_bar_y, hit_bar_h;
uint32_t hit_retry_x, hit_retry_y, hit_retry_w, hit_retry_h;
uint32_t hit_accept_x, hit_accept_y, hit_accept_w, hit_accept_h;
uint32_t hit_forget_x, hit_forget_y, hit_forget_w, hit_forget_h;
uint32_t hit_back_x, hit_back_w, hit_fwd_x, hit_fwd_w;
uint32_t hit_bm_y, hit_bm_h, hit_bm_x[4], hit_bm_w[4];

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

static void browser_free_images(struct br_tab *t) {
    if (!t)
        return;
    for (int i = 0; i < BR_MAX_IMAGES; i++) {
        if (t->images[i].rgb)
            kfree(t->images[i].rgb);
        memset(&t->images[i], 0, sizeof(t->images[i]));
    }
    t->nimages = 0;
}

void browser_tab_teardown_js(struct br_tab *t) {
    if (!t)
        return;
    browser_js_invalidate_handles(&t->jsh);
    if (t->js) {
        js_rt_set_host_mark(t->js, NULL, NULL);
        js_rt_destroy(t->js);
        t->js = NULL;
    }
    browser_free_images(t);
    dom_doc_clear(&t->doc);
    css_sheet_init(&t->sheet);
    t->nboxes = 0;
    t->js_ok = 0;
    t->use_layout = 0;
    t->focus_node = -1;
    t->focus_kind = 0;
    t->focus_value[0] = '\0';
    t->reader_reason[0] = '\0';
    memset(&t->jsh, 0, sizeof(t->jsh));
}

static const char *peak_js_demo_html(void) {
    return "<!doctype html><html><head><title>Peak JS Demo</title>"
           "<style>"
           "h1{color:#3DA36A} button{color:#9AC4AE} #out{color:#E8F0EC}"
           "input{color:#E8F0EC} .box{background:#1a2a22;padding:8px}"
           "</style></head><body>"
           "<h1 id='title'>Peak Browser JavaScript</h1>"
           "<p id='msg'>Click the button — scripts run in-guest.</p>"
           "<button id='btn'>Count</button>"
           "<p id='out'>count: 0</p>"
           "<div class='box'><form action='peak://demo' method='get'>"
           "<label>Name </label>"
           "<input id='name' name='q' type='text' placeholder='type here' value=''/>"
           "<input type='submit' value='Go'/>"
           "</form></div>"
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

static void hist_push(struct br_tab *t, const char *from_url) {
    if (!t || !from_url || !from_url[0])
        return;
    if (t->nhist_back >= BR_HIST_MAX) {
        memmove(t->hist_back[0], t->hist_back[1],
                (size_t)(BR_HIST_MAX - 1) * BR_URL_MAX);
        t->nhist_back = BR_HIST_MAX - 1;
    }
    snprintf(t->hist_back[t->nhist_back++], BR_URL_MAX, "%s", from_url);
    t->nhist_fwd = 0;
    t->forward_url[0] = '\0';
    snprintf(t->prev_url, sizeof(t->prev_url), "%s",
             t->nhist_back ? t->hist_back[t->nhist_back - 1] : "");
}

static int tag_ci_is(const char *p, const char *name) {
    size_t n = strlen(name);
    for (size_t i = 0; i < n; i++) {
        char a = p[i], b = name[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (a != b)
            return 0;
    }
    char c = p[n];
    return c == '>' || c == ' ' || c == '/' || c == '\t' || c == '\n' || c == '\r';
}

static void extract_inline_styles(struct br_tab *t, const char *html) {
    const char *p = html;
    while (*p) {
        if (p[0] == '<' && tag_ci_is(p + 1, "style")) {
            while (*p && *p != '>')
                p++;
            if (*p == '>')
                p++;
            const char *start = p;
            while (*p && !(p[0] == '<' && p[1] == '/'))
                p++;
            size_t len = (size_t)(p - start);
            if (len > 0 && len < 8192) {
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

static void fetch_external_stylesheets(struct br_tab *t, const char *html) {
    int fetched = 0;
    const char *p = html;
    char *cssbuf = kmalloc(BR_STYLESHEET_MAX);
    if (!cssbuf)
        return;
    while (*p && fetched < BR_MAX_STYLESHEETS) {
        if (p[0] == '<' && tag_ci_is(p + 1, "link")) {
            const char *tag = p;
            while (*p && *p != '>')
                p++;
            size_t tlen = (size_t)(p - tag);
            char tmp[512];
            if (tlen >= sizeof(tmp))
                tlen = sizeof(tmp) - 1;
            memcpy(tmp, tag, tlen);
            tmp[tlen] = '\0';
            for (char *c = tmp; *c; c++)
                if (*c >= 'A' && *c <= 'Z')
                    *c = (char)(*c - 'A' + 'a');
            if (!strstr(tmp, "stylesheet"))
                continue;
            const char *href = strstr(tmp, "href=");
            if (!href)
                continue;
            href += 5;
            char quote = 0;
            if (*href == '"' || *href == '\'')
                quote = *href++;
            char rel[BR_URL_MAX];
            size_t ri = 0;
            while (*href && *href != quote && *href != ' ' && *href != '>' &&
                   ri + 1 < sizeof(rel))
                rel[ri++] = *href++;
            rel[ri] = '\0';
            if (!rel[0])
                continue;
            char abs[BR_URL_MAX];
            if (http_resolve_url(t->url, rel, abs, sizeof(abs)) != 0)
                snprintf(abs, sizeof(abs), "%s", rel);
            int st = 0;
            cssbuf[0] = '\0';
            if (net_ready() && net_http_get(abs, cssbuf, BR_STYLESHEET_MAX, &st) == 0 &&
                st >= 200 && st < 300) {
                css_parse_stylesheet(&t->sheet, cssbuf);
                fetched++;
            }
            continue;
        }
        p++;
    }
    kfree(cssbuf);
}

static void load_page_images(struct br_tab *t) {
    browser_free_images(t);
    if (!t || t->doc.body < 0)
        return;
    char *buf = kmalloc(BR_IMG_BYTES_MAX);
    if (!buf)
        return;
    for (int id = 0; id < t->doc.nnodes && t->nimages < BR_MAX_IMAGES; id++) {
        struct dom_node *n = dom_node(&t->doc, id);
        if (!n || n->type != DOM_ELEMENT || strcmp(n->tag, "img") != 0)
            continue;
        const char *src = dom_get_attr(&t->doc, id, "src");
        if (!src || !src[0])
            continue;
        char abs[BR_URL_MAX];
        if (http_resolve_url(t->url, src, abs, sizeof(abs)) != 0)
            snprintf(abs, sizeof(abs), "%s", src);
        int st = 0;
        buf[0] = '\0';
        size_t nread = 0;
        if (!net_ready() || net_http_get(abs, buf, BR_IMG_BYTES_MAX, &st) != 0 ||
            st < 200 || st >= 300)
            continue;
        nread = net_http_last_body_stored();
        if (!nread)
            nread = strlen(buf);
        if (!nread || nread > BR_IMG_BYTES_MAX)
            continue;
        struct img_decoded img;
        memset(&img, 0, sizeof(img));
        if (img_decode_mem((const uint8_t *)buf, nread, &img) != 0)
            continue;
        struct br_img *bi = &t->images[t->nimages++];
        memset(bi, 0, sizeof(*bi));
        bi->used = 1;
        bi->node_id = id;
        bi->pw = img.w;
        bi->ph = img.h;
        bi->rgb = img.rgb;
        img.rgb = NULL;
        const char *alt = dom_get_attr(&t->doc, id, "alt");
        if (alt)
            snprintf(bi->alt, sizeof(bi->alt), "%s", alt);
        img_decode_free(&img);
    }
    kfree(buf);
}

void browser_rebuild_layout(struct br_tab *t, int content_w) {
    t->nboxes = css_layout(&t->doc, &t->sheet, t->boxes, BR_MAX_BOXES, content_w);
    t->use_layout = t->nboxes > 0;
    /* Sync image box geometry from layout. */
    for (int i = 0; i < t->nimages; i++) {
        t->images[i].x = t->images[i].y = 0;
        t->images[i].w = (int)t->images[i].pw;
        t->images[i].h = (int)t->images[i].ph;
        if (t->images[i].w > content_w)
            t->images[i].w = content_w;
        for (int b = 0; b < t->nboxes; b++) {
            if (t->boxes[b].node_id == t->images[i].node_id && t->boxes[b].kind == 1) {
                t->images[i].x = t->boxes[b].x;
                t->images[i].y = t->boxes[b].y;
                if (t->images[i].pw > 0) {
                    t->boxes[b].w = t->images[i].w;
                    t->boxes[b].h = t->images[i].h > 0 ? t->images[i].h : 48;
                    t->images[i].h = t->boxes[b].h;
                }
                break;
            }
        }
    }
    if (t->use_layout)
        snprintf(t->layout_mode, sizeof(t->layout_mode), "css");
}

static void load_document(struct br_tab *t, const char *html) {
    browser_tab_teardown_js(t);
    browser_extract_title(t, html, active);
    browser_init_page_colors(t, html);
    css_sheet_init(&t->sheet);
    extract_inline_styles(t, html);
    if (html && html[0] && strncmp(t->url, "peak:", 5) != 0)
        fetch_external_stylesheets(t, html);

    if (!html || !html[0]) {
        snprintf(t->reader_reason, sizeof(t->reader_reason), "empty body");
        browser_reader_fallback(t, html ? html : "", active);
        snprintf(t->status, sizeof(t->status), "Empty response — reader");
        snprintf(t->layout_mode, sizeof(t->layout_mode), "reader");
        return;
    }

    if (dom_parse_html(&t->doc, html, t->url) != 0) {
        snprintf(t->reader_reason, sizeof(t->reader_reason), "DOM parse failed");
        browser_reader_fallback(t, html, active);
        snprintf(t->status, sizeof(t->status), "DOM parse failed — reader mode");
        snprintf(t->layout_mode, sizeof(t->layout_mode), "reader");
        return;
    }
    if (t->doc.title[0])
        snprintf(t->title, sizeof(t->title), "%s", t->doc.title);

    load_page_images(t);

    t->js = js_rt_create();
    int script_failed = 0;
    if (!t->js) {
        snprintf(t->reader_reason, sizeof(t->reader_reason), "JS runtime OOM");
        script_failed = 1;
    } else {
        js_rt_set_budgets(t->js, JS_INS_BUDGET_DEFAULT, JS_HEAP_OBJS_DEFAULT);
        t->dom_dirty = 0;
        browser_js_host_init(&t->jsh, t->js, &t->doc, &t->dom_dirty);
        browser_js_install_dom(&t->jsh);
        webapi_set_tab(active, 0);
        webapi_install(t->js, t->url);
        /* Partial script failure must not force reader if layout has content. */
        if (browser_js_run_scripts(&t->jsh) != 0)
            script_failed = 1;
        if (webapi_load_classic_scripts(t->js, &t->doc, t->url) != 0)
            script_failed = 1;
    }
    t->js_ok = !script_failed;

    browser_rebuild_layout(t, 640);
    /* Prefer CSS layout whenever we have enough boxes; reader only on hard fail. */
    if (t->nboxes >= 2)
        t->use_layout = 1;
    if (!t->use_layout) {
        if (!t->reader_reason[0])
            snprintf(t->reader_reason, sizeof(t->reader_reason),
                     script_failed ? "scripts failed; sparse layout" : "sparse layout");
        browser_parse_html(t, html, active);
        snprintf(t->layout_mode, sizeof(t->layout_mode), "reader");
        snprintf(t->status, sizeof(t->status),
                 "HTTP %d | %s | %s | %zuB%s", t->http_status,
                 t->http2 ? "H2" : "H1", t->js_ok ? "JS ok" : "JS err",
                 t->last_body_len, t->body_truncated ? " trunc" : "");
    } else {
        browser_clear_blocks(t);
        snprintf(t->layout_mode, sizeof(t->layout_mode), "css");
        snprintf(t->status, sizeof(t->status),
                 "HTTP %d | %s | JS %s | %d boxes | %zuB%s", t->http_status,
                 t->http2 ? "H2" : "H1", t->js_ok ? "ok" : "err", t->nboxes,
                 t->last_body_len, t->body_truncated ? " trunc" : "");
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
    memset(hit_tab_close_x, 0, sizeof(hit_tab_close_x));
    memset(hit_tab_close_w, 0, sizeof(hit_tab_close_w));
    browser_closed_clear();

    browser_bookmarks_init();

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
    t->fetching = 0;
    t->show_retry = 1;
    t->show_tls_accept = 0;

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
        else if (detail && !strcmp(detail, "fetch: tls-untrusted")) {
            title = "Untrusted certificate";
            t->show_tls_accept = 1;
        } else if (detail && !strcmp(detail, "fetch: tls-rng"))
            title = "Secure connection unavailable";
        else if (detail && !strcmp(detail, "fetch: tls-alert"))
            title = "Server rejected the connection";
        else
            title = "Secure connection failed";
        hint = t->show_tls_accept
                   ? "Accept once to trust this host, or Forget saved trust. Retry reloads."
                   : "Settings → Network: trust-on-first-use or forget saved certificates.";
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
    } else if (kind == BR_ERR_TLS) {
        const char *tls_err = tls_last_error();
        int tls_code = tls_last_error_code();
        if (tls_err && tls_err[0]) {
            char line[BR_TEXT_MAX];
            snprintf(line, sizeof(line), "[%s] %s", tls_err_name(tls_code), tls_err);
            browser_add_block(t, BR_CODE, line);
        }
        if (detail && detail[0]) {
            char line[BR_TEXT_MAX];
            snprintf(line, sizeof(line), "%s", detail);
            browser_add_block(t, BR_P, line);
        }
    } else if (detail && detail[0]) {
        char line[BR_TEXT_MAX];
        snprintf(line, sizeof(line), "%s", detail);
        browser_add_block(t, BR_P, line);
    }
    if (kind == BR_ERR_DNS || kind == BR_ERR_NETWORK || kind == BR_ERR_HTTP) {
        const char *net_err = net_last_error();
        if (net_err && net_err[0]) {
            char line[BR_TEXT_MAX];
            snprintf(line, sizeof(line), "%s", net_err);
            browser_add_block(t, BR_CODE, line);
        }
    }

    if (kind == BR_ERR_TLS && detail)
        snprintf(t->status, sizeof(t->status), "%s — %s",
                 detail, tls_last_error() ? tls_last_error() : "TLS error");
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
    if (!browser_hist_navigating && t->url[0] && strcmp(t->url, norm))
        hist_push(t, t->url);
    snprintf(t->url, sizeof(t->url), "%s", norm);
    t->scroll_y = 0;
    t->show_retry = 0;
    t->show_tls_accept = 0;

    if (!strcmp(t->url, "peak://demo") || !strcmp(t->url, "about:js") ||
        !strcmp(t->url, "peak:demo")) {
        t->http_status = 200;
        t->fetching = 0;
        load_document(t, peak_js_demo_html());
        snprintf(t->title, sizeof(t->title), "Peak JS Demo");
        return;
    }

    snprintf(t->status, sizeof(t->status), "Fetching...");
    t->fetching = 1;
    t->fetch_start = timer_ticks();
    needs_redraw = 1;

    char *body = ensure_body_cache();
    if (!body) {
        t->fetching = 0;
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
            t->fetching = 0;
            browser_error_page(t, BR_ERR_LOCAL, t->url, st);
            return;
        }
    } else {
        if (!net_ready()) {
            t->fetching = 0;
            browser_error_page(t, BR_ERR_NETWORK, NULL, 0);
            return;
        }
        snprintf(t->status, sizeof(t->status), "DNS + TCP/TLS...");
        ok = (net_http_get(t->url, body, BR_BODY_MAX, &st) == 0);

        if (!ok) {
            t->http_status = st;
            t->fetching = 0;
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
    t->fetching = 0;
    t->http2 = net_http_last_h2();
    t->body_truncated = net_http_last_body_truncated();
    t->body_total = net_http_last_body_total();
    t->last_body_len = net_http_last_body_stored();
    if (!t->last_body_len)
        t->last_body_len = strlen(body);
    if (t->body_truncated && !t->reader_reason[0])
        snprintf(t->reader_reason, sizeof(t->reader_reason), "body truncated");
    load_document(t, body);
}

/* Form submit: build GET query or POST body from form controls. */
int browser_form_submit(struct br_tab *t, int form_node) {
    if (!t || form_node < 0)
        return -1;
    struct dom_node *form = dom_node(&t->doc, form_node);
    if (!form || strcmp(form->tag, "form") != 0)
        return -1;
    const char *action = dom_get_attr(&t->doc, form_node, "action");
    const char *method = dom_get_attr(&t->doc, form_node, "method");
    int is_post = method && (method[0] == 'p' || method[0] == 'P');
    char abs[BR_URL_MAX];
    if (!action || !action[0])
        snprintf(abs, sizeof(abs), "%s", t->url);
    else if (http_resolve_url(t->url, action, abs, sizeof(abs)) != 0)
        snprintf(abs, sizeof(abs), "%s", action);

    char payload[1024];
    size_t po = 0;
    payload[0] = '\0';
    for (int id = 0; id < t->doc.nnodes; id++) {
        struct dom_node *n = dom_node(&t->doc, id);
        if (!n || n->type != DOM_ELEMENT)
            continue;
        if (strcmp(n->tag, "input") && strcmp(n->tag, "textarea") && strcmp(n->tag, "select"))
            continue;
        /* Lite: any control in document belongs to nearest form (single-form pages). */
        const char *name = dom_get_attr(&t->doc, id, "name");
        if (!name || !name[0])
            continue;
        const char *type = dom_get_attr(&t->doc, id, "type");
        if (type && (!strcmp(type, "submit") || !strcmp(type, "button")))
            continue;
        const char *val = dom_get_attr(&t->doc, id, "value");
        char text[96];
        text[0] = '\0';
        if (val)
            snprintf(text, sizeof(text), "%s", val);
        else if (!strcmp(n->tag, "textarea"))
            dom_collect_text(&t->doc, id, text, sizeof(text));
        if (id == t->focus_node && t->focus_value[0])
            snprintf(text, sizeof(text), "%s", t->focus_value);
        if (po && po + 1 < sizeof(payload))
            payload[po++] = '&';
        /* Minimal urlencode: alnum and .-_ pass; else %XX */
        for (const char *p = name; *p && po + 4 < sizeof(payload); p++) {
            char c = *p;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
                payload[po++] = c;
            else if (c == ' ')
                payload[po++] = '+';
            else {
                static const char *hex = "0123456789ABCDEF";
                payload[po++] = '%';
                payload[po++] = hex[(uint8_t)c >> 4];
                payload[po++] = hex[(uint8_t)c & 0xf];
            }
        }
        if (po + 1 < sizeof(payload))
            payload[po++] = '=';
        for (const char *p = text; *p && po + 4 < sizeof(payload); p++) {
            char c = *p;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
                payload[po++] = c;
            else if (c == ' ')
                payload[po++] = '+';
            else {
                static const char *hex = "0123456789ABCDEF";
                payload[po++] = '%';
                payload[po++] = hex[(uint8_t)c >> 4];
                payload[po++] = hex[(uint8_t)c & 0xf];
            }
        }
        payload[po] = '\0';
    }

    if (!is_post) {
        char url[BR_URL_MAX];
        if (strchr(abs, '?'))
            snprintf(url, sizeof(url), "%s&%s", abs, payload);
        else
            snprintf(url, sizeof(url), "%s?%s", abs, payload);
        browser_go(url);
        return 0;
    }

    /* POST */
    privacy_grant_net_client(0);
    hist_push(t, t->url);
    snprintf(t->url, sizeof(t->url), "%s", abs);
    char *body = ensure_body_cache();
    if (!body)
        return -1;
    int st = 0;
    struct net_http_request req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "POST");
    req.url = abs;
    req.headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    req.body = payload;
    req.body_len = strlen(payload);
    t->fetching = 1;
    needs_redraw = 1;
    int rc = net_http_request(&req, body, BR_BODY_MAX, &st, NULL, 0);
    t->fetching = 0;
    if (rc != 0) {
        browser_error_page(t, BR_ERR_HTTP, NULL, st);
        return -1;
    }
    t->http_status = st;
    t->http2 = net_http_last_h2();
    t->body_truncated = net_http_last_body_truncated();
    t->last_body_len = net_http_last_body_stored();
    load_document(t, body);
    return 0;
}

int browser_wants_redraw(void) {
    return needs_redraw;
}

void browser_clear_wants_redraw(void) {
    needs_redraw = 0;
}

void browser_tick(void) {
    static uint8_t last_spin_frame = 0xff;
    for (int i = 0; i < ntabs; i++) {
        if (!tabs[i].used)
            continue;
        /* Cap fetch spinner dirty to glyph frame changes (~8 ticks). */
        if (tabs[i].fetching) {
            uint8_t frame =
                (uint8_t)(((timer_ticks() - tabs[i].fetch_start) / 8) % 4);
            if (frame != last_spin_frame) {
                last_spin_frame = frame;
                needs_redraw = 1;
            }
        }
        if (!tabs[i].js)
            continue;
        if (js_tick(tabs[i].js))
            needs_redraw = 1; /* timer fire / microtask drain only */
        if (browser_js_flush_pending_nav(&tabs[i].jsh))
            continue; /* tab may have been torn down / replaced */
        if (!tabs[i].js)
            continue;
        if (tabs[i].dom_dirty) {
            tabs[i].dom_dirty = 0;
            browser_rebuild_layout(&tabs[i], 640);
            needs_redraw = 1;
        }
        /* Do not dirty for js_pending_work() — future timers must not peg paint. */
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

const char *browser_page_body(size_t *len_out) {
    struct br_tab *t = browser_cur();
    if (!body_cache || !t || !t->last_body_len) {
        if (len_out)
            *len_out = 0;
        return NULL;
    }
    if (len_out)
        *len_out = t->last_body_len;
    return body_cache;
}
