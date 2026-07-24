#include "js_compile_internal.h"

#define next(c) js_lex_next(c)
#define emit_op(c, op) js_emit_op(c, op)
#define emit_u8(c, v) js_emit_u8(c, v)
#define emit_u16(c, v) js_emit_u16(c, v)
#define emit_i16(c, v) js_emit_i16(c, v)
#define emit_f64(c, n) js_emit_f64(c, n)
#define here(c) js_emit_here(c)
#define patch_i16(c, at, v) js_emit_patch_i16(c, at, v)
#define intern(c, s) js_intern_str(c, s)
#define cerr(c, msg) js_lex_error(c, msg)

/* local/global name table for current function */
#define LOC_MAX 64
struct scope {
    char names[LOC_MAX][48];
    int n;
    int is_fn;
};
static struct scope scopes[16];
static int scope_sp;

static void scope_push(int is_fn) {
    if (scope_sp < 16) {
        scopes[scope_sp].n = 0;
        scopes[scope_sp].is_fn = is_fn;
        scope_sp++;
    }
}
static void scope_pop(void) {
    if (scope_sp > 0)
        scope_sp--;
}
static int scope_add(const char *name) {
    if (scope_sp <= 0)
        return -1;
    struct scope *s = &scopes[scope_sp - 1];
    for (int i = 0; i < s->n; i++)
        if (!strcmp(s->names[i], name))
            return i;
    if (s->n >= LOC_MAX)
        return -1;
    snprintf(s->names[s->n], sizeof(s->names[0]), "%s", name);
    return s->n++;
}
static int scope_find(const char *name, int *depth_out) {
    for (int d = scope_sp - 1; d >= 0; d--) {
        for (int i = 0; i < scopes[d].n; i++) {
            if (!strcmp(scopes[d].names[i], name)) {
                if (depth_out)
                    *depth_out = scope_sp - 1 - d;
                return i;
            }
        }
    }
    return -1;
}

static int parse_stmt(struct js_compiler *c);
static int parse_expr(struct js_compiler *c);

static int emit_make_func(struct js_compiler *c, uint32_t body, uint8_t arity,
                          uint8_t locals, int is_async) {
    if (js_emit_op(c, OP_MAKE_FUNC) || js_emit_u16(c, (uint16_t)body) ||
        js_emit_u8(c, arity) || js_emit_u8(c, locals) ||
        js_emit_u8(c, is_async ? 1 : 0))
        return -1;
    return 0;
}

static int export_bind(struct js_compiler *c, const char *name) {
    if (!c->module_mode)
        return 0;
    int eid = js_intern_str(c, "exports");
    int nid = js_intern_str(c, name);
    if (eid < 0 || nid < 0)
        return -1;
    if (js_emit_op(c, OP_GET_GLOBAL) || js_emit_u16(c, (uint16_t)eid))
        return -1;
    if (js_emit_op(c, OP_GET_GLOBAL) || js_emit_u16(c, (uint16_t)nid))
        return -1;
    if (js_emit_op(c, OP_SET_PROP) || js_emit_u16(c, (uint16_t)nid))
        return -1;
    return js_emit_op(c, OP_POP);
}

/* Compact lexer checkpoint for assignment / arrow lookahead (avoids 256B memcpy). */
struct lex_save {
    const char *p;
    enum js_tok tok;
    double num;
    int line;
    char text[48];
};

static void lex_checkpoint(struct js_compiler *c, struct lex_save *s) {
    s->p = c->p;
    s->tok = c->tok;
    s->num = c->num;
    s->line = c->line;
    snprintf(s->text, sizeof(s->text), "%s", c->text);
}

static void lex_restore(struct js_compiler *c, const struct lex_save *s) {
    c->p = s->p;
    c->tok = s->tok;
    c->num = s->num;
    c->line = s->line;
    snprintf(c->text, sizeof(c->text), "%s", s->text);
}

static int expect(struct js_compiler *c, enum js_tok t, const char *msg) {
    if (c->tok != t) {
        js_lex_error(c, msg);
        return -1;
    }
    js_lex_next(c);
    return 0;
}

static int parse_primary(struct js_compiler *c) {
    if (c->tok == T_ASYNC) {
        js_lex_next(c);
        c->pending_async = 1;
        if (c->tok == T_FUNCTION)
            return parse_primary(c); /* fall into function with pending_async */
        if (c->tok == T_LPAREN)
            return parse_primary(c); /* arrow () => with pending_async */
        js_lex_error(c, "async unsupported");
        c->pending_async = 0;
        return -1;
    }
    if (c->tok == T_NUM) {
        if (js_emit_op(c, OP_PUSH_NUM) || js_emit_f64(c, c->num))
            return -1;
        js_lex_next(c);
        return 0;
    }
    if (c->tok == T_STR) {
        int id = js_intern_str(c, c->text);
        if (id < 0)
            return -1;
        if (js_emit_op(c, OP_PUSH_STR) || js_emit_u16(c, (uint16_t)id))
            return -1;
        js_lex_next(c);
        return 0;
    }
    if (c->tok == T_TRUE) {
        js_emit_op(c, OP_PUSH_TRUE);
        js_lex_next(c);
        return 0;
    }
    if (c->tok == T_FALSE) {
        js_emit_op(c, OP_PUSH_FALSE);
        js_lex_next(c);
        return 0;
    }
    if (c->tok == T_NULL) {
        js_emit_op(c, OP_PUSH_NULL);
        js_lex_next(c);
        return 0;
    }
    if (c->tok == T_UNDEFINED) {
        js_emit_op(c, OP_PUSH_UNDEF);
        js_lex_next(c);
        return 0;
    }
    if (c->tok == T_THIS) {
        js_emit_op(c, OP_THIS);
        js_lex_next(c);
        return 0;
    }
    if (c->tok == T_IDENT) {
        char name[48];
        snprintf(name, sizeof(name), "%s", c->text);
        js_lex_next(c);
        /* Single-arg arrow without (): not supported — error without emitting. */
        if (c->tok == T_ARROW) {
            js_lex_error(c, "arrow without () not supported; use (x)=>");
            return -1;
        }
        int depth = 0;
        int loc = scope_find(name, &depth);
        if (loc >= 0 && depth == 0) {
            js_emit_op(c, OP_GET_LOCAL);
            js_emit_u16(c, (uint16_t)loc);
        } else {
            int id = js_intern_str(c, name);
            if (id < 0)
                return -1;
            js_emit_op(c, OP_GET_GLOBAL);
            js_emit_u16(c, (uint16_t)id);
        }
        return 0;
    }
    if (c->tok == T_LPAREN) {
        js_lex_next(c);
        /* arrow () => or (a,b)=> */
        if (c->tok == T_RPAREN) {
            js_lex_next(c);
            if (c->tok == T_ARROW) {
                js_lex_next(c);
                uint32_t jmp_over = js_emit_here(c);
                js_emit_op(c, OP_JMP);
                js_emit_i16(c, 0);
                uint32_t body = js_emit_here(c);
                scope_push(1);
                if (c->tok == T_LBRACE) {
                    js_lex_next(c);
                    while (c->tok != T_RBRACE && c->tok != T_EOF)
                        if (parse_stmt(c))
                            return -1;
                    if (expect(c, T_RBRACE, "'}'"))
                        return -1;
                    js_emit_op(c, OP_PUSH_UNDEF);
                    js_emit_op(c, OP_RET);
                } else {
                    if (parse_expr(c))
                        return -1;
                    js_emit_op(c, OP_RET);
                }
                int locals = scopes[scope_sp - 1].n;
                scope_pop();
                uint32_t after = js_emit_here(c);
                js_emit_patch_i16(c, jmp_over + 1, (int16_t)(after - (jmp_over + 3)));
                int asy = c->pending_async;
                c->pending_async = 0;
                return emit_make_func(c, body, 0, (uint8_t)locals, asy);
            }
            js_lex_error(c, "empty ()");
            c->pending_async = 0;
            return -1;
        }
        /* look ahead for arrow params: ident , ... ) => */
        if (c->tok == T_IDENT) {
            char params[8][48];
            int np = 0;
            struct lex_save save;
            lex_checkpoint(c, &save);
            while (c->tok == T_IDENT && np < 8) {
                snprintf(params[np++], sizeof(params[0]), "%s", c->text);
                js_lex_next(c);
                if (c->tok == T_COMMA) {
                    js_lex_next(c);
                    continue;
                }
                break;
            }
            if (c->tok == T_RPAREN) {
                js_lex_next(c);
                if (c->tok == T_ARROW) {
                    js_lex_next(c);
                    uint32_t jmp_over = js_emit_here(c);
                    js_emit_op(c, OP_JMP);
                    js_emit_i16(c, 0);
                    uint32_t body = js_emit_here(c);
                    scope_push(1);
                    for (int i = 0; i < np; i++)
                        scope_add(params[i]);
                    if (c->tok == T_LBRACE) {
                        js_lex_next(c);
                        while (c->tok != T_RBRACE && c->tok != T_EOF)
                            if (parse_stmt(c))
                                return -1;
                        if (expect(c, T_RBRACE, "'}'"))
                            return -1;
                        js_emit_op(c, OP_PUSH_UNDEF);
                        js_emit_op(c, OP_RET);
                    } else {
                        if (parse_expr(c))
                            return -1;
                        js_emit_op(c, OP_RET);
                    }
                    int locals = scopes[scope_sp - 1].n;
                    scope_pop();
                    uint32_t after = js_emit_here(c);
                    js_emit_patch_i16(c, jmp_over + 1, (int16_t)(after - (jmp_over + 3)));
                    int asy = c->pending_async;
                    c->pending_async = 0;
                    return emit_make_func(c, body, (uint8_t)np, (uint8_t)locals, asy);
                }
            }
            /* restore and parse as grouped expr */
            lex_restore(c, &save);
            c->pending_async = 0;
        }
        if (parse_expr(c))
            return -1;
        return expect(c, T_RPAREN, "')'");
    }
    if (c->tok == T_LBRACE) {
        js_lex_next(c);
        js_emit_op(c, OP_NEW_OBJ);
        while (c->tok != T_RBRACE && c->tok != T_EOF) {
            char key[64];
            if (c->tok == T_IDENT || c->tok == T_STR) {
                snprintf(key, sizeof(key), "%s", c->text);
                js_lex_next(c);
            } else {
                js_lex_error(c, "object key");
                return -1;
            }
            if (expect(c, T_COLON, "':'"))
                return -1;
            if (parse_expr(c))
                return -1;
            int id = js_intern_str(c, key);
            if (id < 0)
                return -1;
            js_emit_op(c, OP_SET_PROP);
            js_emit_u16(c, (uint16_t)id);
            /* SET_PROP consumes value but we need object on stack — VM keeps obj */
            if (c->tok == T_COMMA)
                js_lex_next(c);
        }
        return expect(c, T_RBRACE, "'}'");
    }
    if (c->tok == T_LBRACK) {
        js_lex_next(c);
        js_emit_op(c, OP_NEW_ARR);
        while (c->tok != T_RBRACK && c->tok != T_EOF) {
            if (parse_expr(c))
                return -1;
            js_emit_op(c, OP_ARR_PUSH);
            if (c->tok == T_COMMA)
                js_lex_next(c);
        }
        return expect(c, T_RBRACK, "']'");
    }
    if (c->tok == T_FUNCTION) {
        js_lex_next(c);
        char fname[48] = "";
        if (c->tok == T_IDENT) {
            snprintf(fname, sizeof(fname), "%s", c->text);
            js_lex_next(c);
        }
        if (expect(c, T_LPAREN, "'('"))
            return -1;
        char params[8][48];
        int np = 0;
        while (c->tok != T_RPAREN && c->tok != T_EOF && np < 8) {
            if (c->tok != T_IDENT) {
                js_lex_error(c, "param");
                return -1;
            }
            snprintf(params[np++], sizeof(params[0]), "%s", c->text);
            js_lex_next(c);
            if (c->tok == T_COMMA)
                js_lex_next(c);
        }
        if (expect(c, T_RPAREN, "')'") || expect(c, T_LBRACE, "'{'"))
            return -1;
        uint32_t jmp_over = js_emit_here(c);
        js_emit_op(c, OP_JMP);
        js_emit_i16(c, 0);
        uint32_t body = js_emit_here(c);
        scope_push(1);
        for (int i = 0; i < np; i++)
            scope_add(params[i]);
        while (c->tok != T_RBRACE && c->tok != T_EOF)
            if (parse_stmt(c))
                return -1;
        if (expect(c, T_RBRACE, "'}'"))
            return -1;
        js_emit_op(c, OP_PUSH_UNDEF);
        js_emit_op(c, OP_RET);
        int locals = scopes[scope_sp - 1].n;
        scope_pop();
        uint32_t after = js_emit_here(c);
        js_emit_patch_i16(c, jmp_over + 1, (int16_t)(after - (jmp_over + 3)));
        int asy = c->pending_async;
        c->pending_async = 0;
        if (emit_make_func(c, body, (uint8_t)np, (uint8_t)locals, asy))
            return -1;
        if (fname[0]) {
            js_emit_op(c, OP_DUP);
            int id = js_intern_str(c, fname);
            js_emit_op(c, OP_SET_GLOBAL);
            js_emit_u16(c, (uint16_t)id);
        }
        return 0;
    }
    js_lex_error(c, "expression");
    return -1;
}

static int parse_postfix(struct js_compiler *c) {
    if (parse_primary(c))
        return -1;
    for (;;) {
        if (c->tok == T_DOT) {
            js_lex_next(c);
            if (c->tok != T_IDENT) {
                js_lex_error(c, "property");
                return -1;
            }
            int id = js_intern_str(c, c->text);
            js_lex_next(c);
            js_emit_op(c, OP_GET_PROP);
            js_emit_u16(c, (uint16_t)id);
        } else if (c->tok == T_LBRACK) {
            js_lex_next(c);
            if (parse_expr(c))
                return -1;
            if (expect(c, T_RBRACK, "']'"))
                return -1;
            js_emit_op(c, OP_GET_IDX);
        } else if (c->tok == T_LPAREN) {
            js_lex_next(c);
            int argc = 0;
            while (c->tok != T_RPAREN && c->tok != T_EOF) {
                if (parse_expr(c))
                    return -1;
                argc++;
                if (c->tok == T_COMMA)
                    js_lex_next(c);
            }
            if (expect(c, T_RPAREN, "')'"))
                return -1;
            js_emit_op(c, OP_CALL);
            js_emit_u8(c, (uint8_t)argc);
        } else {
            break;
        }
    }
    return 0;
}

static int parse_unary(struct js_compiler *c) {
    if (c->tok == T_BANG) {
        js_lex_next(c);
        if (parse_unary(c))
            return -1;
        return js_emit_op(c, OP_NOT);
    }
    if (c->tok == T_MINUS) {
        js_lex_next(c);
        if (parse_unary(c))
            return -1;
        return js_emit_op(c, OP_NEG);
    }
    if (c->tok == T_TYPEOF) {
        js_lex_next(c);
        if (parse_unary(c))
            return -1;
        return js_emit_op(c, OP_TYPEOF);
    }
    if (c->tok == T_AWAIT) {
        js_lex_next(c);
        if (parse_unary(c))
            return -1;
        return js_emit_op(c, OP_AWAIT);
    }
    return parse_postfix(c);
}

static int parse_mul(struct js_compiler *c) {
    if (parse_unary(c))
        return -1;
    while (c->tok == T_STAR || c->tok == T_SLASH || c->tok == T_PERCENT) {
        enum js_tok op = c->tok;
        js_lex_next(c);
        if (parse_unary(c))
            return -1;
        js_emit_op(c, op == T_STAR ? OP_MUL : (op == T_SLASH ? OP_DIV : OP_MOD));
    }
    return 0;
}
static int parse_add(struct js_compiler *c) {
    if (parse_mul(c))
        return -1;
    while (c->tok == T_PLUS || c->tok == T_MINUS) {
        enum js_tok op = c->tok;
        js_lex_next(c);
        if (parse_mul(c))
            return -1;
        js_emit_op(c, op == T_PLUS ? OP_ADD : OP_SUB);
    }
    return 0;
}
static int parse_rel(struct js_compiler *c) {
    /* Hot path: local < number → OP_LT_LOCAL_NUM (one dispatch vs GET/PUSH/LT). */
    if (c->tok == T_IDENT) {
        char name[48];
        snprintf(name, sizeof(name), "%s", c->text);
        struct lex_save save;
        lex_checkpoint(c, &save);
        js_lex_next(c);
        if (c->tok == T_LT) {
            js_lex_next(c);
            if (c->tok == T_NUM) {
                double lim = c->num;
                js_lex_next(c);
                /* RHS must be a bare literal (not `50 + 1`, `50 * 2`, …). */
                if (c->tok != T_PLUS && c->tok != T_MINUS && c->tok != T_STAR &&
                    c->tok != T_SLASH && c->tok != T_PERCENT) {
                    int depth = 0;
                    int loc = scope_find(name, &depth);
                    if (loc >= 0 && depth == 0) {
                        js_emit_op(c, OP_LT_LOCAL_NUM);
                        js_emit_u16(c, (uint16_t)loc);
                        js_emit_f64(c, lim);
                        goto rel_more;
                    }
                }
            }
        }
        lex_restore(c, &save);
    }
    if (parse_add(c))
        return -1;
rel_more:
    while (c->tok == T_LT || c->tok == T_LE || c->tok == T_GT || c->tok == T_GE) {
        enum js_tok op = c->tok;
        js_lex_next(c);
        if (parse_add(c))
            return -1;
        if (op == T_LT)
            js_emit_op(c, OP_LT);
        else if (op == T_LE)
            js_emit_op(c, OP_LE);
        else if (op == T_GT)
            js_emit_op(c, OP_GT);
        else
            js_emit_op(c, OP_GE);
    }
    return 0;
}
static int parse_eq(struct js_compiler *c) {
    if (parse_rel(c))
        return -1;
    while (c->tok == T_EQEQ || c->tok == T_NE) {
        enum js_tok op = c->tok;
        js_lex_next(c);
        if (parse_rel(c))
            return -1;
        js_emit_op(c, op == T_EQEQ ? OP_EQ : OP_NE);
    }
    return 0;
}
static int parse_and(struct js_compiler *c) {
    if (parse_eq(c))
        return -1;
    while (c->tok == T_ANDAND) {
        js_lex_next(c);
        uint32_t j = js_emit_here(c);
        js_emit_op(c, OP_JMP_IFN);
        js_emit_i16(c, 0);
        js_emit_op(c, OP_POP);
        if (parse_eq(c))
            return -1;
        uint32_t after = js_emit_here(c);
        js_emit_patch_i16(c, j + 1, (int16_t)(after - (j + 3)));
    }
    return 0;
}
static int parse_or(struct js_compiler *c) {
    if (parse_and(c))
        return -1;
    while (c->tok == T_OROR) {
        js_lex_next(c);
        uint32_t j = js_emit_here(c);
        js_emit_op(c, OP_JMP_IF);
        js_emit_i16(c, 0);
        js_emit_op(c, OP_POP);
        if (parse_and(c))
            return -1;
        uint32_t after = js_emit_here(c);
        js_emit_patch_i16(c, j + 1, (int16_t)(after - (j + 3)));
    }
    return 0;
}

static int parse_assignment(struct js_compiler *c);

static int parse_expr(struct js_compiler *c) {
    return parse_assignment(c);
}

static int parse_assignment(struct js_compiler *c) {
    /* Detect ident = expr; peephole local i=i±1 and n=n+m. */
    if (c->tok == T_IDENT) {
        char name[48];
        snprintf(name, sizeof(name), "%s", c->text);
        struct lex_save save;
        lex_checkpoint(c, &save);
        js_lex_next(c);
        if (c->tok == T_EQ) {
            js_lex_next(c);
            int depth = 0;
            int loc = scope_find(name, &depth);
            /* Hot paths: local `i = i + 1` / `i = i - 1` / `n = n + m`. */
            if (loc >= 0 && depth == 0 && c->tok == T_IDENT && !strcmp(c->text, name)) {
                struct lex_save save2;
                lex_checkpoint(c, &save2);
                js_lex_next(c);
                if (c->tok == T_PLUS) {
                    js_lex_next(c);
                    if (c->tok == T_NUM && c->num == 1.0) {
                        js_lex_next(c);
                        js_emit_op(c, OP_INC_LOCAL);
                        js_emit_u16(c, (uint16_t)loc);
                        return 0;
                    }
                    if (c->tok == T_IDENT) {
                        int d2 = 0;
                        int src = scope_find(c->text, &d2);
                        if (src >= 0 && d2 == 0) {
                            js_lex_next(c);
                            /* Bare `n = n + m` only (not `n = n + m + 1`). */
                            if (c->tok != T_PLUS && c->tok != T_MINUS &&
                                c->tok != T_STAR && c->tok != T_SLASH &&
                                c->tok != T_PERCENT) {
                                js_emit_op(c, OP_ADD_LOCAL);
                                js_emit_u16(c, (uint16_t)loc);
                                js_emit_u16(c, (uint16_t)src);
                                return 0;
                            }
                        }
                    }
                } else if (c->tok == T_MINUS) {
                    js_lex_next(c);
                    if (c->tok == T_NUM && c->num == 1.0) {
                        js_lex_next(c);
                        js_emit_op(c, OP_DEC_LOCAL);
                        js_emit_u16(c, (uint16_t)loc);
                        return 0;
                    }
                }
                lex_restore(c, &save2);
            }
            if (parse_assignment(c))
                return -1;
            if (loc >= 0 && depth == 0) {
                js_emit_op(c, OP_SET_LOCAL);
                js_emit_u16(c, (uint16_t)loc);
            } else {
                int id = js_intern_str(c, name);
                js_emit_op(c, OP_SET_GLOBAL);
                js_emit_u16(c, (uint16_t)id);
            }
            return 0;
        }
        lex_restore(c, &save);
    }
    return parse_or(c);
}

static int parse_block(struct js_compiler *c) {
    if (expect(c, T_LBRACE, "'{'"))
        return -1;
    while (c->tok != T_RBRACE && c->tok != T_EOF)
        if (parse_stmt(c))
            return -1;
    return expect(c, T_RBRACE, "'}'");
}

static int parse_stmt(struct js_compiler *c) {
    if (c->tok == T_SEMI) {
        js_lex_next(c);
        return 0;
    }
    if (c->tok == T_ASYNC) {
        js_lex_next(c);
        c->pending_async = 1;
        if (c->tok == T_FUNCTION) {
            /* async function as statement: parse as expression stmt */
            if (parse_expr(c))
                return -1;
            js_emit_op(c, OP_POP);
            if (c->tok == T_SEMI)
                js_lex_next(c);
            return 0;
        }
        js_lex_error(c, "async unsupported");
        c->pending_async = 0;
        return -1;
    }
    if (c->tok == T_LBRACE)
        return parse_block(c);
    if (c->tok == T_VAR || c->tok == T_LET || c->tok == T_CONST) {
        js_lex_next(c);
        if (c->tok != T_IDENT) {
            js_lex_error(c, "name");
            return -1;
        }
        char name[48];
        snprintf(name, sizeof(name), "%s", c->text);
        js_lex_next(c);
        int loc = scope_add(name);
        if (c->tok == T_EQ) {
            js_lex_next(c);
            if (parse_expr(c))
                return -1;
        } else {
            js_emit_op(c, OP_PUSH_UNDEF);
        }
        if (loc >= 0) {
            js_emit_op(c, OP_SET_LOCAL);
            js_emit_u16(c, (uint16_t)loc);
        } else {
            int id = js_intern_str(c, name);
            js_emit_op(c, OP_SET_GLOBAL);
            js_emit_u16(c, (uint16_t)id);
        }
        js_emit_op(c, OP_POP);
        if (c->tok == T_SEMI)
            js_lex_next(c);
        return 0;
    }
    if (c->tok == T_RETURN) {
        js_lex_next(c);
        if (c->tok != T_SEMI && c->tok != T_RBRACE) {
            if (parse_expr(c))
                return -1;
        } else {
            js_emit_op(c, OP_PUSH_UNDEF);
        }
        js_emit_op(c, OP_RET);
        if (c->tok == T_SEMI)
            js_lex_next(c);
        return 0;
    }
    if (c->tok == T_IF) {
        js_lex_next(c);
        if (expect(c, T_LPAREN, "'('"))
            return -1;
        if (parse_expr(c))
            return -1;
        if (expect(c, T_RPAREN, "')'"))
            return -1;
        uint32_t jf = js_emit_here(c);
        js_emit_op(c, OP_JMP_IFN);
        js_emit_i16(c, 0);
        js_emit_op(c, OP_POP);
        if (parse_stmt(c))
            return -1;
        uint32_t je = js_emit_here(c);
        js_emit_op(c, OP_JMP);
        js_emit_i16(c, 0);
        uint32_t else_ip = js_emit_here(c);
        js_emit_patch_i16(c, jf + 1, (int16_t)(else_ip - (jf + 3)));
        /* False path: discard the condition value left by JMP_IFN. */
        js_emit_op(c, OP_POP);
        if (c->tok == T_ELSE) {
            js_lex_next(c);
            if (parse_stmt(c))
                return -1;
        }
        uint32_t end = js_emit_here(c);
        js_emit_patch_i16(c, je + 1, (int16_t)(end - (je + 3)));
        return 0;
    }
    if (c->tok == T_WHILE) {
        js_lex_next(c);
        uint32_t loop = js_emit_here(c);
        if (expect(c, T_LPAREN, "'('"))
            return -1;
        if (parse_expr(c))
            return -1;
        if (expect(c, T_RPAREN, "')'"))
            return -1;
        uint32_t jf = js_emit_here(c);
        js_emit_op(c, OP_JMP_IFN);
        js_emit_i16(c, 0);
        js_emit_op(c, OP_POP);
        if (parse_stmt(c))
            return -1;
        js_emit_op(c, OP_JMP);
        js_emit_i16(c, (int16_t)(loop - (js_emit_here(c) + 2)));
        uint32_t end = js_emit_here(c);
        js_emit_patch_i16(c, jf + 1, (int16_t)(end - (jf + 3)));
        js_emit_op(c, OP_POP);
        return 0;
    }
    if (c->tok == T_FOR) {
        js_lex_next(c);
        if (c->tok == T_AWAIT) {
            js_lex_error(c, "await unsupported");
            return -1;
        }
        if (expect(c, T_LPAREN, "'('"))
            return -1;
        /* for (init; cond; step) */
        if (c->tok != T_SEMI) {
            if (c->tok == T_VAR || c->tok == T_LET || c->tok == T_CONST) {
                if (parse_stmt(c))
                    return -1;
            } else {
                if (parse_expr(c))
                    return -1;
                js_emit_op(c, OP_POP);
                if (c->tok == T_SEMI)
                    js_lex_next(c);
            }
        } else
            js_lex_next(c);
        uint32_t cond = js_emit_here(c);
        if (c->tok != T_SEMI) {
            if (parse_expr(c))
                return -1;
        } else {
            js_emit_op(c, OP_PUSH_TRUE);
        }
        if (expect(c, T_SEMI, "';'"))
            return -1;
        uint32_t jf = js_emit_here(c);
        js_emit_op(c, OP_JMP_IFN);
        js_emit_i16(c, 0);
        js_emit_op(c, OP_POP);
        uint32_t jmp_body = js_emit_here(c);
        js_emit_op(c, OP_JMP);
        js_emit_i16(c, 0);
        uint32_t step = js_emit_here(c);
        if (c->tok != T_RPAREN) {
            if (parse_expr(c))
                return -1;
            js_emit_op(c, OP_POP);
        }
        if (expect(c, T_RPAREN, "')'"))
            return -1;
        js_emit_op(c, OP_JMP);
        js_emit_i16(c, (int16_t)(cond - (js_emit_here(c) + 2)));
        uint32_t body = js_emit_here(c);
        js_emit_patch_i16(c, jmp_body + 1, (int16_t)(body - (jmp_body + 3)));
        if (parse_stmt(c))
            return -1;
        js_emit_op(c, OP_JMP);
        js_emit_i16(c, (int16_t)(step - (js_emit_here(c) + 2)));
        uint32_t end = js_emit_here(c);
        js_emit_patch_i16(c, jf + 1, (int16_t)(end - (jf + 3)));
        js_emit_op(c, OP_POP);
        return 0;
    }
    if (c->tok == T_THROW) {
        js_lex_next(c);
        if (parse_expr(c))
            return -1;
        js_emit_op(c, OP_THROW);
        if (c->tok == T_SEMI)
            js_lex_next(c);
        return 0;
    }
    if (c->tok == T_TRY) {
        js_lex_next(c);
        uint32_t try_push = js_emit_here(c);
        js_emit_op(c, OP_TRY_PUSH);
        js_emit_i16(c, 0);
        if (parse_block(c))
            return -1;
        js_emit_op(c, OP_TRY_POP);
        uint32_t jmp_end = js_emit_here(c);
        js_emit_op(c, OP_JMP);
        js_emit_i16(c, 0);
        uint32_t catch_ip = js_emit_here(c);
        js_emit_patch_i16(c, try_push + 1, (int16_t)(catch_ip - (try_push + 3)));
        if (c->tok == T_CATCH) {
            js_lex_next(c);
            if (c->tok == T_LPAREN) {
                js_lex_next(c);
                if (c->tok == T_IDENT) {
                    char en[48];
                    snprintf(en, sizeof(en), "%s", c->text);
                    js_lex_next(c);
                    int loc = scope_add(en);
                    if (loc >= 0) {
                        js_emit_op(c, OP_SET_LOCAL);
                        js_emit_u16(c, (uint16_t)loc);
                        js_emit_op(c, OP_POP);
                    } else {
                        int id = js_intern_str(c, en);
                        js_emit_op(c, OP_SET_GLOBAL);
                        js_emit_u16(c, (uint16_t)id);
                        js_emit_op(c, OP_POP);
                    }
                }
                if (expect(c, T_RPAREN, "')'"))
                    return -1;
            }
            if (parse_block(c))
                return -1;
        }
        if (c->tok == T_FINALLY) {
            js_lex_next(c);
            if (parse_block(c))
                return -1;
        }
        uint32_t end = js_emit_here(c);
        js_emit_patch_i16(c, jmp_end + 1, (int16_t)(end - (jmp_end + 3)));
        return 0;
    }
    if (c->tok == T_IMPORT) {
        /* import { name } from "mod"; */
        js_lex_next(c);
        if (expect(c, T_LBRACE, "'{'"))
            return -1;
        if (c->tok != T_IDENT) {
            js_lex_error(c, "import name");
            return -1;
        }
        char iname[48];
        snprintf(iname, sizeof(iname), "%s", c->text);
        js_lex_next(c);
        if (expect(c, T_RBRACE, "'}'") || expect(c, T_FROM, "'from'"))
            return -1;
        if (c->tok != T_STR) {
            js_lex_error(c, "module name");
            return -1;
        }
        char mname[48];
        snprintf(mname, sizeof(mname), "%s", c->text);
        js_lex_next(c);
        if (c->tok == T_SEMI)
            js_lex_next(c);
        /* Resolve from rt->modules at runtime via OP_GET_GLOBAL __import hack:
         * push module exports by scanning — use dedicated OP or native.
         * Emit: get bound export as global binding. */
        int found = -1;
        if (c->rt) {
            for (int i = 0; i < 8; i++) {
                if (c->rt->modules[i].used &&
                    !strcmp(c->rt->modules[i].name, mname)) {
                    found = i;
                    break;
                }
            }
        }
        if (found < 0) {
            js_lex_error(c, "module not found");
            return -1;
        }
        /* At compile time bind: load export value onto stack via a temp global
         * set before eval — inject as PUSH of property from stored exports.
         * Use runtime helper: set global name from module exports. */
        {
            struct js_value exp = c->rt->modules[found].exports;
            struct js_value v = js_undef();
            if (exp.type == JT_OBJ && exp.u.o)
                js_obj_get(c->rt, exp.u.o, iname, &v);
            if (js_obj_set(c->rt, c->rt->global, iname, v) != 0) {
                js_lex_error(c, "import bind");
                return -1;
            }
        }
        return 0;
    }
    if (c->tok == T_EXPORT) {
        if (!c->module_mode) {
            js_lex_error(c, "export outside module");
            return -1;
        }
        js_lex_next(c);
        if (c->tok == T_ASYNC) {
            js_lex_next(c);
            c->pending_async = 1;
        }
        if (c->tok == T_FUNCTION) {
            /* Parse function expression; capture name for exports binding. */
            struct lex_save save;
            lex_checkpoint(c, &save);
            js_lex_next(c); /* function */
            char fname[48] = "";
            if (c->tok == T_IDENT) {
                snprintf(fname, sizeof(fname), "%s", c->text);
            }
            lex_restore(c, &save);
            if (parse_expr(c))
                return -1;
            js_emit_op(c, OP_POP);
            if (fname[0] && export_bind(c, fname))
                return -1;
            return 0;
        }
        if (c->tok == T_VAR || c->tok == T_LET || c->tok == T_CONST) {
            js_lex_next(c);
            if (c->tok != T_IDENT) {
                js_lex_error(c, "name");
                return -1;
            }
            char name[48];
            snprintf(name, sizeof(name), "%s", c->text);
            js_lex_next(c);
            if (c->tok == T_EQ) {
                js_lex_next(c);
                if (parse_expr(c))
                    return -1;
            } else {
                js_emit_op(c, OP_PUSH_UNDEF);
            }
            int id = js_intern_str(c, name);
            if (id < 0)
                return -1;
            js_emit_op(c, OP_SET_GLOBAL);
            js_emit_u16(c, (uint16_t)id);
            js_emit_op(c, OP_POP);
            if (c->tok == T_SEMI)
                js_lex_next(c);
            if (export_bind(c, name))
                return -1;
            return 0;
        }
        js_lex_error(c, "export form");
        return -1;
    }
    if (c->tok == T_CLASS) {
        /* class Name { constructor(){} method(){} } — compile as function ctor */
        js_lex_next(c);
        char cname[48] = "Class";
        if (c->tok == T_IDENT) {
            snprintf(cname, sizeof(cname), "%s", c->text);
            js_lex_next(c);
        }
        if (c->tok == T_EXTENDS) {
            js_lex_next(c);
            if (c->tok == T_IDENT)
                js_lex_next(c);
        }
        if (expect(c, T_LBRACE, "'{'"))
            return -1;
        /* emit empty constructor function bound to name */
        uint32_t jmp_over = js_emit_here(c);
        js_emit_op(c, OP_JMP);
        js_emit_i16(c, 0);
        uint32_t body = js_emit_here(c);
        js_emit_op(c, OP_PUSH_UNDEF);
        js_emit_op(c, OP_RET);
        uint32_t after = js_emit_here(c);
        js_emit_patch_i16(c, jmp_over + 1, (int16_t)(after - (jmp_over + 3)));
        if (emit_make_func(c, body, 0, 0, 0))
            return -1;
        int id = js_intern_str(c, cname);
        js_emit_op(c, OP_SET_GLOBAL);
        js_emit_u16(c, (uint16_t)id);
        js_emit_op(c, OP_POP);
        /* skip class body tokens simply */
        int depth = 1;
        while (*c->p && depth) {
            if (c->tok == T_LBRACE)
                depth++;
            else if (c->tok == T_RBRACE) {
                depth--;
                if (!depth)
                    break;
            }
            js_lex_next(c);
        }
        if (c->tok == T_RBRACE)
            js_lex_next(c);
        return 0;
    }
    /* expression statement — keep value when it is the last script statement (REPL). */
    if (parse_expr(c))
        return -1;
    if (c->tok == T_SEMI)
        js_lex_next(c);
    if (c->tok != T_EOF)
        js_emit_op(c, OP_POP);
    return 0;
}


void js_parse_scope_reset(void) {
    scope_sp = 0;
}

int js_parse_stmt(struct js_compiler *c) {
    return parse_stmt(c);
}
