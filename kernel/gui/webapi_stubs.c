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

static int method_is_allowed(struct js_runtime *rt, const struct js_value *v,
                             char *out_method, size_t out_len) {
    char buf[16];
    js_val_to_cstring(rt, v, buf, sizeof(buf));
    if (!buf[0])
        return 0;
    for (size_t i = 0; buf[i]; i++) {
        char c = buf[i];
        if (c >= 'a' && c <= 'z')
            buf[i] = (char)(c - 'a' + 'A');
    }
    if (!strcmp(buf, "GET") || !strcmp(buf, "POST")) {
        if (out_method && out_len) {
            size_t n = strlen(buf);
            if (n + 1 > out_len)
                n = out_len - 1;
            memcpy(out_method, buf, n);
            out_method[n] = '\0';
        }
        return 1;
    }
    return 0;
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
                                 void *ret, char *method_out, size_t method_len,
                                 char *body_out, size_t body_len,
                                 int *have_body, struct js_value *signal_out) {
    struct js_value v;
    snprintf(method_out, method_len, "GET");
    *have_body = 0;
    if (signal_out)
        js_val_set_undefined(signal_out);
    if (js_val_get_prop(rt, init, "method", &v) == 0 && !js_val_is_undefined(&v) &&
        !js_val_is_null(&v)) {
        if (!method_is_allowed(rt, &v, method_out, method_len))
            return stub_fail(rt, ret, "fetch: only GET/POST supported");
    }
    if (js_val_get_prop(rt, init, "signal", &v) == 0 && !js_val_is_undefined(&v) &&
        !js_val_is_null(&v)) {
        if (!js_val_is_object(&v))
            return stub_fail(rt, ret, "fetch: AbortSignal required");
        if (signal_out)
            *signal_out = v;
    }
    if (js_val_get_prop(rt, init, "body", &v) == 0 && !js_val_is_undefined(&v) &&
        !js_val_is_null(&v)) {
        if (strcmp(method_out, "POST") != 0)
            return stub_fail(rt, ret, "fetch: request body only with POST");
        if (js_val_is_object(&v) || js_val_is_function(&v))
            return stub_fail(rt, ret, "fetch: body must be string");
        js_val_to_cstring(rt, &v, body_out, body_len);
        if (strlen(body_out) + 1 >= body_len)
            return stub_fail(rt, ret, "fetch: body too large");
        *have_body = 1;
    }
    for (size_t i = 0; k_fetch_unsupported_init[i]; i++) {
        if (js_val_get_prop(rt, init, k_fetch_unsupported_init[i], &v) == 0 &&
            !js_val_is_undefined(&v) && !js_val_is_null(&v))
            return stub_fail(rt, ret, "fetch: unsupported init option");
    }
    return 0;
}

static int signal_is_aborted(struct js_runtime *rt, const struct js_value *sig) {
    if (!sig || js_val_is_undefined(sig) || js_val_is_null(sig))
        return 0;
    struct js_value aborted;
    if (js_val_get_prop(rt, sig, "aborted", &aborted) != 0)
        return 0;
    return js_val_to_bool(&aborted);
}

static int stub_fetch(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    (void)ud;
    js_val_set_undefined(ret);
    if (!rt || argc < 1)
        return stub_fail(rt, ret, "fetch: URL required");

    struct js_value *args = argv;
    if (js_val_is_null(&args[0]) || js_val_is_undefined(&args[0]))
        return stub_fail(rt, ret, "fetch: URL required");
    if (js_val_is_object(&args[0]) || js_val_is_function(&args[0]))
        return stub_fail(rt, ret, "fetch: URL string required");

    char method[8] = "GET";
    char body_buf[4096];
    int have_body = 0;
    struct js_value signal;
    js_val_set_undefined(&signal);
    body_buf[0] = '\0';

    if (argc >= 2 && !js_val_is_undefined(&args[1]) && !js_val_is_null(&args[1])) {
        if (!js_val_is_object(&args[1]))
            return stub_fail(rt, ret, "fetch: init must be object");
        if (stub_fetch_check_init(rt, &args[1], ret, method, sizeof(method),
                                  body_buf, sizeof(body_buf), &have_body, &signal) != 0)
            return -1;
    }

    if (signal_is_aborted(rt, &signal))
        return stub_fail(rt, ret, "fetch: aborted");

    char url[320];
    js_val_to_cstring(rt, &args[0], url, sizeof(url));
    if (!url[0])
        return stub_fail(rt, ret, "fetch: URL required");
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
    if (strncmp(abs, "http://", 7) != 0 && strncmp(abs, "https://", 8) != 0)
        return stub_fail(rt, ret, "fetch: only http(s) URLs supported");
    if (http_blocks_active_mixed(g_web_page_url, abs))
        return stub_fail(rt, ret, "fetch: mixed-content");

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
    snprintf(req.method, sizeof(req.method), "%s", method);
    req.url = abs;
    if (have_body) {
        req.body = body_buf;
        req.body_len = strlen(body_buf);
    }
    if (signal_is_aborted(rt, &signal)) {
        kfree(body);
        kfree(hdrs);
        return stub_fail(rt, ret, "fetch: aborted");
    }
    int rc = net_http_request(&req, body, 64 * 1024, &st, hdrs, 2048);
    if (rc != 0 && net_http_needs_tls()) {
        kfree(body);
        kfree(hdrs);
        return stub_fail(rt, ret, net_http_tls_reject_name());
    }
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

static int stub_storage_remove(struct js_runtime *rt, int argc, void *argv, void *ret,
                               void *ud) {
    char key[WEB_STORE_KEY_SCRATCH];
    js_val_set_undefined(ret);
    if (argc < 1)
        return 0;
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], key, sizeof(key));
    if (!key[0] || strlen(key) >= WEB_STORE_KEY)
        return 0;
    (void)web_store_remove(web_store_for((const char *)ud), key);
    return 0;
}

void webapi_install_storage_stubs(struct js_runtime *rt) {
    struct js_value ls, ss;
    js_val_new_object(rt, &ls);
    js_val_new_object(rt, &ss);
    webapi_install_fn(rt, &ls, "getItem", stub_storage_get, (void *)"local");
    webapi_install_fn(rt, &ls, "setItem", stub_storage_set, (void *)"local");
    webapi_install_fn(rt, &ls, "removeItem", stub_storage_remove, (void *)"local");
    webapi_install_fn(rt, &ss, "getItem", stub_storage_get, (void *)"session");
    webapi_install_fn(rt, &ss, "setItem", stub_storage_set, (void *)"session");
    webapi_install_fn(rt, &ss, "removeItem", stub_storage_remove, (void *)"session");
    js_obj_set(rt, rt->global, "localStorage", ls);
    js_obj_set(rt, rt->global, "sessionStorage", ss);
}

static int stub_abort_fn(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    (void)argc;
    (void)argv;
    js_val_set_undefined(ret);
    struct js_object *sig_obj = ud;
    if (!sig_obj)
        return 0;
    struct js_value signal;
    signal.type = JT_OBJ;
    signal.u.o = sig_obj;
    struct js_value t;
    js_val_set_bool(&t, 1);
    js_val_set_prop(rt, &signal, "aborted", &t);
    return 0;
}

/* Factory (Peak JS has no native `new` for host ctors): AbortController(). */
static int stub_abort_controller(struct js_runtime *rt, int argc, void *argv, void *ret,
                                 void *ud) {
    (void)argc;
    (void)argv;
    (void)ud;
    struct js_value signal, ctl;
    js_val_new_object(rt, &signal);
    struct js_value aborted;
    js_val_set_bool(&aborted, 0);
    js_val_set_prop(rt, &signal, "aborted", &aborted);
    js_val_new_object(rt, &ctl);
    js_val_set_prop(rt, &ctl, "signal", &signal);
    /* Bind abort to this signal object via userdata pointer on a native fn. */
    webapi_install_fn(rt, &ctl, "abort", stub_abort_fn, signal.u.o);
    *(struct js_value *)ret = ctl;
    return 0;
}

/*
 * Minimal AbortController factory with working signal.aborted + abort().
 * Call as AbortController() (no `new` — Peak VM lacks host ctor OP_NEW).
 */
void webapi_install_abort_controller_stub(struct js_runtime *rt) {
    js_rt_set_global_fn(rt, "AbortController", stub_abort_controller, NULL);
}
