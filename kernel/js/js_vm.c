#include "js_internal.h"
#include "console.h"
#include "serial.h"

static int truthy(struct js_value v) {
    return js_val_to_bool(&v);
}

static const char *strtab(struct js_runtime *rt, uint16_t id) {
    if (id >= rt->str_count)
        return "";
    return rt->strtab[id];
}

static int push(struct js_runtime *rt, struct js_value v) {
    if (rt->sp >= JS_STACK_MAX) {
        snprintf(rt->err, sizeof(rt->err), "stack overflow");
        return -1;
    }
    rt->stack[rt->sp++] = v;
    return 0;
}
static int pop(struct js_runtime *rt, struct js_value *out) {
    if (rt->sp == 0) {
        snprintf(rt->err, sizeof(rt->err), "stack underflow");
        return -1;
    }
    if (out)
        *out = rt->stack[--rt->sp];
    else
        rt->sp--;
    return 0;
}

static uint8_t rd8(struct js_runtime *rt, uint32_t *ip) {
    return rt->code[(*ip)++];
}
static uint16_t rd16(struct js_runtime *rt, uint32_t *ip) {
    uint16_t v = (uint16_t)rt->code[*ip] | ((uint16_t)rt->code[*ip + 1] << 8);
    *ip += 2;
    return v;
}
static int16_t rdi16(struct js_runtime *rt, uint32_t *ip) {
    return (int16_t)rd16(rt, ip);
}
static double rdf64(struct js_runtime *rt, uint32_t *ip) {
    union { double d; uint8_t b[8]; } u;
    for (int i = 0; i < 8; i++)
        u.b[i] = rt->code[(*ip)++];
    return u.d;
}

static struct js_value to_str_val(struct js_runtime *rt, struct js_value v) {
    char buf[128];
    js_val_to_cstring(rt, &v, buf, sizeof(buf));
    struct js_string *s = js_str_new(rt, buf, strlen(buf));
    struct js_value r;
    r.type = JT_STR;
    r.u.s = s;
    return r;
}

static int do_call(struct js_runtime *rt, int argc) {
    if (rt->sp < (uint32_t)argc + 1) {
        snprintf(rt->err, sizeof(rt->err), "call stack");
        return -1;
    }
    struct js_value *args = &rt->stack[rt->sp - argc];
    struct js_value callee = rt->stack[rt->sp - argc - 1];
    if (callee.type == JT_NATIVE && callee.u.o && callee.u.o->native) {
        struct js_value ret = js_undef();
        int rc = callee.u.o->native(rt, argc, args, &ret, callee.u.o->userdata);
        rt->sp -= (uint32_t)argc + 1;
        if (rc != 0)
            return -1;
        return push(rt, ret);
    }
    if (callee.type != JT_FUNC || !callee.u.o) {
        snprintf(rt->err, sizeof(rt->err), "not a function");
        return -1;
    }
    if (rt->fp >= JS_FRAME_MAX) {
        snprintf(rt->err, sizeof(rt->err), "too many frames");
        return -1;
    }
    struct js_object *fn = callee.u.o;
    struct js_frame *fr = &rt->frames[rt->fp++];
    memset(fr, 0, sizeof(*fr));
    fr->func = fn;
    fr->ip = fn->code_off;
    fr->catch_ip = -1;
    fr->this_v = js_undef();
    fr->env = js_obj_new(rt, 0);
    if (!fr->env)
        return -1;
    fr->env->closure_env = fn->closure_env;
    fr->local_count = fn->local_count;
    fr->bp = rt->sp; /* after removing callee+args we'll place locals */
    /* remove callee+args from stack, then push locals */
    struct js_value argcopy[8];
    int ncopy = argc < 8 ? argc : 8;
    for (int i = 0; i < ncopy; i++)
        argcopy[i] = args[i];
    rt->sp -= (uint32_t)argc + 1;
    fr->bp = rt->sp;
    for (uint16_t i = 0; i < fr->local_count; i++) {
        if (push(rt, i < ncopy ? argcopy[i] : js_undef()))
            return -1;
    }
    return 0;
}

int js_vm_run(struct js_runtime *rt, uint32_t entry_ip) {
    int stop_at;
    if (entry_ip != (uint32_t)-1) {
        stop_at = rt->fp;
        if (rt->fp >= JS_FRAME_MAX)
            return -1;
        struct js_frame *fr = &rt->frames[rt->fp++];
        memset(fr, 0, sizeof(*fr));
        fr->ip = entry_ip;
        fr->bp = rt->sp;
        fr->catch_ip = -1;
        fr->this_v = js_undef();
        fr->local_count = 0;
    } else {
        /* Run until the current top frame returns (nested call). */
        if (rt->fp <= 0)
            return -1;
        stop_at = rt->fp - 1;
    }

    while (rt->fp > stop_at) {
        if (rt->ins_used++ >= rt->ins_budget) {
            snprintf(rt->err, sizeof(rt->err), "instruction budget exceeded");
            rt->aborted = 1;
            return -1;
        }
        if ((rt->ins_used & 0x3FFF) == 0 && rt->obj_count > rt->max_objs * 3 / 4)
            js_gc(rt);

        struct js_frame *fr = &rt->frames[rt->fp - 1];
        if (fr->ip >= rt->code_len) {
            snprintf(rt->err, sizeof(rt->err), "ip out of range");
            return -1;
        }
        uint8_t op = rd8(rt, &fr->ip);
        switch (op) {
        case OP_NOP: break;
        case OP_PUSH_UNDEF: if (push(rt, js_undef())) return -1; break;
        case OP_PUSH_NULL: if (push(rt, js_null())) return -1; break;
        case OP_PUSH_TRUE: if (push(rt, js_bool(1))) return -1; break;
        case OP_PUSH_FALSE: if (push(rt, js_bool(0))) return -1; break;
        case OP_PUSH_BOOL: {
            uint8_t b = rd8(rt, &fr->ip);
            if (push(rt, js_bool(b)))
                return -1;
            break;
        }
        case OP_PUSH_NUM: {
            double n = rdf64(rt, &fr->ip);
            if (push(rt, js_num(n)))
                return -1;
            break;
        }
        case OP_PUSH_STR: {
            uint16_t id = rd16(rt, &fr->ip);
            const char *s = strtab(rt, id);
            struct js_string *st = js_str_new(rt, s, strlen(s));
            struct js_value v;
            v.type = JT_STR;
            v.u.s = st;
            if (push(rt, v))
                return -1;
            break;
        }
        case OP_POP: if (pop(rt, NULL)) return -1; break;
        case OP_DUP: {
            if (rt->sp == 0)
                return -1;
            if (push(rt, rt->stack[rt->sp - 1]))
                return -1;
            break;
        }
        case OP_GET_GLOBAL: {
            uint16_t id = rd16(rt, &fr->ip);
            struct js_value v;
            js_obj_get(rt, rt->global, strtab(rt, id), &v);
            if (push(rt, v))
                return -1;
            break;
        }
        case OP_SET_GLOBAL: {
            uint16_t id = rd16(rt, &fr->ip);
            if (rt->sp == 0)
                return -1;
            js_obj_set(rt, rt->global, strtab(rt, id), rt->stack[rt->sp - 1]);
            break;
        }
        case OP_GET_LOCAL: {
            uint16_t i = rd16(rt, &fr->ip);
            if (fr->bp + i >= rt->sp)
                return -1;
            if (push(rt, rt->stack[fr->bp + i]))
                return -1;
            break;
        }
        case OP_SET_LOCAL: {
            uint16_t i = rd16(rt, &fr->ip);
            if (rt->sp == 0 || fr->bp + i >= rt->sp)
                return -1;
            rt->stack[fr->bp + i] = rt->stack[rt->sp - 1];
            break;
        }
        case OP_GET_PROP: {
            uint16_t id = rd16(rt, &fr->ip);
            struct js_value obj;
            if (pop(rt, &obj))
                return -1;
            struct js_value v = js_undef();
            if (obj.type == JT_OBJ || obj.type == JT_ARR || obj.type == JT_FUNC ||
                obj.type == JT_DOM || obj.type == JT_NATIVE)
                js_obj_get(rt, obj.u.o, strtab(rt, id), &v);
            else if (obj.type == JT_STR && obj.u.s && !strcmp(strtab(rt, id), "length"))
                v = js_num((double)obj.u.s->len);
            if (push(rt, v))
                return -1;
            break;
        }
        case OP_SET_PROP: {
            uint16_t id = rd16(rt, &fr->ip);
            struct js_value val;
            if (pop(rt, &val))
                return -1;
            if (rt->sp == 0)
                return -1;
            struct js_value obj = rt->stack[rt->sp - 1];
            if (obj.type == JT_OBJ || obj.type == JT_ARR || obj.type == JT_DOM)
                js_obj_set(rt, obj.u.o, strtab(rt, id), val);
            break;
        }
        case OP_GET_IDX: {
            struct js_value idx, obj;
            if (pop(rt, &idx) || pop(rt, &obj))
                return -1;
            char key[64];
            js_val_to_cstring(rt, &idx, key, sizeof(key));
            struct js_value v = js_undef();
            if (obj.type == JT_ARR || obj.type == JT_OBJ)
                js_obj_get(rt, obj.u.o, key, &v);
            else if (obj.type == JT_STR && obj.u.s && idx.type == JT_NUM) {
                int i = (int)idx.u.n;
                if (i >= 0 && (uint32_t)i < obj.u.s->len) {
                    char ch[2] = { obj.u.s->data[i], 0 };
                    struct js_string *s = js_str_new(rt, ch, 1);
                    v.type = JT_STR;
                    v.u.s = s;
                }
            }
            if (push(rt, v))
                return -1;
            break;
        }
        case OP_SET_IDX: {
            struct js_value val, idx, obj;
            if (pop(rt, &val) || pop(rt, &idx) || pop(rt, &obj))
                return -1;
            char key[64];
            js_val_to_cstring(rt, &idx, key, sizeof(key));
            if (obj.type == JT_ARR || obj.type == JT_OBJ)
                js_obj_set(rt, obj.u.o, key, val);
            if (push(rt, val))
                return -1;
            break;
        }
        case OP_ADD: {
            struct js_value b, a;
            if (pop(rt, &b) || pop(rt, &a))
                return -1;
            if (a.type == JT_STR || b.type == JT_STR) {
                char ab[256], bb[128];
                js_val_to_cstring(rt, &a, ab, sizeof(ab));
                js_val_to_cstring(rt, &b, bb, sizeof(bb));
                size_t n = strlen(ab);
                snprintf(ab + n, sizeof(ab) - n, "%s", bb);
                struct js_string *s = js_str_new(rt, ab, strlen(ab));
                struct js_value r;
                r.type = JT_STR;
                r.u.s = s;
                if (push(rt, r))
                    return -1;
            } else if (push(rt, js_num(js_val_to_number(&a) + js_val_to_number(&b))))
                return -1;
            break;
        }
        case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: {
            struct js_value b, a;
            if (pop(rt, &b) || pop(rt, &a))
                return -1;
            double x = js_val_to_number(&a), y = js_val_to_number(&b), r = 0;
            if (op == OP_SUB) r = x - y;
            else if (op == OP_MUL) r = x * y;
            else if (op == OP_DIV) r = y != 0 ? x / y : 0;
            else r = y != 0 ? (double)((long long)x % (long long)y) : 0;
            if (push(rt, js_num(r)))
                return -1;
            break;
        }
        case OP_NEG: {
            struct js_value a;
            if (pop(rt, &a))
                return -1;
            if (push(rt, js_num(-js_val_to_number(&a))))
                return -1;
            break;
        }
        case OP_NOT: {
            struct js_value a;
            if (pop(rt, &a))
                return -1;
            if (push(rt, js_bool(!truthy(a))))
                return -1;
            break;
        }
        case OP_TYPEOF: {
            struct js_value a;
            if (pop(rt, &a))
                return -1;
            const char *t = "undefined";
            if (a.type == JT_NULL) t = "object";
            else if (a.type == JT_BOOL) t = "boolean";
            else if (a.type == JT_NUM) t = "number";
            else if (a.type == JT_STR) t = "string";
            else if (a.type == JT_FUNC || a.type == JT_NATIVE) t = "function";
            else if (a.type == JT_OBJ || a.type == JT_ARR || a.type == JT_DOM) t = "object";
            struct js_string *s = js_str_new(rt, t, strlen(t));
            struct js_value v;
            v.type = JT_STR;
            v.u.s = s;
            if (push(rt, v))
                return -1;
            break;
        }
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            struct js_value b, a;
            if (pop(rt, &b) || pop(rt, &a))
                return -1;
            int r = 0;
            if (a.type == JT_STR && b.type == JT_STR && a.u.s && b.u.s) {
                int cmp = strcmp(a.u.s->data, b.u.s->data);
                if (op == OP_EQ) r = cmp == 0;
                else if (op == OP_NE) r = cmp != 0;
                else if (op == OP_LT) r = cmp < 0;
                else if (op == OP_LE) r = cmp <= 0;
                else if (op == OP_GT) r = cmp > 0;
                else r = cmp >= 0;
            } else {
                double x = js_val_to_number(&a), y = js_val_to_number(&b);
                if (op == OP_EQ) r = x == y;
                else if (op == OP_NE) r = x != y;
                else if (op == OP_LT) r = x < y;
                else if (op == OP_LE) r = x <= y;
                else if (op == OP_GT) r = x > y;
                else r = x >= y;
            }
            if (push(rt, js_bool(r)))
                return -1;
            break;
        }
        case OP_JMP: {
            int16_t off = rdi16(rt, &fr->ip);
            fr->ip = (uint32_t)((int32_t)fr->ip + off);
            break;
        }
        case OP_JMP_IF: {
            int16_t off = rdi16(rt, &fr->ip);
            if (rt->sp == 0)
                return -1;
            if (truthy(rt->stack[rt->sp - 1]))
                fr->ip = (uint32_t)((int32_t)fr->ip + off);
            break;
        }
        case OP_JMP_IFN: {
            int16_t off = rdi16(rt, &fr->ip);
            if (rt->sp == 0)
                return -1;
            if (!truthy(rt->stack[rt->sp - 1]))
                fr->ip = (uint32_t)((int32_t)fr->ip + off);
            break;
        }
        case OP_CALL: {
            uint8_t argc = rd8(rt, &fr->ip);
            if (do_call(rt, argc))
                return -1;
            break;
        }
        case OP_RET: {
            struct js_value ret = js_undef();
            if (rt->sp > fr->bp + fr->local_count)
                pop(rt, &ret);
            /* pop locals */
            rt->sp = fr->bp;
            rt->fp--;
            if (push(rt, ret))
                return -1;
            if (rt->fp <= stop_at)
                return 0;
            break;
        }
        case OP_NEW_OBJ: {
            struct js_object *o = js_obj_new(rt, 0);
            struct js_value v;
            v.type = JT_OBJ;
            v.u.o = o;
            if (push(rt, v))
                return -1;
            break;
        }
        case OP_NEW_ARR: {
            struct js_object *o = js_obj_new(rt, 1);
            struct js_value v;
            v.type = JT_ARR;
            v.u.o = o;
            if (push(rt, v))
                return -1;
            break;
        }
        case OP_ARR_PUSH: {
            struct js_value val;
            if (pop(rt, &val))
                return -1;
            if (rt->sp == 0)
                return -1;
            struct js_value arr = rt->stack[rt->sp - 1];
            if (arr.type == JT_ARR && arr.u.o) {
                char key[16];
                snprintf(key, sizeof(key), "%u", (unsigned)arr.u.o->arr_len);
                js_obj_set(rt, arr.u.o, key, val);
            }
            break;
        }
        case OP_MAKE_FUNC: {
            uint16_t off = rd16(rt, &fr->ip);
            uint8_t arity = rd8(rt, &fr->ip);
            uint8_t locals = rd8(rt, &fr->ip);
            struct js_object *o = js_obj_new(rt, 0);
            if (!o)
                return -1;
            o->is_func = 1;
            o->code_off = off;
            o->arity = arity;
            o->local_count = locals;
            o->closure_env = fr->env;
            struct js_value v;
            v.type = JT_FUNC;
            v.u.o = o;
            if (push(rt, v))
                return -1;
            break;
        }
        case OP_THIS:
            if (push(rt, fr->this_v))
                return -1;
            break;
        case OP_THROW: {
            struct js_value ex;
            if (pop(rt, &ex))
                return -1;
            /* unwind to catch */
            while (rt->fp > stop_at) {
                fr = &rt->frames[rt->fp - 1];
                if (fr->catch_ip >= 0) {
                    rt->sp = fr->bp + fr->local_count;
                    if (push(rt, ex))
                        return -1;
                    fr->ip = (uint32_t)fr->catch_ip;
                    fr->catch_ip = -1;
                    goto next_ins;
                }
                rt->sp = fr->bp;
                rt->fp--;
            }
            char buf[96];
            js_val_to_cstring(rt, &ex, buf, sizeof(buf));
            snprintf(rt->err, sizeof(rt->err), "uncaught: %s", buf);
            return -1;
        }
        case OP_TRY_PUSH: {
            int16_t off = rdi16(rt, &fr->ip);
            fr->catch_ip = (int)((int32_t)fr->ip + off);
            break;
        }
        case OP_TRY_POP:
            fr->catch_ip = -1;
            break;
        case OP_HALT:
            rt->fp = stop_at;
            return 0;
        default:
            snprintf(rt->err, sizeof(rt->err), "bad opcode %u", (unsigned)op);
            return -1;
        }
    next_ins:;
    }
    return 0;
}

/* ---------- builtins ---------- */

static int nat_print(struct js_runtime *rt, int argc, void *argv, void *ret,
                     void *ud) {
    (void)ud;
    char buf[256];
    for (int i = 0; i < argc; i++) {
        js_val_to_cstring(rt, &((struct js_value *)argv)[i], buf, sizeof(buf));
        console_write(buf);
        if (i + 1 < argc)
            console_write(" ");
    }
    console_write("\n");
    js_val_set_undefined(ret);
    return 0;
}

static int nat_console_log(struct js_runtime *rt, int argc, void *argv, void *ret,
                           void *ud) {
    return nat_print(rt, argc, argv, ret, ud);
}

static int nat_parse_int(struct js_runtime *rt, int argc, void *argv, void *ret,
                         void *ud) {
    (void)ud;
    double n = 0;
    if (argc > 0)
        n = js_val_to_number(&((struct js_value *)argv)[0]);
    js_val_set_number(ret, (double)(long long)n);
    (void)rt;
    return 0;
}

static int nat_math_floor(struct js_runtime *rt, int argc, void *argv, void *ret,
                          void *ud) {
    (void)rt;
    (void)ud;
    double n = argc > 0 ? js_val_to_number(&((struct js_value *)argv)[0]) : 0;
    js_val_set_number(ret, (double)(long long)n);
    return 0;
}
static int nat_math_abs(struct js_runtime *rt, int argc, void *argv, void *ret,
                        void *ud) {
    (void)rt;
    (void)ud;
    double n = argc > 0 ? js_val_to_number(&((struct js_value *)argv)[0]) : 0;
    js_val_set_number(ret, n < 0 ? -n : n);
    return 0;
}
static int nat_math_min(struct js_runtime *rt, int argc, void *argv, void *ret,
                        void *ud) {
    (void)rt;
    (void)ud;
    double m = argc > 0 ? js_val_to_number(&((struct js_value *)argv)[0]) : 0;
    for (int i = 1; i < argc; i++) {
        double n = js_val_to_number(&((struct js_value *)argv)[i]);
        if (n < m)
            m = n;
    }
    js_val_set_number(ret, m);
    return 0;
}
static int nat_math_max(struct js_runtime *rt, int argc, void *argv, void *ret,
                        void *ud) {
    (void)rt;
    (void)ud;
    double m = argc > 0 ? js_val_to_number(&((struct js_value *)argv)[0]) : 0;
    for (int i = 1; i < argc; i++) {
        double n = js_val_to_number(&((struct js_value *)argv)[i]);
        if (n > m)
            m = n;
    }
    js_val_set_number(ret, m);
    return 0;
}

static int nat_json_stringify(struct js_runtime *rt, int argc, void *argv, void *ret,
                              void *ud) {
    (void)ud;
    char buf[256];
    if (argc < 1) {
        js_val_set_undefined(ret);
        return 0;
    }
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], buf, sizeof(buf));
    return js_val_set_string(rt, ret, buf);
}

static int nat_set_timeout(struct js_runtime *rt, int argc, void *argv, void *ret,
                           void *ud) {
    (void)ud;
    if (argc < 1) {
        js_val_set_number(ret, -1);
        return 0;
    }
    uint32_t ms = argc > 1 ? (uint32_t)js_val_to_number(&((struct js_value *)argv)[1]) : 0;
    int id = js_set_timeout(rt, &((struct js_value *)argv)[0], ms, 0);
    js_val_set_number(ret, (double)id);
    return 0;
}
static int nat_set_interval(struct js_runtime *rt, int argc, void *argv, void *ret,
                            void *ud) {
    (void)ud;
    if (argc < 1) {
        js_val_set_number(ret, -1);
        return 0;
    }
    uint32_t ms = argc > 1 ? (uint32_t)js_val_to_number(&((struct js_value *)argv)[1]) : 0;
    int id = js_set_timeout(rt, &((struct js_value *)argv)[0], ms, 1);
    js_val_set_number(ret, (double)id);
    return 0;
}
static int nat_clear_timer(struct js_runtime *rt, int argc, void *argv, void *ret,
                           void *ud) {
    (void)ud;
    if (argc > 0)
        js_clear_timer(rt, (int)js_val_to_number(&((struct js_value *)argv)[0]));
    js_val_set_undefined(ret);
    return 0;
}

static int nat_queue_microtask(struct js_runtime *rt, int argc, void *argv, void *ret,
                               void *ud) {
    (void)ud;
    js_val_set_undefined(ret);
    if (!rt || argc < 1 || rt->micro_n >= 32)
        return 0;
    struct js_value *fn = &((struct js_value *)argv)[0];
    if (!js_val_is_function(fn))
        return 0;
    rt->micro[rt->micro_n++] = *fn;
    return 0;
}

/* Promise.resolve(v).then(fn) — schedules fn as a microtask (arg ignored in v1). */
static int nat_promise_then(struct js_runtime *rt, int argc, void *argv, void *ret,
                            void *ud) {
    struct js_object *self = ud;
    js_val_set_undefined(ret);
    if (!rt || argc < 1 || !self)
        return 0;
    struct js_value *fn = &((struct js_value *)argv)[0];
    if (js_val_is_function(fn) && rt->micro_n < 32)
        rt->micro[rt->micro_n++] = *fn;
    struct js_value out;
    out.type = JT_OBJ;
    out.u.o = self;
    *(struct js_value *)ret = out;
    return 0;
}

static int nat_promise_resolve(struct js_runtime *rt, int argc, void *argv, void *ret,
                               void *ud) {
    (void)ud;
    struct js_value obj;
    if (js_val_new_object(rt, &obj) != 0) {
        js_val_set_undefined(ret);
        return 0;
    }
    struct js_value v = argc > 0 ? ((struct js_value *)argv)[0] : js_undef();
    js_val_set_prop(rt, &obj, "__v", &v);
    struct js_object *th = js_obj_new(rt, 0);
    if (th) {
        th->is_native = 1;
        th->is_func = 1;
        th->native = nat_promise_then;
        th->userdata = obj.u.o;
        struct js_value tv;
        tv.type = JT_NATIVE;
        tv.u.o = th;
        js_val_set_prop(rt, &obj, "then", &tv);
    }
    *(struct js_value *)ret = obj;
    return 0;
}

void js_install_builtins(struct js_runtime *rt) {
    js_rt_set_global_fn(rt, "print", nat_print, NULL);
    js_rt_set_global_fn(rt, "parseInt", nat_parse_int, NULL);
    js_rt_set_global_fn(rt, "setTimeout", nat_set_timeout, NULL);
    js_rt_set_global_fn(rt, "setInterval", nat_set_interval, NULL);
    js_rt_set_global_fn(rt, "clearTimeout", nat_clear_timer, NULL);
    js_rt_set_global_fn(rt, "clearInterval", nat_clear_timer, NULL);
    js_rt_set_global_fn(rt, "queueMicrotask", nat_queue_microtask, NULL);

    struct js_value console;
    js_val_new_object(rt, &console);
    struct js_object *clog = js_obj_new(rt, 0);
    clog->is_native = 1;
    clog->is_func = 1;
    clog->native = nat_console_log;
    struct js_value fv;
    fv.type = JT_NATIVE;
    fv.u.o = clog;
    js_val_set_prop(rt, &console, "log", &fv);
    js_obj_set(rt, rt->global, "console", console);

    struct js_value math;
    js_val_new_object(rt, &math);
    struct {
        const char *n;
        js_native_fn f;
    } mf[] = {
        { "floor", nat_math_floor },
        { "abs", nat_math_abs },
        { "min", nat_math_min },
        { "max", nat_math_max },
    };
    for (unsigned i = 0; i < sizeof(mf) / sizeof(mf[0]); i++) {
        struct js_object *o = js_obj_new(rt, 0);
        o->is_native = 1;
        o->is_func = 1;
        o->native = mf[i].f;
        struct js_value v;
        v.type = JT_NATIVE;
        v.u.o = o;
        js_val_set_prop(rt, &math, mf[i].n, &v);
    }
    js_obj_set(rt, rt->global, "Math", math);

    struct js_value json;
    js_val_new_object(rt, &json);
    struct js_object *jsfn = js_obj_new(rt, 0);
    jsfn->is_native = 1;
    jsfn->is_func = 1;
    jsfn->native = nat_json_stringify;
    struct js_value jv;
    jv.type = JT_NATIVE;
    jv.u.o = jsfn;
    js_val_set_prop(rt, &json, "stringify", &jv);
    js_obj_set(rt, rt->global, "JSON", json);

    struct js_value promise;
    js_val_new_object(rt, &promise);
    struct js_object *pres = js_obj_new(rt, 0);
    if (pres) {
        pres->is_native = 1;
        pres->is_func = 1;
        pres->native = nat_promise_resolve;
        struct js_value pv;
        pv.type = JT_NATIVE;
        pv.u.o = pres;
        js_val_set_prop(rt, &promise, "resolve", &pv);
    }
    js_obj_set(rt, rt->global, "Promise", promise);

    (void)to_str_val;
    (void)serial_write_str;
}
