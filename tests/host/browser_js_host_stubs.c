/*
 * Host stubs for browser_js.c under PEAK_HOST_TEST.
 */
#include "browser.h"

#include <stdio.h>
#include <string.h>

static char last_go[160];
static int go_count;
static int reload_count;

void browser_go(const char *url) {
    go_count++;
    if (url)
        snprintf(last_go, sizeof(last_go), "%s", url);
    else
        last_go[0] = '\0';
}

void browser_reload(void) {
    reload_count++;
}

void browser_js_host_test_reset(void) {
    go_count = 0;
    reload_count = 0;
    last_go[0] = '\0';
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
