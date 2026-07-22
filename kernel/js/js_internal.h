#ifndef PEAK_JS_INTERNAL_H
#define PEAK_JS_INTERNAL_H

#include "js.h"
#include "heap.h"
#include "util.h"

enum js_type {
    JT_UNDEF = 0,
    JT_NULL,
    JT_BOOL,
    JT_NUM,
    JT_STR,
    JT_OBJ,
    JT_ARR,
    JT_FUNC,
    JT_NATIVE,
    JT_DOM, /* host object handle */
};

struct js_string {
    int marked;
    uint32_t len;
    char *data;
};

struct js_object;

struct js_value {
    uint8_t type;
    union {
        int b;
        double n;
        struct js_string *s;
        struct js_object *o;
    } u;
};

struct js_prop {
    char *key;
    struct js_value val;
    struct js_prop *next;
};

struct js_object {
    int marked;
    int is_array;
    int is_func;
    int is_native;
    int is_dom;
    uint32_t arr_len;
    struct js_prop *props;
    /* function */
    uint32_t code_off;
    uint16_t arity;
    uint16_t local_count;
    struct js_object *proto;
    struct js_object *closure_env; /* parent env object */
    js_native_fn native;
    void *userdata;
    /* DOM bridge */
    void *dom_node;
};

enum js_op {
    OP_NOP = 0,
    OP_PUSH_UNDEF,
    OP_PUSH_NULL,
    OP_PUSH_BOOL,   /* u8 */
    OP_PUSH_NUM,    /* f64 */
    OP_PUSH_STR,    /* u16 idx */
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,
    OP_POP,
    OP_DUP,
    OP_GET_GLOBAL,  /* u16 idx */
    OP_SET_GLOBAL,  /* u16 idx */
    OP_GET_LOCAL,   /* u16 */
    OP_SET_LOCAL,   /* u16 */
    OP_GET_PROP,    /* u16 key idx */
    OP_SET_PROP,    /* u16 key idx */
    OP_GET_IDX,
    OP_SET_IDX,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_NEG, OP_NOT, OP_TYPEOF,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_AND, OP_OR,
    OP_JMP,         /* i16 */
    OP_JMP_IF,      /* i16 */
    OP_JMP_IFN,     /* i16 */
    OP_CALL,        /* u8 argc */
    OP_RET,
    OP_NEW_OBJ,
    OP_NEW_ARR,
    OP_ARR_PUSH,
    OP_MAKE_FUNC,   /* u16 code_off, u8 arity, u8 locals */
    OP_THIS,
    OP_THROW,
    OP_TRY_PUSH,    /* i16 catch */
    OP_TRY_POP,
    OP_CLOSURE_GET, /* u16 */
    OP_CLOSURE_SET, /* u16 */
    OP_INC_LOCAL,   /* u16 */
    OP_HALT,
};

#define JS_CODE_MAX   65536
#define JS_STRTAB_MAX 1024
#define JS_STACK_MAX  512
#define JS_FRAME_MAX  64
#define JS_TIMER_MAX  64
#define JS_OBJ_MAX_DEFAULT 4096

struct js_frame {
    uint32_t ip;
    uint32_t bp; /* stack base */
    uint16_t local_base;
    uint16_t local_count;
    struct js_object *func;
    struct js_object *env;
    struct js_value this_v;
    int catch_ip; /* -1 none */
};

struct js_timer {
    int used;
    int repeat;
    uint64_t due_tick;
    uint32_t interval_ticks;
    struct js_value fn;
};

struct js_runtime {
    struct js_value stack[JS_STACK_MAX];
    uint32_t sp;
    struct js_frame frames[JS_FRAME_MAX];
    int fp;
    uint8_t *code;
    uint32_t code_len;
    uint32_t code_cap;
    char **strtab;
    uint32_t str_count;
    uint32_t str_cap;
    struct js_object *global;
    struct js_object **objs;
    uint32_t obj_count;
    uint32_t obj_cap;
    struct js_string **strs;
    uint32_t heap_str_count;
    uint32_t heap_str_cap;
    uint32_t ins_budget;
    uint32_t ins_used;
    uint32_t max_objs;
    uint32_t gc_runs;
    char err[JS_ERR_MAX];
    struct js_timer timers[JS_TIMER_MAX];
    int next_timer_id;
    struct js_value micro[32];
    int micro_n;
    int aborted;
};

/* compile */
int js_compile(struct js_runtime *rt, const char *src, const char *filename);

/* vm */
int js_vm_run(struct js_runtime *rt, uint32_t entry_ip);

/* value/object helpers */
struct js_string *js_str_new(struct js_runtime *rt, const char *s, size_t len);
struct js_object *js_obj_new(struct js_runtime *rt, int is_array);
int js_obj_set(struct js_runtime *rt, struct js_object *o, const char *key,
               struct js_value v);
int js_obj_get(struct js_runtime *rt, struct js_object *o, const char *key,
               struct js_value *out);
void js_gc(struct js_runtime *rt);
void js_install_builtins(struct js_runtime *rt);

static inline struct js_value js_undef(void) {
    struct js_value v;
    v.type = JT_UNDEF;
    v.u.n = 0;
    return v;
}
static inline struct js_value js_null(void) {
    struct js_value v;
    v.type = JT_NULL;
    v.u.n = 0;
    return v;
}
static inline struct js_value js_bool(int b) {
    struct js_value v;
    v.type = JT_BOOL;
    v.u.b = b ? 1 : 0;
    return v;
}
static inline struct js_value js_num(double n) {
    struct js_value v;
    v.type = JT_NUM;
    v.u.n = n;
    return v;
}

#endif
