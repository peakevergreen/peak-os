/*
 * Host tests for Web API stubs: unsupported surfaces fail closed/clearly.
 */
#include "js.h"
#include "webapi.h"
#include "webapi_internal.h"

#include <stdio.h>
#include <string.h>

void webapi_host_set_http(int rc, int status, const char *body, const char *headers);

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

    /* AbortController must not exist — no silent fake shell. */
    eval_ok(rt, "typeof AbortController", "\"undefined\"");

    /* fetch: unsupported init fails closed with clear errors. */
    eval_fails(rt, "fetch()", "URL required");
    eval_fails(rt, "fetch('https://example.com/x',{method:'POST'})", "only GET");
    eval_fails(rt, "fetch('https://example.com/x',{method:'post'})", "only GET");
    eval_fails(rt, "fetch('https://example.com/x',{signal:{}})", "AbortSignal");
    eval_fails(rt, "fetch('https://example.com/x',{body:'x'})", "request body");
    eval_fails(rt, "fetch('https://example.com/x','bad')", "init must be object");

    /* fetch: GET path returns stub response shape (host-canned HTTP). */
    webapi_host_set_http(0, 200, "hello", "");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.ok", "true");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.status", "200");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.bodyText", "\"hello\"");
    eval_ok(rt, "var r=fetch('https://example.com/x',{method:'GET'}); r.ok", "true");
    eval_ok(rt, "var r=fetch('https://example.com/x',{}); r.ok", "true");

    /* Network / CORS failure → ok:false (not a silent success). */
    webapi_host_set_http(-1, 0, "", "");
    eval_ok(rt, "var r=fetch('https://example.com/x'); r.ok", "false");

    /* Storage: in-memory get/set; quota exhaustion fails closed. */
    eval_ok(rt, "localStorage.setItem('a','1'); localStorage.getItem('a')", "\"1\"");
    eval_ok(rt, "sessionStorage.setItem('b','2'); sessionStorage.getItem('b')", "\"2\"");
    eval_ok(rt, "localStorage.getItem('missing')", "null");
    eval_fails(rt, "localStorage.setItem('only')", "key and value");

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

    js_rt_destroy(rt);
    if (fails) {
        fprintf(stderr, "%d webapi host test(s) failed\n", fails);
        return 1;
    }
    printf("test_webapi: ok\n");
    return 0;
}
