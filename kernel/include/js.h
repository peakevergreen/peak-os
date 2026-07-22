#ifndef PEAK_JS_H
#define PEAK_JS_H

#include "types.h"

/* Peak-authored stackless bytecode JavaScript runtime (no JIT). */

#define JS_ERR_MAX 192
#define JS_INS_BUDGET_DEFAULT 250000
#define JS_HEAP_OBJS_DEFAULT  4096

struct js_runtime;
struct js_value;

void js_runtime_init(void);

struct js_runtime *js_rt_create(void);
void js_rt_destroy(struct js_runtime *rt);
void js_rt_set_budgets(struct js_runtime *rt, uint32_t ins_budget, uint32_t max_objs);
void js_rt_reset(struct js_runtime *rt);

/* Evaluate source. Returns 0 on success. out_json may receive a short JSON-ish result. */
int js_eval(struct js_runtime *rt, const char *source, const char *filename,
            char *out_json, size_t out_cap);
const char *js_last_error(struct js_runtime *rt);

/* Host bindings registration (C functions callable from JS). */
typedef int (*js_native_fn)(struct js_runtime *rt, int argc, void *argv /* js_value* */,
                            void *ret /* js_value* */, void *userdata);
int js_rt_set_global_fn(struct js_runtime *rt, const char *name, js_native_fn fn,
                        void *userdata);

/* Value helpers for host bindings / DOM bridge. */
int js_val_is_undefined(const void *v);
int js_val_is_null(const void *v);
int js_val_is_bool(const void *v);
int js_val_is_number(const void *v);
int js_val_is_string(const void *v);
int js_val_is_object(const void *v);
int js_val_is_function(const void *v);
int js_val_to_bool(const void *v);
double js_val_to_number(const void *v);
const char *js_val_to_cstring(struct js_runtime *rt, const void *v, char *buf, size_t cap);
void js_val_set_undefined(void *v);
void js_val_set_null(void *v);
void js_val_set_bool(void *v, int b);
void js_val_set_number(void *v, double n);
int js_val_set_string(struct js_runtime *rt, void *v, const char *s);
int js_val_new_object(struct js_runtime *rt, void *v);
int js_val_new_array(struct js_runtime *rt, void *v);
int js_val_get_prop(struct js_runtime *rt, const void *obj, const char *key, void *out);
int js_val_set_prop(struct js_runtime *rt, void *obj, const char *key, const void *val);
int js_val_call(struct js_runtime *rt, void *fn, void *this_v, int argc, void *argv,
                void *ret);

/* Timers / microtasks for browser embedding. */
int js_set_timeout(struct js_runtime *rt, void *fn, uint32_t ms, int repeat);
void js_clear_timer(struct js_runtime *rt, int id);
void js_tick(struct js_runtime *rt); /* drain due timers + microtasks */
int js_pending_work(struct js_runtime *rt);

/* Metrics */
void js_rt_stats(struct js_runtime *rt, uint32_t *objs, uint32_t *ins_used,
                 uint32_t *timers, uint32_t *gc_runs);

/* Opaque value size for stack allocation in host code. */
#define JS_VALUE_BYTES 24

#endif
