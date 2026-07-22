#include "js_compile_internal.h"

/* Peak JS compiler entry: lexer + recursive-descent parser → bytecode. */

int js_compile(struct js_runtime *rt, const char *src, const char *filename) {
    struct js_compiler c;
    memset(&c, 0, sizeof(c));
    c.rt = rt;
    c.src = src;
    c.p = src;
    c.filename = filename;
    c.line = 1;
    js_parse_scope_reset(); /* top-level bindings are globals; functions push scopes */
    js_lex_next(&c);
    while (c.tok != T_EOF) {
        if (js_parse_stmt(&c))
            return -1;
    }
    js_emit_op(&c, OP_HALT);
    return 0;
}
