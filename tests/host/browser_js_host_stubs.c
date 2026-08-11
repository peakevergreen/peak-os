/*
 * Host stubs for browser_js.c under PEAK_HOST_TEST.
 */
#include "browser.h"
#include "browser_js.h"

#include <stdio.h>
#include <string.h>

#define HOST_STUB_MAX_TABS 4

static char last_go[160];
static int go_count;
static int reload_count;
static int last_go_tab = -1;
static int last_reload_tab = -1;

/* Optional tab table so flush can resolve hosts like the kernel browser. */
static struct browser_js_host *stub_tabs[HOST_STUB_MAX_TABS];
static int stub_ntabs;

void browser_go(const char *url) {
    go_count++;
    last_go_tab = -1;
    if (url)
        snprintf(last_go, sizeof(last_go), "%s", url);
    else
        last_go[0] = '\0';
}

void browser_go_tab(int tab, const char *url) {
    go_count++;
    last_go_tab = tab;
    if (url)
        snprintf(last_go, sizeof(last_go), "%s", url);
    else
        last_go[0] = '\0';
}

void browser_reload(void) {
    reload_count++;
    last_reload_tab = -1;
}

void browser_reload_tab(int tab) {
    reload_count++;
    last_reload_tab = tab;
}

int browser_tab_index_for_js_host(const struct browser_js_host *h) {
    if (!h)
        return -1;
    for (int i = 0; i < stub_ntabs; i++) {
        if (stub_tabs[i] == h)
            return i;
    }
    return -1;
}

void browser_js_host_test_reset(void) {
    go_count = 0;
    reload_count = 0;
    last_go[0] = '\0';
    last_go_tab = -1;
    last_reload_tab = -1;
    stub_ntabs = 0;
    memset(stub_tabs, 0, sizeof(stub_tabs));
}

void browser_js_host_test_register_tab(int idx, struct browser_js_host *h) {
    if (idx < 0 || idx >= HOST_STUB_MAX_TABS)
        return;
    stub_tabs[idx] = h;
    if (idx + 1 > stub_ntabs)
        stub_ntabs = idx + 1;
}

int browser_js_host_test_go_count(void) {
    return go_count;
}

int browser_js_host_test_reload_count(void) {
    return reload_count;
}

const char *browser_js_host_test_last_go(void) {
    return last_go;
}

int browser_js_host_test_last_go_tab(void) {
    return last_go_tab;
}

int browser_js_host_test_last_reload_tab(void) {
    return last_reload_tab;
}
