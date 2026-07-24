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

static int eval_fails(struct js_runtime *rt, const char *src, const char *err_sub) {
    char out[64];
    int rc = js_eval(rt, src, "<test>", out, sizeof(out));
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

    /* async/await: unwrap Promise.resolve; async functions return promises.
     * for-await still fail-closed. ES modules via js_eval_module + import. */
    eval_eq(rt, "await 7", "7");
    eval_eq(rt, "await Promise.resolve(42)", "42");
    eval_eq(rt, "async function f(){return 3;} await f()", "3");
    eval_eq(rt, "var f=async ()=>5; await f()", "5");
    eval_fails(rt, "for await(var x of []){}", "await unsupported");
    {
        char mout[64];
        expect(js_eval_module(rt, "export var x=41;", "m1", mout, sizeof(mout)) == 0,
               "eval module");
        eval_eq(rt, "import {x} from \"m1\"; x+1", "42");
    }

    /* Hot-path regressions: string reuse, INC/DEC/ADD_LOCAL, LT_LOCAL_NUM, lazy call env */
    eval_eq(rt, "var t='ab'+'cd'; t", "\"abcd\"");
    eval_eq(rt, "typeof typeof 1", "\"string\"");
    eval_eq(rt,
            "function loop(){var n=0; for(var i=0;i<20;i=i+1){n=n+i;} return n;} loop()",
            "190");
    {
        uint32_t objs = 0, ins = 0, timers = 0, gc = 0;
        char out[64];
        /* Repeated string literal must not grow heap_str unboundedly (str_imm reuse). */
        expect(js_eval(rt, "var s='hi'; s+s+s+s+s", "<str>", out, sizeof(out)) == 0,
               "str reuse eval");
        expect(strcmp(out, "\"hihihihihi\"") == 0, "str reuse result");
        js_rt_stats(rt, &objs, &ins, &timers, &gc);
        expect(ins > 0 && ins < 64, "str reuse modest ins");
        /* Function-local i=i+1 uses INC_LOCAL — fewer dispatches than GET/PUSH/ADD/SET. */
        expect(js_eval(rt,
                       "function f(){var i=0; while(i<50){i=i+1;} return i;} f()",
                       "<inc>", out, sizeof(out)) == 0,
               "inc_local eval");
        expect(strcmp(out, "50") == 0, "inc_local result");
        js_rt_stats(rt, &objs, &ins, &timers, &gc);
        /* 50 iterations: LT_LOCAL_NUM + INC_LOCAL keep dispatch well under prior bound. */
        expect(ins > 50 && ins < 500, "inc_local dispatch bound");
        /* DEC_LOCAL: i = i - 1 */
        expect(js_eval(rt,
                       "function d(){var i=40; while(i>0){i=i-1;} return i;} d()",
                       "<dec>", out, sizeof(out)) == 0,
               "dec_local eval");
        expect(strcmp(out, "0") == 0, "dec_local result");
        js_rt_stats(rt, &objs, &ins, &timers, &gc);
        expect(ins > 40 && ins < 500, "dec_local dispatch bound");
        /* ADD_LOCAL + LT_LOCAL_NUM: n = n + i with local < const */
        expect(js_eval(rt,
                       "function acc(){var n=0; for(var i=0;i<30;i=i+1){n=n+i;} return n;} acc()",
                       "<add>", out, sizeof(out)) == 0,
               "add_local eval");
        expect(strcmp(out, "435") == 0, "add_local result");
        js_rt_stats(rt, &objs, &ins, &timers, &gc);
        expect(ins > 30 && ins < 700, "add_local dispatch bound");
        /* Stay within default instruction/object budgets on a denser loop. */
        expect(js_eval(rt,
                       "function hot(){var n=0; for(var i=0;i<200;i=i+1){n=n+i;} return n;} hot()",
                       "<hot>", out, sizeof(out)) == 0,
               "hot loop within budget");
        expect(strcmp(out, "19900") == 0, "hot loop result");
        js_rt_stats(rt, &objs, &ins, &timers, &gc);
        expect(ins < 8000 && objs < 64, "hot loop budget sanity");
    }

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
