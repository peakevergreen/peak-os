/*
 * Host tests: Browser JS listener GC roots, deferred nav, tab-compact fixup.
 */
#include "browser_js.h"
#include "dom.h"
#include "js.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void browser_js_host_test_reset(void);
void browser_js_host_test_register_tab(int idx, struct browser_js_host *h);
int browser_js_host_test_go_count(void);
int browser_js_host_test_reload_count(void);
const char *browser_js_host_test_last_go(void);
int browser_js_host_test_last_go_tab(void);
int browser_js_host_test_last_reload_tab(void);

static int fails;

static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        fails++;
    }
}

static void setup_doc(struct dom_document *doc) {
    dom_doc_init(doc);
    snprintf(doc->url, sizeof(doc->url), "peak://test");
    int html = dom_create_element(doc, "html");
    doc->root = html;
    int body = dom_create_element(doc, "body");
    doc->body = body;
    dom_append_child(doc, html, body);
    int btn = dom_create_element(doc, "button");
    dom_set_attr(doc, btn, "id", "btn");
    dom_append_child(doc, body, btn);
}

static void test_listener_survives_gc(void) {
    struct dom_document doc;
    int dirty = 0;
    setup_doc(&doc);
    struct js_runtime *rt = js_rt_create();
    expect(rt != NULL, "rt create");
    struct browser_js_host h;
    browser_js_host_init(&h, rt, &doc, &dirty);
    expect(browser_js_install_dom(&h) == 0, "install dom");

    char out[64];
    expect(js_eval(rt,
                   "var hit=0;"
                   "__dom_on(document.getElementById('btn'),'click',function(){ hit = hit + 1; });",
                   "<t>", out, sizeof(out)) == 0,
           "register listener");

    for (int i = 0; i < 8; i++)
        js_rt_gc(rt);

    expect(browser_js_dispatch_click(&h, dom_get_element_by_id(&doc, "btn")) == 0,
           "dispatch after gc");
    expect(js_eval(rt, "hit", "<t>", out, sizeof(out)) == 0, "read hit");
    expect(out[0] == '1', "listener ran after GC");

    js_rt_set_host_mark(rt, NULL, NULL);
    js_rt_destroy(rt);
}

static void test_nav_deferred_until_flush(void) {
    struct dom_document doc;
    int dirty = 0;
    setup_doc(&doc);
    struct js_runtime *rt = js_rt_create();
    struct browser_js_host h;
    browser_js_host_init(&h, rt, &doc, &dirty);
    browser_js_install_dom(&h);
    browser_js_host_test_reset();

    char out[64];
    expect(js_eval(rt, "location.assign('peak://next');", "<t>", out, sizeof(out)) == 0,
           "assign");
    expect(browser_js_host_test_go_count() == 0, "go not during assign");
    expect(h.pending_nav == 1, "pending assign queued");
    expect(strcmp(h.pending_url, "peak://next") == 0, "pending url");

    expect(browser_js_flush_pending_nav(&h) == 1, "flush assign");
    expect(browser_js_host_test_go_count() == 1, "go after flush");
    expect(strcmp(browser_js_host_test_last_go(), "peak://next") == 0, "go url");

    browser_js_host_test_reset();
    expect(js_eval(rt, "setTimeout(function(){ location.reload(); }, 0);", "<t>", out,
                   sizeof(out)) == 0,
           "reload timer");
    for (int i = 0; i < 5; i++)
        js_tick(rt);
    expect(browser_js_host_test_reload_count() == 0, "reload not mid-tick");
    expect(h.pending_nav == 2, "pending reload");
    expect(browser_js_flush_pending_nav(&h) == 1, "flush reload");
    expect(browser_js_host_test_reload_count() == 1, "reload after flush");

    js_rt_set_host_mark(rt, NULL, NULL);
    js_rt_destroy(rt);
}

static void test_flush_targets_owning_tab(void) {
    struct {
        struct dom_document doc;
        int dirty;
        struct js_runtime *js;
        struct browser_js_host jsh;
    } slots[2];

    memset(slots, 0, sizeof(slots));
    for (int i = 0; i < 2; i++) {
        setup_doc(&slots[i].doc);
        slots[i].js = js_rt_create();
        expect(slots[i].js != NULL, "rt create for tab slot");
        browser_js_host_init(&slots[i].jsh, slots[i].js, &slots[i].doc, &slots[i].dirty);
        browser_js_install_dom(&slots[i].jsh);
    }

    browser_js_host_test_reset();
    browser_js_host_test_register_tab(0, &slots[0].jsh);
    browser_js_host_test_register_tab(1, &slots[1].jsh);

    char out[64];
    expect(js_eval(slots[0].js, "location.assign('peak://from-tab0');", "<t>", out,
                   sizeof(out)) == 0,
           "assign on tab0");
    expect(slots[0].jsh.pending_nav == 1, "tab0 pending assign");

    expect(browser_js_flush_pending_nav(&slots[0].jsh) == 1, "flush tab0");
    expect(browser_js_host_test_go_count() == 1, "one go");
    expect(browser_js_host_test_last_go_tab() == 0, "go targeted tab0 not active");
    expect(strcmp(browser_js_host_test_last_go(), "peak://from-tab0") == 0, "go url tab0");

    browser_js_host_test_reset();
    browser_js_host_test_register_tab(0, &slots[0].jsh);
    browser_js_host_test_register_tab(1, &slots[1].jsh);

    expect(js_eval(slots[1].js, "location.reload();", "<t>", out, sizeof(out)) == 0,
           "reload on tab1");
    expect(slots[1].jsh.pending_nav == 2, "tab1 pending reload");
    expect(browser_js_flush_pending_nav(&slots[1].jsh) == 1, "flush tab1");
    expect(browser_js_host_test_reload_count() == 1, "one reload");
    expect(browser_js_host_test_last_reload_tab() == 1, "reload targeted tab1");

    for (int i = 0; i < 2; i++) {
        js_rt_set_host_mark(slots[i].js, NULL, NULL);
        js_rt_destroy(slots[i].js);
    }
}

static void test_fixup_after_move(void) {
    struct {
        struct dom_document doc;
        int dirty;
        struct js_runtime *js;
        struct browser_js_host jsh;
    } slots[2];

    memset(slots, 0, sizeof(slots));
    setup_doc(&slots[1].doc);
    slots[1].js = js_rt_create();
    browser_js_host_init(&slots[1].jsh, slots[1].js, &slots[1].doc, &slots[1].dirty);
    browser_js_install_dom(&slots[1].jsh);

    void *old = &slots[1].jsh;
    slots[0] = slots[1];
    memset(&slots[1], 0, sizeof(slots[1]));
    browser_js_fixup_after_move(&slots[0].jsh, slots[0].js, &slots[0].doc,
                                  &slots[0].dirty, old);

    expect(slots[0].jsh.doc == &slots[0].doc, "doc rebound");
    expect(slots[0].jsh.dirty == &slots[0].dirty, "dirty rebound");
    expect(slots[0].jsh.rt == slots[0].js, "rt rebound");

    char out[64];
    expect(js_eval(slots[0].js, "document.getElementById('btn')", "<t>", out, sizeof(out)) == 0,
           "dom after move");
    expect(strstr(out, "object") != NULL || out[0] == '[', "getElementById via rebound userdata");

    js_rt_set_host_mark(slots[0].js, NULL, NULL);
    js_rt_destroy(slots[0].js);
}

int main(void) {
    js_runtime_init();
    test_listener_survives_gc();
    test_nav_deferred_until_flush();
    test_flush_targets_owning_tab();
    test_fixup_after_move();
    if (fails) {
        fprintf(stderr, "%d browser_js test(s) failed\n", fails);
        return 1;
    }
    printf("test_browser_js: ok\n");
    return 0;
}
