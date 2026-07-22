#include "js_internal.h"
#include "timer.h"

static int g_inited;

void js_runtime_init(void) {
    g_inited = 1;
}

const char *js_last_error(struct js_runtime *rt) {
    return rt && rt->err[0] ? rt->err : "no error";
}

struct js_string *js_str_new(struct js_runtime *rt, const char *s, size_t len) {
    if (!rt)
        return NULL;
    if (rt->heap_str_count >= rt->heap_str_cap) {
        uint32_t nc = rt->heap_str_cap ? rt->heap_str_cap * 2 : 64;
        struct js_string **n = krealloc(rt->strs, nc * sizeof(*n));
        if (!n)
            return NULL;
        rt->strs = n;
        rt->heap_str_cap = nc;
    }
    struct js_string *st = kzalloc(sizeof(*st));
    if (!st)
        return NULL;
    st->data = kmalloc(len + 1);
    if (!st->data) {
        kfree(st);
        return NULL;
    }
    if (len && s)
        memcpy(st->data, s, len);
    st->data[len] = '\0';
    st->len = (uint32_t)len;
    rt->strs[rt->heap_str_count++] = st;
    return st;
}

struct js_object *js_obj_new(struct js_runtime *rt, int is_array) {
    if (!rt)
        return NULL;
    if (rt->obj_count >= rt->max_objs) {
        snprintf(rt->err, sizeof(rt->err), "object budget exceeded");
        rt->aborted = 1;
        return NULL;
    }
    if (rt->obj_count >= rt->obj_cap) {
        uint32_t nc = rt->obj_cap ? rt->obj_cap * 2 : 128;
        if (nc > rt->max_objs)
            nc = rt->max_objs;
        struct js_object **n = krealloc(rt->objs, nc * sizeof(*n));
        if (!n)
            return NULL;
        rt->objs = n;
        rt->obj_cap = nc;
    }
    struct js_object *o = kzalloc(sizeof(*o));
    if (!o)
        return NULL;
    o->is_array = is_array;
    rt->objs[rt->obj_count++] = o;
    return o;
}

int js_obj_set(struct js_runtime *rt, struct js_object *o, const char *key,
               struct js_value v) {
    (void)rt;
    if (!o || !key)
        return -1;
    for (struct js_prop *p = o->props; p; p = p->next) {
        if (!strcmp(p->key, key)) {
            p->val = v;
            return 0;
        }
    }
    struct js_prop *p = kzalloc(sizeof(*p));
    if (!p)
        return -1;
    size_t kl = strlen(key);
    p->key = kmalloc(kl + 1);
    if (!p->key) {
        kfree(p);
        return -1;
    }
    memcpy(p->key, key, kl + 1);
    p->val = v;
    p->next = o->props;
    o->props = p;
    if (o->is_array && key[0] >= '0' && key[0] <= '9') {
        uint32_t idx = 0;
        for (const char *c = key; *c >= '0' && *c <= '9'; c++)
            idx = idx * 10 + (uint32_t)(*c - '0');
        if (idx + 1 > o->arr_len)
            o->arr_len = idx + 1;
    }
    return 0;
}

int js_obj_get(struct js_runtime *rt, struct js_object *o, const char *key,
               struct js_value *out) {
    (void)rt;
    if (!o || !key || !out)
        return -1;
    if (o->is_array && !strcmp(key, "length")) {
        *out = js_num((double)o->arr_len);
        return 0;
    }
    for (struct js_prop *p = o->props; p; p = p->next) {
        if (!strcmp(p->key, key)) {
            *out = p->val;
            return 0;
        }
    }
    if (o->proto)
        return js_obj_get(rt, o->proto, key, out);
    *out = js_undef();
    return -1;
}

static void mark_val(struct js_value v);
static void mark_obj(struct js_object *o) {
    if (!o || o->marked)
        return;
    o->marked = 1;
    for (struct js_prop *p = o->props; p; p = p->next)
        mark_val(p->val);
    if (o->proto)
        mark_obj(o->proto);
    if (o->closure_env)
        mark_obj(o->closure_env);
}

static void mark_val(struct js_value v) {
    if (v.type == JT_STR && v.u.s)
        v.u.s->marked = 1;
    else if ((v.type == JT_OBJ || v.type == JT_ARR || v.type == JT_FUNC ||
              v.type == JT_NATIVE || v.type == JT_DOM) &&
             v.u.o)
        mark_obj(v.u.o);
}

void js_gc(struct js_runtime *rt) {
    if (!rt)
        return;
    for (uint32_t i = 0; i < rt->obj_count; i++)
        if (rt->objs[i])
            rt->objs[i]->marked = 0;
    for (uint32_t i = 0; i < rt->heap_str_count; i++)
        if (rt->strs[i])
            rt->strs[i]->marked = 0;
    if (rt->global)
        mark_obj(rt->global);
    for (uint32_t i = 0; i < rt->sp; i++)
        mark_val(rt->stack[i]);
    for (int i = 0; i < rt->fp; i++) {
        mark_val(rt->frames[i].this_v);
        if (rt->frames[i].func)
            mark_obj(rt->frames[i].func);
        if (rt->frames[i].env)
            mark_obj(rt->frames[i].env);
    }
    for (int i = 0; i < JS_TIMER_MAX; i++)
        if (rt->timers[i].used)
            mark_val(rt->timers[i].fn);
    for (int i = 0; i < rt->micro_n; i++)
        mark_val(rt->micro[i]);

    uint32_t w = 0;
    for (uint32_t i = 0; i < rt->obj_count; i++) {
        struct js_object *o = rt->objs[i];
        if (!o)
            continue;
        if (!o->marked) {
            struct js_prop *p = o->props;
            while (p) {
                struct js_prop *n = p->next;
                kfree(p->key);
                kfree(p);
                p = n;
            }
            kfree(o);
        } else {
            rt->objs[w++] = o;
        }
    }
    rt->obj_count = w;

    w = 0;
    for (uint32_t i = 0; i < rt->heap_str_count; i++) {
        struct js_string *s = rt->strs[i];
        if (!s)
            continue;
        if (!s->marked) {
            kfree(s->data);
            kfree(s);
        } else {
            rt->strs[w++] = s;
        }
    }
    rt->heap_str_count = w;
    rt->gc_runs++;
}

struct js_runtime *js_rt_create(void) {
    if (!g_inited)
        js_runtime_init();
    struct js_runtime *rt = kzalloc(sizeof(*rt));
    if (!rt)
        return NULL;
    rt->code_cap = 4096;
    rt->code = kzalloc(rt->code_cap);
    rt->str_cap = 128;
    rt->strtab = kzalloc(rt->str_cap * sizeof(char *));
    rt->obj_cap = 128;
    rt->objs = kzalloc(rt->obj_cap * sizeof(struct js_object *));
    rt->heap_str_cap = 64;
    rt->strs = kzalloc(rt->heap_str_cap * sizeof(struct js_string *));
    rt->ins_budget = JS_INS_BUDGET_DEFAULT;
    rt->max_objs = JS_HEAP_OBJS_DEFAULT;
    if (!rt->code || !rt->strtab || !rt->objs || !rt->strs) {
        js_rt_destroy(rt);
        return NULL;
    }
    rt->global = js_obj_new(rt, 0);
    if (!rt->global) {
        js_rt_destroy(rt);
        return NULL;
    }
    js_install_builtins(rt);
    return rt;
}

void js_rt_destroy(struct js_runtime *rt) {
    if (!rt)
        return;
    if (rt->global) {
        /* Force collect everything by clearing roots */
        rt->global = NULL;
        rt->sp = 0;
        rt->fp = 0;
        rt->micro_n = 0;
        memset(rt->timers, 0, sizeof(rt->timers));
        js_gc(rt);
    }
    for (uint32_t i = 0; i < rt->str_count; i++)
        kfree(rt->strtab[i]);
    kfree(rt->strtab);
    kfree(rt->code);
    kfree(rt->objs);
    kfree(rt->strs);
    kfree(rt);
}

/* ins_budget: max VM dispatch steps per js_eval/js_vm_run (0 = keep current).
 * max_objs: hard cap on live heap objects tracked by js_obj_new (0 = keep current). */
void js_rt_set_budgets(struct js_runtime *rt, uint32_t ins_budget, uint32_t max_objs) {
    if (!rt)
        return;
    if (ins_budget)
        rt->ins_budget = ins_budget;
    if (max_objs)
        rt->max_objs = max_objs;
}

void js_rt_reset(struct js_runtime *rt) {
    if (!rt)
        return;
    rt->sp = 0;
    rt->fp = 0;
    rt->code_len = 0;
    rt->ins_used = 0;
    rt->aborted = 0;
    rt->err[0] = '\0';
    rt->micro_n = 0;
    for (uint32_t i = 0; i < rt->str_count; i++)
        kfree(rt->strtab[i]);
    rt->str_count = 0;
    memset(rt->timers, 0, sizeof(rt->timers));
    /* Keep global object; clear user props by recreating. */
    rt->global = NULL;
    js_gc(rt);
    rt->global = js_obj_new(rt, 0);
    if (rt->global)
        js_install_builtins(rt);
}

int js_eval(struct js_runtime *rt, const char *source, const char *filename,
            char *out_json, size_t out_cap) {
    if (!rt || !source)
        return -1;
    rt->err[0] = '\0';
    rt->ins_used = 0;
    rt->aborted = 0;
    rt->sp = 0;
    rt->fp = 0;
    rt->code_len = 0;
    for (uint32_t i = 0; i < rt->str_count; i++)
        kfree(rt->strtab[i]);
    rt->str_count = 0;

    if (js_compile(rt, source, filename ? filename : "<eval>") != 0)
        return -1;
    if (js_vm_run(rt, 0) != 0)
        return -1;
    if (out_json && out_cap) {
        if (rt->sp > 0) {
            struct js_value v = rt->stack[rt->sp - 1];
            if (v.type == JT_NUM) {
                /* simple integer-ish print */
                long long iv = (long long)v.u.n;
                if ((double)iv == v.u.n)
                    snprintf(out_json, out_cap, "%lld", iv);
                else
                    snprintf(out_json, out_cap, "%d", (int)v.u.n);
            } else if (v.type == JT_BOOL) {
                snprintf(out_json, out_cap, "%s", v.u.b ? "true" : "false");
            } else if (v.type == JT_STR && v.u.s) {
                snprintf(out_json, out_cap, "\"%s\"", v.u.s->data);
            } else if (v.type == JT_NULL) {
                snprintf(out_json, out_cap, "null");
            } else if (v.type == JT_UNDEF) {
                snprintf(out_json, out_cap, "undefined");
            } else {
                snprintf(out_json, out_cap, "[object]");
            }
        } else {
            snprintf(out_json, out_cap, "undefined");
        }
    }
    return 0;
}

int js_rt_set_global_fn(struct js_runtime *rt, const char *name, js_native_fn fn,
                        void *userdata) {
    if (!rt || !name || !fn || !rt->global)
        return -1;
    struct js_object *o = js_obj_new(rt, 0);
    if (!o)
        return -1;
    o->is_native = 1;
    o->is_func = 1;
    o->native = fn;
    o->userdata = userdata;
    struct js_value v;
    v.type = JT_NATIVE;
    v.u.o = o;
    return js_obj_set(rt, rt->global, name, v);
}

int js_set_timeout(struct js_runtime *rt, void *fn, uint32_t ms, int repeat) {
    if (!rt || !fn)
        return -1;
    for (int i = 0; i < JS_TIMER_MAX; i++) {
        if (!rt->timers[i].used) {
            rt->timers[i].used = 1;
            rt->timers[i].repeat = repeat;
            rt->timers[i].fn = *(struct js_value *)fn;
            uint32_t ticks = ms / 10;
            if (!ticks)
                ticks = 1;
            rt->timers[i].interval_ticks = ticks;
            rt->timers[i].due_tick = timer_ticks() + ticks;
            return i + 1;
        }
    }
    return -1;
}

void js_clear_timer(struct js_runtime *rt, int id) {
    if (!rt || id < 1 || id > JS_TIMER_MAX)
        return;
    rt->timers[id - 1].used = 0;
}

void js_tick(struct js_runtime *rt) {
    if (!rt)
        return;
    uint64_t now = timer_ticks();
    for (int i = 0; i < JS_TIMER_MAX; i++) {
        if (!rt->timers[i].used)
            continue;
        if (now < rt->timers[i].due_tick)
            continue;
        struct js_value fn = rt->timers[i].fn;
        if (fn.type == JT_FUNC || fn.type == JT_NATIVE) {
            struct js_value ret;
            js_val_call(rt, &fn, NULL, 0, NULL, &ret);
        }
        if (rt->timers[i].repeat)
            rt->timers[i].due_tick = now + rt->timers[i].interval_ticks;
        else
            rt->timers[i].used = 0;
    }
    /* microtasks */
    while (rt->micro_n > 0) {
        struct js_value fn = rt->micro[--rt->micro_n];
        if (fn.type == JT_FUNC || fn.type == JT_NATIVE) {
            struct js_value ret;
            js_val_call(rt, &fn, NULL, 0, NULL, &ret);
        }
    }
}

int js_pending_work(struct js_runtime *rt) {
    if (!rt)
        return 0;
    if (rt->micro_n)
        return 1;
    for (int i = 0; i < JS_TIMER_MAX; i++)
        if (rt->timers[i].used)
            return 1;
    return 0;
}

void js_rt_stats(struct js_runtime *rt, uint32_t *objs, uint32_t *ins_used,
                 uint32_t *timers, uint32_t *gc_runs) {
    if (!rt)
        return;
    if (objs)
        *objs = rt->obj_count;
    if (ins_used)
        *ins_used = rt->ins_used;
    if (gc_runs)
        *gc_runs = rt->gc_runs;
    if (timers) {
        uint32_t n = 0;
        for (int i = 0; i < JS_TIMER_MAX; i++)
            if (rt->timers[i].used)
                n++;
        *timers = n;
    }
}

/* ---- public value helpers ---- */
int js_val_is_undefined(const void *v) {
    return v && ((const struct js_value *)v)->type == JT_UNDEF;
}
int js_val_is_null(const void *v) {
    return v && ((const struct js_value *)v)->type == JT_NULL;
}
int js_val_is_bool(const void *v) {
    return v && ((const struct js_value *)v)->type == JT_BOOL;
}
int js_val_is_number(const void *v) {
    return v && ((const struct js_value *)v)->type == JT_NUM;
}
int js_val_is_string(const void *v) {
    return v && ((const struct js_value *)v)->type == JT_STR;
}
int js_val_is_object(const void *v) {
    const struct js_value *x = v;
    return x && (x->type == JT_OBJ || x->type == JT_ARR || x->type == JT_DOM);
}
int js_val_is_function(const void *v) {
    const struct js_value *x = v;
    return x && (x->type == JT_FUNC || x->type == JT_NATIVE);
}
int js_val_to_bool(const void *v) {
    const struct js_value *x = v;
    if (!x)
        return 0;
    switch (x->type) {
    case JT_BOOL: return x->u.b;
    case JT_NUM: return x->u.n != 0;
    case JT_STR: return x->u.s && x->u.s->len > 0;
    case JT_NULL:
    case JT_UNDEF: return 0;
    default: return 1;
    }
}
double js_val_to_number(const void *v) {
    const struct js_value *x = v;
    if (!x)
        return 0;
    if (x->type == JT_NUM)
        return x->u.n;
    if (x->type == JT_BOOL)
        return x->u.b ? 1 : 0;
    if (x->type == JT_STR && x->u.s) {
        double n = 0;
        int neg = 0;
        const char *p = x->u.s->data;
        if (*p == '-') {
            neg = 1;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            p++;
        }
        if (*p == '.') {
            p++;
            double frac = 0.1;
            while (*p >= '0' && *p <= '9') {
                n += (*p - '0') * frac;
                frac *= 0.1;
                p++;
            }
        }
        return neg ? -n : n;
    }
    return 0;
}
const char *js_val_to_cstring(struct js_runtime *rt, const void *v, char *buf,
                              size_t cap) {
    (void)rt;
    const struct js_value *x = v;
    if (!buf || !cap)
        return "";
    buf[0] = '\0';
    if (!x)
        return buf;
    if (x->type == JT_STR && x->u.s) {
        snprintf(buf, cap, "%s", x->u.s->data);
        return buf;
    }
    if (x->type == JT_NUM) {
        long long iv = (long long)x->u.n;
        if ((double)iv == x->u.n)
            snprintf(buf, cap, "%lld", iv);
        else
            snprintf(buf, cap, "%d", (int)x->u.n);
        return buf;
    }
    if (x->type == JT_BOOL) {
        snprintf(buf, cap, "%s", x->u.b ? "true" : "false");
        return buf;
    }
    if (x->type == JT_NULL) {
        snprintf(buf, cap, "null");
        return buf;
    }
    if (x->type == JT_UNDEF) {
        snprintf(buf, cap, "undefined");
        return buf;
    }
    snprintf(buf, cap, "[object Object]");
    return buf;
}
void js_val_set_undefined(void *v) {
    if (v)
        *(struct js_value *)v = js_undef();
}
void js_val_set_null(void *v) {
    if (v)
        *(struct js_value *)v = js_null();
}
void js_val_set_bool(void *v, int b) {
    if (v)
        *(struct js_value *)v = js_bool(b);
}
void js_val_set_number(void *v, double n) {
    if (v)
        *(struct js_value *)v = js_num(n);
}
int js_val_set_string(struct js_runtime *rt, void *v, const char *s) {
    if (!rt || !v)
        return -1;
    struct js_string *st = js_str_new(rt, s ? s : "", s ? strlen(s) : 0);
    if (!st)
        return -1;
    struct js_value x;
    x.type = JT_STR;
    x.u.s = st;
    *(struct js_value *)v = x;
    return 0;
}
int js_val_new_object(struct js_runtime *rt, void *v) {
    struct js_object *o = js_obj_new(rt, 0);
    if (!o || !v)
        return -1;
    struct js_value x;
    x.type = JT_OBJ;
    x.u.o = o;
    *(struct js_value *)v = x;
    return 0;
}
int js_val_new_array(struct js_runtime *rt, void *v) {
    struct js_object *o = js_obj_new(rt, 1);
    if (!o || !v)
        return -1;
    struct js_value x;
    x.type = JT_ARR;
    x.u.o = o;
    *(struct js_value *)v = x;
    return 0;
}
int js_val_get_prop(struct js_runtime *rt, const void *obj, const char *key,
                    void *out) {
    const struct js_value *o = obj;
    if (!o || !out || (o->type != JT_OBJ && o->type != JT_ARR && o->type != JT_FUNC &&
                       o->type != JT_DOM && o->type != JT_NATIVE))
        return -1;
    return js_obj_get(rt, o->u.o, key, (struct js_value *)out);
}
int js_val_set_prop(struct js_runtime *rt, void *obj, const char *key,
                    const void *val) {
    struct js_value *o = obj;
    if (!o || !val || (o->type != JT_OBJ && o->type != JT_ARR && o->type != JT_DOM))
        return -1;
    return js_obj_set(rt, o->u.o, key, *(const struct js_value *)val);
}
int js_val_call(struct js_runtime *rt, void *fn, void *this_v, int argc, void *argv,
                void *ret) {
    if (!rt || !fn)
        return -1;
    struct js_value *f = fn;
    if (f->type == JT_NATIVE && f->u.o && f->u.o->native) {
        struct js_value r = js_undef();
        int rc = f->u.o->native(rt, argc, argv, &r, f->u.o->userdata);
        if (ret)
            *(struct js_value *)ret = r;
        return rc;
    }
    if (f->type != JT_FUNC || !f->u.o)
        return -1;
    if (rt->fp >= JS_FRAME_MAX || rt->sp + f->u.o->local_count >= JS_STACK_MAX)
        return -1;
    struct js_frame *fr = &rt->frames[rt->fp++];
    memset(fr, 0, sizeof(*fr));
    fr->func = f->u.o;
    fr->ip = f->u.o->code_off;
    fr->local_count = f->u.o->local_count;
    fr->catch_ip = -1;
    fr->this_v = this_v ? *(struct js_value *)this_v : js_undef();
    fr->env = js_obj_new(rt, 0);
    if (!fr->env) {
        rt->fp--;
        return -1;
    }
    fr->env->closure_env = f->u.o->closure_env;
    fr->bp = rt->sp;
    for (uint16_t i = 0; i < fr->local_count; i++) {
        struct js_value a = js_undef();
        if (i < argc && argv)
            a = ((struct js_value *)argv)[i];
        rt->stack[rt->sp++] = a;
    }
    int rc = js_vm_run(rt, (uint32_t)-1);
    if (ret) {
        if (rt->sp > 0)
            *(struct js_value *)ret = rt->stack[--rt->sp];
        else
            *(struct js_value *)ret = js_undef();
    } else if (rt->sp > 0) {
        rt->sp--;
    }
    return rc;
}
