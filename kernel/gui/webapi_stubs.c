/*
 * Quarantined Web API stubs for browser tabs.
 *
 * Intentionally partial — unsupported surfaces fail closed with clear errors
 * rather than silent no-ops that imply spec support.
 */
#include "webapi_internal.h"
#include "webapi.h"
#include "http_util.h"
#include "net.h"
#include "heap.h"
#include "util.h"

static int stub_fail(struct js_runtime *rt, void *ret, const char *msg) {
    if (ret)
        js_val_set_undefined(ret);
    if (rt && msg)
        snprintf(rt->err, sizeof(rt->err), "%s", msg);
    return -1;
}

static int method_is_get(struct js_runtime *rt, const struct js_value *v) {
    char buf[16];
    js_val_to_cstring(rt, v, buf, sizeof(buf));
    /* Explicit empty / non-GET methods fail closed (default GET is omit-only). */
    if (!buf[0])
        return 0;
    const char *g = "GET";
    for (size_t i = 0; g[i]; i++) {
        char c = buf[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        if (c != g[i])
            return 0;
    }
    return buf[3] == '\0';
}

/* Init keys beyond method/signal/body that would imply unsupported fetch features. */
static const char *const k_fetch_unsupported_init[] = {
    "headers", "credentials", "mode", "redirect", "cache", "referrer",
    "referrerPolicy", "integrity", "keepalive", "duplex", NULL,
};

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

static int stub_fetch_check_init(struct js_runtime *rt, const struct js_value *init,
                                 void *ret) {
    struct js_value v;
    if (js_val_get_prop(rt, init, "method", &v) == 0 && !js_val_is_undefined(&v) &&
        !js_val_is_null(&v)) {
        if (!method_is_get(rt, &v))
            return stub_fail(rt, ret, "fetch: only GET supported");
    }
    if (js_val_get_prop(rt, init, "signal", &v) == 0 && !js_val_is_undefined(&v) &&
        !js_val_is_null(&v))
        return stub_fail(rt, ret, "fetch: AbortSignal unsupported");
    if (js_val_get_prop(rt, init, "body", &v) == 0 && !js_val_is_undefined(&v) &&
        !js_val_is_null(&v))
        return stub_fail(rt, ret, "fetch: request body unsupported");
    for (size_t i = 0; k_fetch_unsupported_init[i]; i++) {
        if (js_val_get_prop(rt, init, k_fetch_unsupported_init[i], &v) == 0 &&
            !js_val_is_undefined(&v) && !js_val_is_null(&v))
            return stub_fail(rt, ret, "fetch: unsupported init option");
    }
    return 0;
}

static int stub_fetch(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    (void)ud;
    js_val_set_undefined(ret);
    if (!rt || argc < 1)
        return stub_fail(rt, ret, "fetch: URL required");

    struct js_value *args = argv;
    /* Request / URL objects are unsupported — require a stringifiable scalar URL. */
    if (js_val_is_null(&args[0]) || js_val_is_undefined(&args[0]))
        return stub_fail(rt, ret, "fetch: URL required");
    if (js_val_is_object(&args[0]) || js_val_is_function(&args[0]))
        return stub_fail(rt, ret, "fetch: URL string required");

    if (argc >= 2 && !js_val_is_undefined(&args[1]) && !js_val_is_null(&args[1])) {
        if (!js_val_is_object(&args[1]))
            return stub_fail(rt, ret, "fetch: init must be object");
        if (stub_fetch_check_init(rt, &args[1], ret) != 0)
            return -1;
    }

    char url[320];
    js_val_to_cstring(rt, &args[0], url, sizeof(url));
    if (!url[0])
        return stub_fail(rt, ret, "fetch: URL required");
    /* Reject absolute non-http(s) schemes before resolve (avoid treating
     * javascript:/data: as path-relative under the page origin). */
    {
        const char *p = url;
        while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '.'))
            p++;
        if (*p == ':' && strncmp(url, "http:", 5) != 0 && strncmp(url, "https:", 6) != 0)
            return stub_fail(rt, ret, "fetch: only http(s) URLs supported");
    }
    char abs[320];
    if (http_resolve_url(g_web_page_url, url, abs, sizeof(abs)) != 0)
        snprintf(abs, sizeof(abs), "%s", url);
    /* GET-only stub: only http(s) after resolve. */
    if (strncmp(abs, "http://", 7) != 0 && strncmp(abs, "https://", 8) != 0)
        return stub_fail(rt, ret, "fetch: only http(s) URLs supported");

    char *body = kmalloc(64 * 1024);
    char *hdrs = kmalloc(2048);
    if (!body || !hdrs) {
        kfree(body);
        kfree(hdrs);
        return stub_fail(rt, ret, "fetch: out of memory");
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

/* Scratch larger than store slots so oversized keys/values are detected, not truncated. */
#define WEB_STORE_KEY_SCRATCH (WEB_STORE_KEY + 8)
#define WEB_STORE_VAL_SCRATCH (WEB_STORE_VAL + 8)

static int stub_storage_get(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    char key[WEB_STORE_KEY_SCRATCH], val[WEB_STORE_VAL];
    js_val_set_null(ret);
    if (argc < 1)
        return 0;
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], key, sizeof(key));
    if (!key[0] || strlen(key) >= WEB_STORE_KEY)
        return 0; /* miss / invalid key → null (fail closed, no truncate match) */
    if (web_store_get(web_store_for((const char *)ud), key, val, sizeof(val)) == 0)
        js_val_set_string(rt, ret, val);
    return 0;
}

static int stub_storage_set(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    char key[WEB_STORE_KEY_SCRATCH], val[WEB_STORE_VAL_SCRATCH];
    js_val_set_undefined(ret);
    if (argc < 2)
        return stub_fail(rt, ret, "storage.setItem: key and value required");
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], key, sizeof(key));
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], val, sizeof(val));
    if (!key[0])
        return stub_fail(rt, ret, "storage.setItem: empty key");
    if (strlen(key) >= WEB_STORE_KEY || strlen(val) >= WEB_STORE_VAL)
        return stub_fail(rt, ret, "storage.setItem: quota exceeded");
    if (web_store_set(web_store_for((const char *)ud), key, val) != 0)
        return stub_fail(rt, ret, "storage.setItem: quota exceeded");
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
    /* removeItem / clear / key / length: unsupported — omit (no silent fakes). */
    js_obj_set(rt, rt->global, "localStorage", ls);
    js_obj_set(rt, rt->global, "sessionStorage", ss);
}

/*
 * AbortController is unsupported. Do not install a fake shell (signal.aborted
 * stuck false, no abort wiring) — absence fails closed for feature detection.
 */
void webapi_install_abort_controller_stub(struct js_runtime *rt) {
    (void)rt;
}
