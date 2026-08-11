/*
 * Host tests for Web API stubs: unsupported surfaces fail closed/clearly.
 */
#include "js.h"
#include "webapi.h"
#include "webapi_internal.h"
#include "browser_isolation.h"

#include <stdio.h>
#include <string.h>

void webapi_host_set_http(int rc, int status, const char *body, const char *headers);
void webapi_host_set_tls_fail(const char *reject_name);
void webapi_host_clear_tls(void);
void webapi_host_vfs_reset(void);

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
    webapi_host_vfs_reset();
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
    eval_fails(rt, "fetch('https://example.com/x',{method:'HEAD'})", "method HEAD not supported");
    eval_fails(rt, "fetch('https://example.com/x',{method:'PUT'})", "method PUT not supported");
    eval_fails(rt, "fetch('https://example.com/x',{method:''})", "only GET/POST");
    eval_fails(rt, "fetch('https://example.com/x',{body:'x'})", "only with POST");
    webapi_host_set_http(0, 200, "hello", "");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.ok", "true");
    eval_ok(rt, "var r=fetch('https://example.com/x',{headers:{}}); r.ok", "true");
    eval_ok(rt, "var h={Accept:'text/plain'}; var r=fetch('https://example.com/x',{headers:h}); r.ok", "true");
    eval_fails(rt, "fetch('https://example.com/x',{headers:[]})", "headers array");
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

    /* Response.json() parses bodyText; invalid JSON fails closed. */
    webapi_host_set_http(0, 200, "{\"n\":42,\"s\":\"hi\"}", "");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.json().n", "42");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.json().s", "\"hi\"");
    webapi_host_set_http(0, 200, "[1,2,3]", "");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.json()[1]", "2");
    webapi_host_set_http(0, 200, "not-json", "");
    eval_fails(rt, "var r=fetch('https://example.com/x'); r.json()", "invalid JSON");
    webapi_host_set_http(0, 200, "x", "");

    /*
     * Native Response.json userdata must keep the Response object alive:
     * extract the method, drop the Response, force GC, then call — no crash.
     */
    webapi_host_set_http(0, 200, "{\"n\":7}", "");
    eval_ok(rt, "var r=fetch('https://example.com/x'); var rj=r.json; r=null", "null");
    for (int i = 0; i < 8; i++)
        js_rt_gc(rt);
    eval_ok(rt, "rj().n", "7");
    webapi_host_set_http(0, 200, "x", "");

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

    /* Storage: localStorage VFS-backed; session in-memory; quota fail closed. */
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

    /* Tab clear isolates session; private tab never uses durable local. */
    webapi_clear_tab(0);
    eval_ok(rt, "localStorage.getItem('a')", "null");
    webapi_set_tab(0, 1);
    expect(webapi_install(rt, "https://example.com/page") == 0, "reinstall private");
    eval_ok(rt, "localStorage.setItem('priv','1'); sessionStorage.getItem('priv')", "\"1\"");
    webapi_set_tab(0, 0);
    webapi_clear_tab(0);
    webapi_host_vfs_reset();
    expect(webapi_install(rt, "https://example.com/page") == 0, "reinstall normal");
    eval_ok(rt, "localStorage.getItem('priv')", "null");

    /* localStorage survives runtime reinstall when origin matches (VFS persist). */
    webapi_host_vfs_reset();
    webapi_clear_tab(0);
    expect(webapi_install(rt, "https://example.com/page") == 0, "persist install");
    eval_ok(rt, "localStorage.setItem('persist','yes')", "undefined");
    js_rt_destroy(rt);
    rt = js_rt_create();
    expect(rt != NULL, "recreate runtime");
    webapi_set_tab(0, 0);
    webapi_clear_tab(0);
    expect(webapi_install(rt, "https://example.com/page") == 0, "persist reload");
    eval_ok(rt, "localStorage.getItem('persist')", "\"yes\"");
    eval_ok(rt, "sessionStorage.setItem('ephem','1'); sessionStorage.getItem('ephem')", "\"1\"");
    js_rt_destroy(rt);
    rt = js_rt_create();
    expect(rt != NULL, "recreate runtime 2");
    webapi_set_tab(0, 0);
    webapi_clear_tab(0);
    expect(webapi_install(rt, "https://example.com/page") == 0, "session reload");
    eval_ok(rt, "sessionStorage.getItem('ephem')", "null");

    /* Fail-closed isolation: enforce mode blocks fetch with explicit reason. */
    browser_isolation_set_enforce(1);
    eval_fails(rt, "fetch('https://example.com/x')", "ring-3 isolation enforce");
    browser_isolation_set_enforce(0);

    /* Bind switches page URL + store index for the calling tab. */
    webapi_set_runtime_binder(NULL);
    webapi_clear_tab(0);
    webapi_clear_tab(1);
    webapi_bind(0, 0, "https://a.example/page");
    expect(webapi_install(rt, "https://a.example/page") == 0, "bind tab0 install");
    eval_ok(rt, "sessionStorage.setItem('k','a'); sessionStorage.getItem('k')", "\"a\"");
    webapi_bind(1, 0, "https://b.example/page");
    expect(strcmp(g_web_page_url, "https://b.example/page") == 0, "bind updates page url");
    expect(g_web_tab_id == 1, "bind updates tab id");
    expect(webapi_install(rt, "https://b.example/page") == 0, "bind tab1 install");
    eval_ok(rt, "sessionStorage.getItem('k')", "null"); /* isolated store */
    eval_ok(rt, "sessionStorage.setItem('k','b'); sessionStorage.getItem('k')", "\"b\"");
    webapi_bind(0, 0, "https://a.example/page");
    eval_ok(rt, "sessionStorage.getItem('k')", "\"a\""); /* tab0 session preserved */

    /* Compact on close shifts higher tab stores down. */
    webapi_bind(1, 0, "https://b.example/page");
    eval_ok(rt, "sessionStorage.getItem('k')", "\"b\"");
    webapi_compact_tab(0); /* drop tab0; tab1 → slot 0 */
    expect(g_web_tab_id == 0, "compact decrements tab id");
    webapi_bind(0, 0, "https://b.example/page");
    eval_ok(rt, "sessionStorage.getItem('k')", "\"b\""); /* former tab1 data */
    webapi_clear_tab(0);
    webapi_clear_tab(1);

    /* Relative fetch uses rebound page URL (not a stale active-tab origin). */
    webapi_host_set_http(0, 200, "from-b", "");
    webapi_bind(1, 0, "https://b.example/dir/page");
    expect(webapi_install(rt, "https://b.example/dir/page") == 0, "rebind fetch origin");
    eval_ok(rt, "var r=fetch('../x'); r.bodyText", "\"from-b\"");
    webapi_bind(0, 0, "https://a.example/");
    webapi_host_set_http(0, 200, "from-a", "");
    expect(webapi_install(rt, "https://a.example/") == 0, "rebind fetch origin a");
    eval_ok(rt, "var r=fetch('/x'); r.bodyText", "\"from-a\"");

    js_rt_destroy(rt);
    if (fails) {
        fprintf(stderr, "%d webapi host test(s) failed\n", fails);
        return 1;
    }
    printf("test_webapi: ok\n");
    return 0;
}
