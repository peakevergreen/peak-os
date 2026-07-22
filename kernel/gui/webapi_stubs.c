/*
 * Quarantined Web API stubs for browser tabs.
 *
 * These are intentionally partial — not full browser implementations.
 * Scripts must not assume spec-complete fetch, persistent storage, or abort.
 */
#include "webapi_internal.h"
#include "webapi.h"
#include "http_util.h"
#include "net.h"
#include "heap.h"
#include "util.h"

/* --- fetch (STUB: GET-only, same-origin/CORS, no .json()/streams/abort) --- */

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

static int stub_fetch(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    (void)ud;
    js_val_set_undefined(ret);
    if (!rt || argc < 1)
        return 0;
    char url[320];
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], url, sizeof(url));
    char abs[320];
    if (http_resolve_url(g_web_page_url, url, abs, sizeof(abs)) != 0)
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
    if (rc != 0 || !cors_ok(g_web_page_url, abs, hdrs)) {
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

void webapi_install_fetch_stub(struct js_runtime *rt) {
    js_rt_set_global_fn(rt, "fetch", stub_fetch, NULL);
}

/* --- storage (STUB: in-memory per-tab; not persistent localStorage) --- */

static int stub_storage_get(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    char key[48], val[WEB_STORE_VAL];
    js_val_set_null(ret);
    if (argc < 1)
        return 0;
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], key, sizeof(key));
    if (web_store_get(web_store_for((const char *)ud), key, val, sizeof(val)) == 0)
        js_val_set_string(rt, ret, val);
    return 0;
}

static int stub_storage_set(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    char key[48], val[WEB_STORE_VAL];
    js_val_set_undefined(ret);
    if (argc < 2)
        return 0;
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], key, sizeof(key));
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], val, sizeof(val));
    web_store_set(web_store_for((const char *)ud), key, val);
    return 0;
}

void webapi_install_storage_stubs(struct js_runtime *rt) {
    struct js_value ls, ss;
    js_val_new_object(rt, &ls);
    js_val_new_object(rt, &ss);
    webapi_install_fn(rt, &ls, "getItem", stub_storage_get, (void *)"local");
    webapi_install_fn(rt, &ls, "setItem", stub_storage_set, (void *)"local");
    webapi_install_fn(rt, &ss, "getItem", stub_storage_get, (void *)"session");
    webapi_install_fn(rt, &ss, "setItem", stub_storage_set, (void *)"session");
    js_obj_set(rt, rt->global, "localStorage", ls);
    js_obj_set(rt, rt->global, "sessionStorage", ss);
}

/* --- AbortController (STUB: shell only; abort/signal not wired to fetch) --- */

static int stub_abort_ctor(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    (void)argc;
    (void)argv;
    (void)ud;
    js_val_new_object(rt, ret);
    struct js_value sig;
    js_val_new_object(rt, &sig);
    struct js_value aborted;
    js_val_set_bool(&aborted, 0); /* STUB: never transitions to true */
    js_val_set_prop(rt, &sig, "aborted", &aborted);
    js_val_set_prop(rt, (struct js_value *)ret, "signal", &sig);
    return 0;
}

void webapi_install_abort_controller_stub(struct js_runtime *rt) {
    js_rt_set_global_fn(rt, "AbortController", stub_abort_ctor, NULL);
}
