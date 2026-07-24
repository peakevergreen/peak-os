#include "webapi.h"
#include "webapi_internal.h"
#include "http_util.h"
#include "net.h"
#include "heap.h"
#include "util.h"

struct web_store g_web_local[WEB_TAB_STORES];
struct web_store g_web_session[WEB_TAB_STORES];
int g_web_tab_id;
char g_web_page_url[WEB_ORIGIN_MAX];
int g_web_private_tab;

void webapi_set_tab(int tab_id, int private_tab) {
    if (tab_id < 0)
        tab_id = 0;
    if (tab_id >= WEB_TAB_STORES)
        tab_id = WEB_TAB_STORES - 1;
    g_web_tab_id = tab_id;
    g_web_private_tab = private_tab ? 1 : 0;
}

void webapi_clear_tab(int tab_id) {
    if (tab_id < 0 || tab_id >= WEB_TAB_STORES)
        return;
    memset(&g_web_local[tab_id], 0, sizeof(g_web_local[tab_id]));
    memset(&g_web_session[tab_id], 0, sizeof(g_web_session[tab_id]));
}

struct web_store *web_store_for(const char *which) {
    int id = g_web_tab_id;
    if (which && which[0] == 's')
        return &g_web_session[id];
    if (g_web_private_tab)
        return &g_web_session[id]; /* private: never use durable local store */
    return &g_web_local[id];
}

int web_store_get(struct web_store *s, const char *key, char *out, size_t cap) {
    if (!s || !key || !key[0] || !out || !cap)
        return -1;
    if (strlen(key) >= WEB_STORE_KEY)
        return -1;
    for (int i = 0; i < WEB_STORE_KEYS; i++) {
        if (s->items[i].used && !strcmp(s->items[i].key, key)) {
            snprintf(out, cap, "%s", s->items[i].val);
            return 0;
        }
    }
    return -1;
}

int web_store_set(struct web_store *s, const char *key, const char *val) {
    if (!s || !key || !key[0])
        return -1;
    /* Fail closed on oversized entries — never silently truncate. */
    if (strlen(key) >= WEB_STORE_KEY)
        return -1;
    if (val && strlen(val) >= WEB_STORE_VAL)
        return -1;
    for (int i = 0; i < WEB_STORE_KEYS; i++) {
        if (s->items[i].used && !strcmp(s->items[i].key, key)) {
            snprintf(s->items[i].val, sizeof(s->items[i].val), "%s", val ? val : "");
            return 0;
        }
    }
    for (int i = 0; i < WEB_STORE_KEYS; i++) {
        if (!s->items[i].used) {
            s->items[i].used = 1;
            snprintf(s->items[i].key, sizeof(s->items[i].key), "%s", key);
            snprintf(s->items[i].val, sizeof(s->items[i].val), "%s", val ? val : "");
            return 0;
        }
    }
    return -1;
}

int web_store_remove(struct web_store *s, const char *key) {
    if (!s || !key || !key[0] || strlen(key) >= WEB_STORE_KEY)
        return -1;
    for (int i = 0; i < WEB_STORE_KEYS; i++) {
        if (s->items[i].used && !strcmp(s->items[i].key, key)) {
            s->items[i].used = 0;
            s->items[i].key[0] = '\0';
            s->items[i].val[0] = '\0';
            return 0;
        }
    }
    return -1;
}

void webapi_install_fn(struct js_runtime *rt, struct js_value *obj, const char *name,
                       js_native_fn fn, void *ud) {
    struct js_object *o = js_obj_new(rt, 0);
    if (!o)
        return;
    o->is_native = 1;
    o->is_func = 1;
    o->native = fn;
    o->userdata = ud;
    struct js_value v;
    v.type = JT_NATIVE;
    v.u.o = o;
    js_val_set_prop(rt, obj, name, &v);
}

int webapi_install(struct js_runtime *rt, const char *page_url) {
    if (!rt)
        return -1;
    snprintf(g_web_page_url, sizeof(g_web_page_url), "%s", page_url ? page_url : "");
    char origin[WEB_ORIGIN_MAX];
    if (http_parse_origin(g_web_page_url, origin, sizeof(origin)) == 0) {
        struct web_store *L = &g_web_local[g_web_tab_id];
        struct web_store *S = &g_web_session[g_web_tab_id];
        if (strcmp(L->origin, origin) != 0) {
            memset(L, 0, sizeof(*L));
            snprintf(L->origin, sizeof(L->origin), "%s", origin);
        }
        if (strcmp(S->origin, origin) != 0) {
            memset(S, 0, sizeof(*S));
            snprintf(S->origin, sizeof(S->origin), "%s", origin);
        }
    }

    webapi_install_fetch_stub(rt);
    webapi_install_storage_stubs(rt);
    webapi_install_abort_controller_stub(rt);
    return 0;
}

int webapi_load_classic_scripts(struct js_runtime *rt, struct dom_document *doc,
                                const char *page_url) {
    if (!rt || !doc)
        return 0;
    char dump[64];
    char *buf = kmalloc(64 * 1024);
    if (!buf)
        return -1;
    int failed = 0;
    for (int i = 0; i < doc->nscripts; i++) {
        struct dom_script *sc = &doc->scripts[i];
        if (!sc->used || !sc->external || !sc->src[0])
            continue;
        char abs[320];
        if (http_resolve_url(page_url ? page_url : "", sc->src, abs, sizeof(abs)) != 0) {
            failed++;
            continue;
        }
        if (!strncmp(abs, "peak:", 5) || !strncmp(abs, "about:", 6))
            continue;
        int st = 0;
        if (net_http_get(abs, buf, 64 * 1024, &st) != 0 || st < 200 || st >= 300) {
            failed++;
            continue;
        }
        if (js_eval(rt, buf, sc->src, dump, sizeof(dump)) != 0)
            failed++;
    }
    kfree(buf);
    return failed ? -1 : 0;
}
