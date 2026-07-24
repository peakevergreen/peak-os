#ifndef PEAK_BROWSER_JS_H
#define PEAK_BROWSER_JS_H

#include "js.h"
#include "dom.h"

/* Per-tab host bridge for Peak Browser ↔ JS. */
struct browser_js_host {
    struct js_runtime *rt;
    struct dom_document *doc;
    int *dirty;          /* set when DOM mutates / timers fire */
    uint32_t handle_gen; /* bumped on navigate; stale DOM handles fail closed */
    char console_log[8][96];
    int console_n;
    /* Simple event listeners: element id hash → function (one click listener). */
    struct {
        int used;
        int node_id;
        char type[16];
        uint8_t fn[JS_VALUE_BYTES];
    } listeners[32];
    int nlisteners;
};

void browser_js_host_init(struct browser_js_host *h, struct js_runtime *rt,
                          struct dom_document *doc, int *dirty);
/* Invalidate outstanding JS DOM handles (call on navigate / tab reset). */
void browser_js_invalidate_handles(struct browser_js_host *h);
int browser_js_install_dom(struct browser_js_host *h);
int browser_js_run_scripts(struct browser_js_host *h);
int browser_js_dispatch_click(struct browser_js_host *h, int node_id);
int browser_js_dispatch_input(struct browser_js_host *h, int node_id, const char *value);

#endif
