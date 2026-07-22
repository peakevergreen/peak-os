#include "js_compile_internal.h"

static int emit_byte(struct js_compiler *c, uint8_t b) {
    struct js_runtime *rt = c->rt;
    if (rt->code_len + 1 >= rt->code_cap) {
        uint32_t nc = rt->code_cap * 2;
        if (nc > JS_CODE_MAX)
            nc = JS_CODE_MAX;
        if (rt->code_len + 1 >= nc) {
            js_lex_error(c, "code too large");
            return -1;
        }
        uint8_t *n = krealloc(rt->code, nc);
        if (!n) {
            js_lex_error(c, "oom");
            return -1;
        }
        rt->code = n;
        rt->code_cap = nc;
    }
    rt->code[rt->code_len++] = b;
    return 0;
}

int js_emit_op(struct js_compiler *c, enum js_op op) { return emit_byte(c, (uint8_t)op); }
int js_emit_u8(struct js_compiler *c, uint8_t v) { return emit_byte(c, v); }
int js_emit_u16(struct js_compiler *c, uint16_t v) {
    return emit_byte(c, (uint8_t)(v & 0xFF)) || emit_byte(c, (uint8_t)(v >> 8));
}
int js_emit_i16(struct js_compiler *c, int16_t v) { return js_emit_u16(c, (uint16_t)v); }
int js_emit_f64(struct js_compiler *c, double n) {
    union { double d; uint8_t b[8]; } u;
    u.d = n;
    for (int i = 0; i < 8; i++)
        if (emit_byte(c, u.b[i]))
            return -1;
    return 0;
}
uint32_t js_emit_here(struct js_compiler *c) { return c->rt->code_len; }
void js_emit_patch_i16(struct js_compiler *c, uint32_t at, int16_t v) {
    c->rt->code[at] = (uint8_t)(v & 0xFF);
    c->rt->code[at + 1] = (uint8_t)(v >> 8);
}

int js_intern_str(struct js_compiler *c, const char *s) {
    struct js_runtime *rt = c->rt;
    for (uint32_t i = 0; i < rt->str_count; i++)
        if (!strcmp(rt->strtab[i], s))
            return (int)i;
    if (rt->str_count >= rt->str_cap) {
        uint32_t nc = rt->str_cap * 2;
        if (nc > JS_STRTAB_MAX)
            return -1;
        char **n = krealloc(rt->strtab, nc * sizeof(char *));
        if (!n)
            return -1;
        rt->strtab = n;
        rt->str_cap = nc;
    }
    size_t len = strlen(s);
    char *copy = kmalloc(len + 1);
    if (!copy)
        return -1;
    memcpy(copy, s, len + 1);
    rt->strtab[rt->str_count] = copy;
    return (int)rt->str_count++;
}
