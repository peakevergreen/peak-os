/*
 * Host tests for the Peak-authored JavaScript bytecode runtime.
 */
#include "js.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int eval_eq(struct js_runtime *rt, const char *src, const char *want) {
    char out[128];
    if (js_eval(rt, src, "<test>", out, sizeof(out)) != 0) {
        fprintf(stderr, "FAIL eval '%s': %s\n", src, js_last_error(rt));
        fails++;
        return 0;
    }
    if (strcmp(out, want) != 0) {
        fprintf(stderr, "FAIL '%s' => got '%s' want '%s'\n", src, out, want);
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

    eval_eq(rt, "1+2*3", "7");
    eval_eq(rt, "var x=10; x=x+5; x", "15");
    eval_eq(rt, "function add(a,b){return a+b;} add(2,3)", "5");
    eval_eq(rt, "var o={a:1,b:2}; o.a+o.b", "3");
    eval_eq(rt, "var a=[10,20,30]; a[1]", "20");
    eval_eq(rt, "var s='hi'; s", "\"hi\"");
    eval_eq(rt, "var x; if(1){x=3;}else{x=4;} x", "3");
    eval_eq(rt, "var i=0; while(i<3){i=i+1;} i", "3");
    eval_eq(rt, "var n=0; for(var i=0;i<4;i=i+1){n=n+i;} n", "6");
    eval_eq(rt, "typeof 1", "\"number\"");
    eval_eq(rt, "true && false", "false");
    eval_eq(rt, "null", "null");

    /* Modern-ish language surface */
    eval_eq(rt, "let a=2; const b=3; a+b", "5");
    eval_eq(rt, "var f=(x)=>x+1; f(4)", "5");
    eval_eq(rt, "var e; try{throw 1}catch(x){e=x;} e", "1");
    eval_eq(rt, "class C{} typeof C", "\"function\"");

    /* Budgets: runaway loop must fail */
    js_rt_set_budgets(rt, 1000, 256);
    char out[64];
    int rc = js_eval(rt, "var i=0; while(1){i=i+1;}", "<budget>", out, sizeof(out));
    expect(rc != 0, "instruction budget trips");
    js_rt_set_budgets(rt, JS_INS_BUDGET_DEFAULT, JS_HEAP_OBJS_DEFAULT);

    /* Object budget */
    js_rt_set_budgets(rt, JS_INS_BUDGET_DEFAULT, 8);
    rc = js_eval(rt, "var a=[]; for(var i=0;i<100;i=i+1){a[i]={};} 1", "<objs>", out,
                 sizeof(out));
    expect(rc != 0, "object budget trips");
    js_rt_set_budgets(rt, JS_INS_BUDGET_DEFAULT, JS_HEAP_OBJS_DEFAULT);

    js_rt_destroy(rt);
    if (fails) {
        fprintf(stderr, "%d js host test(s) failed\n", fails);
        return 1;
    }
    printf("test_js: ok\n");
    return 0;
}
