/*
 * Host tests for Web API stubs: unsupported surfaces fail closed/clearly.
 */
#include "js.h"
#include "webapi.h"
#include "webapi_internal.h"

#include <stdio.h>
#include <string.h>

void webapi_host_set_http(int rc, int status, const char *body, const char *headers);
void webapi_host_set_tls_fail(const char *reject_name);
void webapi_host_clear_tls(void);

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int eval_ok(struct js_runtime *rt, const char *src, const char *want) {
    char out[128];
    if (js_eval(rt, src, "<webapi>", out, sizeof(out)) != 0) {
        fprintf(stderr, "FAIL eval '%s': %s\n", src, js_last_error(rt));
        fails++;
        return 0;
    }
    if (want && strcmp(out, want) != 0) {
        fprintf(stderr, "FAIL '%s' => got '%s' want '%s'\n", src, out, want);
        fails++;
        return 0;
    }
    return 1;
}

static int eval_fails(struct js_runtime *rt, const char *src, const char *err_sub) {
    char out[64];
    int rc = js_eval(rt, src, "<webapi>", out, sizeof(out));
    if (rc == 0) {
        fprintf(stderr, "FAIL expected error for '%s' (got %s)\n", src, out);
        fails++;
        return 0;
    }
    const char *err = js_last_error(rt);
    if (err_sub && (!err || !strstr(err, err_sub))) {
        fprintf(stderr, "FAIL '%s' error '%s' missing '%s'\n", src, err ? err : "(null)",
                err_sub);
        fails++;
        return 0;
    }
    return 1;
}

int main(void) {
    struct js_runtime *rt = js_rt_create();
    expect(rt != NULL, "create runtime");
    if (!rt)
        return 1;

    webapi_set_tab(0, 0);
    webapi_clear_tab(0);
    expect(webapi_install(rt, "https://example.com/page") == 0, "webapi_install");

    /* AbortController factory with live signal.aborted. */
    eval_ok(rt, "typeof AbortController", "\"function\"");
    eval_ok(rt, "var ac=AbortController(); ac.signal.aborted", "false");
    eval_ok(rt, "var ac=AbortController(); ac.abort(); ac.signal.aborted", "true");

    /* fetch: unsupported init / URL edges fail closed with clear errors. */
    eval_fails(rt, "fetch()", "URL required");
    eval_fails(rt, "fetch('')", "URL required");
    eval_fails(rt, "fetch({})", "URL string required");
    eval_fails(rt, "fetch('javascript:alert(1)')", "only http(s)");
    eval_fails(rt, "fetch('data:text/plain,hi')", "only http(s)");
    eval_fails(rt, "fetch('https://example.com/x',{method:'HEAD'})", "only GET/POST");
    eval_fails(rt, "fetch('https://example.com/x',{method:''})", "only GET/POST");
    eval_fails(rt, "fetch('https://example.com/x',{body:'x'})", "only with POST");
    eval_fails(rt, "fetch('https://example.com/x',{headers:{}})", "unsupported init");
    eval_fails(rt, "fetch('https://example.com/x',{credentials:'include'})", "unsupported init");
    eval_fails(rt, "fetch('https://example.com/x',{mode:'cors'})", "unsupported init");
    eval_fails(rt, "fetch('https://example.com/x','bad')", "init must be object");
    eval_fails(rt, "var ac=AbortController(); ac.abort(); fetch('https://example.com/x',{signal:ac.signal})",
               "aborted");

    /* fetch: GET path returns stub response shape (host-canned HTTP). */
    webapi_host_set_http(0, 200, "hello", "");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.ok", "true");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.status", "200");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.bodyText", "\"hello\"");
    eval_ok(rt, "var r=fetch('https://example.com/x',{method:'GET'}); r.ok", "true");
    eval_ok(rt, "var r=fetch('https://example.com/x',{}); r.ok", "true");
    eval_ok(rt, "var r=fetch('https://example.com/x',{method:'get'}); r.ok", "true");
    eval_ok(rt, "var r=fetch('/rel'); r.ok", "true"); /* relative → same-origin https */
    eval_ok(rt, "var r=fetch('https://example.com/x',{method:'POST',body:'hi'}); r.ok", "true");

    /* Network / CORS failure → ok:false (not a silent success). */
    webapi_host_set_http(-1, 0, "", "");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.ok", "false");
    webapi_host_set_http(0, 200, "x", "");
    eval_ok(rt, "var r=fetch('https://other.example/x'); r.ok", "false"); /* cross-origin no ACAO */

    /* HTTPS TLS failures reject with stable fetch: tls-* names. */
    webapi_host_set_tls_fail("fetch: tls-rng");
    eval_fails(rt, "fetch('https://example.com/x')", "tls-rng");
    webapi_host_set_tls_fail("fetch: tls-alert");
    eval_fails(rt, "fetch('https://example.com/x')", "tls-alert");
    webapi_host_set_tls_fail("fetch: tls-expired");
    eval_fails(rt, "fetch('https://example.com/x')", "tls-expired");
    webapi_host_set_tls_fail("fetch: tls-mismatch");
    eval_fails(rt, "fetch('https://example.com/x')", "tls-mismatch");
    webapi_host_set_tls_fail("fetch: tls-untrusted");
    eval_fails(rt, "fetch('https://example.com/x')", "tls-untrusted");
    webapi_host_clear_tls();
    webapi_host_set_http(0, 200, "x", "");

    /* Active mixed content blocked on HTTPS pages. */
    eval_fails(rt, "fetch('http://evil.example/x')", "mixed-content");

    /* Storage: in-memory get/set; quota / empty / oversized fail closed. */
    eval_ok(rt, "localStorage.setItem('a','1'); localStorage.getItem('a')", "\"1\"");
    eval_ok(rt, "localStorage.removeItem('a'); localStorage.getItem('a')", "null");
    eval_ok(rt, "sessionStorage.setItem('b','2'); sessionStorage.getItem('b')", "\"2\"");
    eval_ok(rt, "localStorage.getItem('missing')", "null");
    eval_ok(rt, "typeof localStorage.removeItem", "\"function\"");
    eval_ok(rt, "typeof localStorage.clear", "\"undefined\"");
    eval_fails(rt, "localStorage.setItem('only')", "key and value");
    eval_fails(rt, "localStorage.setItem('','x')", "empty key");

    {
        /* Key longer than WEB_STORE_KEY-1 must not silently truncate. */
        char src[128];
        char longkey[WEB_STORE_KEY + 4];
        memset(longkey, 'k', sizeof(longkey) - 1);
        longkey[sizeof(longkey) - 1] = '\0';
        snprintf(src, sizeof(src), "localStorage.setItem('%s','v')", longkey);
        eval_fails(rt, src, "quota exceeded");
    }
    {
        /* Value overflow is enforced in web_store_set (JS concat caps at 255). */
        char longval[WEB_STORE_VAL + 8];
        memset(longval, 'v', sizeof(longval) - 1);
        longval[sizeof(longval) - 1] = '\0';
        expect(web_store_set(web_store_for("local"), "toolong", longval) != 0,
               "web_store_set rejects oversized value");
        expect(web_store_set(web_store_for("local"), "fits", "ok") == 0,
               "web_store_set accepts fitting value");
    }

    {
        char src[2048];
        size_t n = 0;
        n += (size_t)snprintf(src + n, sizeof(src) - n, "var i=0;");
        /* WEB_STORE_KEYS is 16; fill beyond capacity. */
        for (int i = 0; i < 17 && n + 64 < sizeof(src); i++)
            n += (size_t)snprintf(src + n, sizeof(src) - n,
                                  "localStorage.setItem('k%d','v');i=i+1;", i);
        n += (size_t)snprintf(src + n, sizeof(src) - n, "i");
        eval_fails(rt, src, "quota exceeded");
    }

    /* Tab clear isolates in-memory store; private tab never uses durable local. */
    webapi_clear_tab(0);
    eval_ok(rt, "localStorage.getItem('a')", "null");
    webapi_set_tab(0, 1);
    expect(webapi_install(rt, "https://example.com/page") == 0, "reinstall private");
    eval_ok(rt, "localStorage.setItem('priv','1'); sessionStorage.getItem('priv')", "\"1\"");
    webapi_set_tab(0, 0);
    webapi_clear_tab(0);
    expect(webapi_install(rt, "https://example.com/page") == 0, "reinstall normal");
    eval_ok(rt, "localStorage.getItem('priv')", "null");

    js_rt_destroy(rt);
    if (fails) {
        fprintf(stderr, "%d webapi host test(s) failed\n", fails);
        return 1;
    }
    printf("test_webapi: ok\n");
    return 0;
}
