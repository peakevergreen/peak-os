#ifndef PEAK_WEBAPI_INTERNAL_H
#define PEAK_WEBAPI_INTERNAL_H

#include "../js/js_internal.h"

#define WEB_STORE_KEYS 16
#define WEB_STORE_VAL  256
#define WEB_ORIGIN_MAX 160

struct web_kv {
    int used;
    char key[48];
    char val[WEB_STORE_VAL];
};

struct web_store {
    char origin[WEB_ORIGIN_MAX];
    struct web_kv items[WEB_STORE_KEYS];
};

#define WEB_TAB_STORES 4

extern struct web_store g_web_local[WEB_TAB_STORES];
extern struct web_store g_web_session[WEB_TAB_STORES];
extern int g_web_tab_id;
extern char g_web_page_url[WEB_ORIGIN_MAX];
extern int g_web_private_tab;

struct web_store *web_store_for(const char *which);
int web_store_get(struct web_store *s, const char *key, char *out, size_t cap);
int web_store_set(struct web_store *s, const char *key, const char *val);

void webapi_install_fn(struct js_runtime *rt, struct js_value *obj, const char *name,
                       js_native_fn fn, void *ud);

/* Stub installers — partial / non-spec implementations quarantined in webapi_stubs.c. */
void webapi_install_fetch_stub(struct js_runtime *rt);
void webapi_install_storage_stubs(struct js_runtime *rt);
void webapi_install_abort_controller_stub(struct js_runtime *rt);

#endif
