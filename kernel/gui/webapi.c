#include "webapi.h"
#include "../js/js_internal.h"
#include "http_util.h"
#include "net.h"
#include "heap.h"
#include "util.h"

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
static struct web_store g_local[WEB_TAB_STORES];
static struct web_store g_session[WEB_TAB_STORES];
static int g_tab_id;
static char g_page_url[WEB_ORIGIN_MAX];
static int g_private_tab;

void webapi_set_tab(int tab_id, int private_tab) {
    if (tab_id < 0)
        tab_id = 0;
    if (tab_id >= WEB_TAB_STORES)
        tab_id = WEB_TAB_STORES - 1;
    g_tab_id = tab_id;
    g_private_tab = private_tab ? 1 : 0;
}

void webapi_clear_tab(int tab_id) {
    if (tab_id < 0 || tab_id >= WEB_TAB_STORES)
        return;
    memset(&g_local[tab_id], 0, sizeof(g_local[tab_id]));
    memset(&g_session[tab_id], 0, sizeof(g_session[tab_id]));
}

static struct web_store *store_for(const char *which) {
    int id = g_tab_id;
    if (which && which[0] == 's')
        return &g_session[id];
    if (g_private_tab)
        return &g_session[id]; /* private: never use durable local store */
    return &g_local[id];
}

static int store_get(struct web_store *s, const char *key, char *out, size_t cap) {
    if (!s || !key)
        return -1;
    for (int i = 0; i < WEB_STORE_KEYS; i++) {
        if (s->items[i].used && !strcmp(s->items[i].key, key)) {
            snprintf(out, cap, "%s", s->items[i].val);
            return 0;
        }
    }
    return -1;
}

static int store_set(struct web_store *s, const char *key, const char *val) {
    if (!s || !key)
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

static void install_fn(struct js_runtime *rt, struct js_value *obj, const char *name,
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

static int cors_ok(const char *page_url, const char *req_url, const char *hdrs) {
    if (http_same_origin(page_url, req_url))
        return 1;
    if (!hdrs)
        return 0;
    const char *p = hdrs;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\r' && *p != '\n')
            p++;
        size_t len = (size_t)(p - line);
        if (len > 27) {
            int match = 1;
            const char *key = "access-control-allow-origin:";
            for (size_t i = 0; key[i]; i++) {
                char c = line[i];
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
                if (c != key[i]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                const char *v = line + 27;
                while (*v == ' ')
                    v++;
                char acao[192];
                size_t i = 0;
                while (v[i] && v[i] != '\r' && v[i] != '\n' && i + 1 < sizeof(acao)) {
                    acao[i] = v[i];
                    i++;
                }
                acao[i] = '\0';
                return http_cors_allows(page_url, acao, 0);
            }
        }
        if (*p == '\r')
            p++;
        if (*p == '\n')
            p++;
    }
    return 0;
}

static int nat_fetch(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    (void)ud;
    js_val_set_undefined(ret);
    if (!rt || argc < 1)
        return 0;
    char url[320];
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], url, sizeof(url));
    char abs[320];
    if (http_resolve_url(g_page_url, url, abs, sizeof(abs)) != 0)
        snprintf(abs, sizeof(abs), "%s", url);

    char *body = kmalloc(64 * 1024);
    char *hdrs = kmalloc(2048);
    if (!body || !hdrs) {
        kfree(body);
        kfree(hdrs);
        return 0;
    }
    int st = 0;
    struct net_http_request req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    req.url = abs;
    int rc = net_http_request(&req, body, 64 * 1024, &st, hdrs, 2048);
    struct js_value o;
    js_val_new_object(rt, &o);
    if (rc != 0 || !cors_ok(g_page_url, abs, hdrs)) {
        struct js_value ok;
        js_val_set_bool(&ok, 0);
        js_val_set_prop(rt, &o, "ok", &ok);
        struct js_value status;
        js_val_set_number(&status, (double)st);
        js_val_set_prop(rt, &o, "status", &status);
        *(struct js_value *)ret = o;
        kfree(body);
        kfree(hdrs);
        return 0;
    }
    struct js_value ok;
    js_val_set_bool(&ok, st >= 200 && st < 300);
    js_val_set_prop(rt, &o, "ok", &ok);
    struct js_value status;
    js_val_set_number(&status, (double)st);
    js_val_set_prop(rt, &o, "status", &status);
    struct js_value txt;
    js_val_set_string(rt, &txt, body);
    js_val_set_prop(rt, &o, "bodyText", &txt);
    *(struct js_value *)ret = o;
    kfree(body);
    kfree(hdrs);
    return 0;
}

static int nat_storage_get(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    char key[48], val[WEB_STORE_VAL];
    js_val_set_null(ret);
    if (argc < 1)
        return 0;
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], key, sizeof(key));
    if (store_get(store_for((const char *)ud), key, val, sizeof(val)) == 0)
        js_val_set_string(rt, ret, val);
    return 0;
}

static int nat_storage_set(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    char key[48], val[WEB_STORE_VAL];
    js_val_set_undefined(ret);
    if (argc < 2)
        return 0;
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], key, sizeof(key));
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], val, sizeof(val));
    store_set(store_for((const char *)ud), key, val);
    return 0;
}

static int nat_abort_ctor(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    (void)argc;
    (void)argv;
    (void)ud;
    js_val_new_object(rt, ret);
    struct js_value sig;
    js_val_new_object(rt, &sig);
    js_val_set_prop(rt, (struct js_value *)ret, "signal", &sig);
    return 0;
}

int webapi_install(struct js_runtime *rt, const char *page_url) {
    if (!rt)
        return -1;
    snprintf(g_page_url, sizeof(g_page_url), "%s", page_url ? page_url : "");
    char origin[WEB_ORIGIN_MAX];
    if (http_parse_origin(g_page_url, origin, sizeof(origin)) == 0) {
        struct web_store *L = &g_local[g_tab_id];
        struct web_store *S = &g_session[g_tab_id];
        if (strcmp(L->origin, origin) != 0) {
            memset(L, 0, sizeof(*L));
            snprintf(L->origin, sizeof(L->origin), "%s", origin);
        }
        if (strcmp(S->origin, origin) != 0) {
            memset(S, 0, sizeof(*S));
            snprintf(S->origin, sizeof(S->origin), "%s", origin);
        }
    }

    js_rt_set_global_fn(rt, "fetch", nat_fetch, NULL);

    struct js_value ls, ss;
    js_val_new_object(rt, &ls);
    js_val_new_object(rt, &ss);
    install_fn(rt, &ls, "getItem", nat_storage_get, (void *)"local");
    install_fn(rt, &ls, "setItem", nat_storage_set, (void *)"local");
    install_fn(rt, &ss, "getItem", nat_storage_get, (void *)"session");
    install_fn(rt, &ss, "setItem", nat_storage_set, (void *)"session");
    js_obj_set(rt, rt->global, "localStorage", ls);
    js_obj_set(rt, rt->global, "sessionStorage", ss);

    js_rt_set_global_fn(rt, "AbortController", nat_abort_ctor, NULL);
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
