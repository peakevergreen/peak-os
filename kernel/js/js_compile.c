#include "js_compile_internal.h"

/* Peak JS compiler entry: lexer + recursive-descent parser → bytecode. */

int js_compile(struct js_runtime *rt, const char *src, const char *filename) {
    return js_compile_ex(rt, src, filename, 0);
}

int js_compile_ex(struct js_runtime *rt, const char *src, const char *filename,
                  int module_mode) {
    struct js_compiler c;
    memset(&c, 0, sizeof(c));
    c.rt = rt;
    c.src = src;
    c.p = src;
    c.filename = filename;
    c.line = 1;
    c.module_mode = module_mode ? 1 : 0;
    js_parse_scope_reset(); /* top-level bindings are globals; functions push scopes */
    js_lex_next(&c);
    if (c.module_mode) {
        /* exports = {}; available as global for export statements */
        js_emit_op(&c, OP_NEW_OBJ);
        int id = js_intern_str(&c, "exports");
        if (id < 0)
            return -1;
        js_emit_op(&c, OP_SET_GLOBAL);
        js_emit_u16(&c, (uint16_t)id);
        js_emit_op(&c, OP_POP);
    }
    while (c.tok != T_EOF) {
        if (js_parse_stmt(&c))
            return -1;
    }
    if (c.module_mode) {
        int id = js_intern_str(&c, "exports");
        if (id < 0)
            return -1;
        js_emit_op(&c, OP_GET_GLOBAL);
        js_emit_u16(&c, (uint16_t)id);
    }
    js_emit_op(&c, OP_HALT);
    return 0;
}
