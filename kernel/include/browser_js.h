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
    char console_filter[32]; /* substring filter; empty = show all */
    /* Event listeners (click/input/change/submit/keydown). */
    struct {
        int used;
        int node_id;
        char type[16];
        uint8_t fn[JS_VALUE_BYTES];
    } listeners[64];
    int nlisteners;
    int prevent_default;
    /* Deferred navigation — never destroy JS mid js_tick/dispatch. */
    int pending_nav; /* 0 none, 1 assign, 2 reload */
    char pending_url[160];
};

void browser_js_host_init(struct browser_js_host *h, struct js_runtime *rt,
                          struct dom_document *doc, int *dirty);
/* After tab array compact: rebind doc/dirty/rt and native userdata. */
void browser_js_fixup_after_move(struct browser_js_host *h, struct js_runtime *rt,
                                   struct dom_document *doc, int *dirty, void *old_host);
/* Apply deferred location.assign / reload; returns 1 if navigation ran. */
int browser_js_flush_pending_nav(struct browser_js_host *h);
/* Invalidate outstanding JS DOM handles (call on navigate / tab reset). */
void browser_js_invalidate_handles(struct browser_js_host *h);
int browser_js_install_dom(struct browser_js_host *h);
int browser_js_run_scripts(struct browser_js_host *h);
int browser_js_dispatch_click(struct browser_js_host *h, int node_id);
int browser_js_dispatch_input(struct browser_js_host *h, int node_id, const char *value);
int browser_js_dispatch_event(struct browser_js_host *h, int node_id, const char *type);
int browser_js_dispatch_keydown(struct browser_js_host *h, int node_id, int key);
void browser_console_clear(struct browser_js_host *h);
void browser_console_set_filter(struct browser_js_host *h, const char *substr);
int browser_console_line_visible(const struct browser_js_host *h, const char *line);

#endif
