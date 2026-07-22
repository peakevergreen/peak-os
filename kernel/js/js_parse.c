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

static int expect(struct js_compiler *c, enum js_tok t, const char *msg) {
    if (c->tok != t) {
        js_lex_error(c, msg);
        return -1;
    }
    js_lex_next(c);
    return 0;
}

static int parse_primary(struct js_compiler *c) {
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
        /* arrow: ident => expr */
        if (c->tok == T_ARROW) {
            js_lex_next(c);
            uint32_t make_at = js_emit_here(c);
            if (js_emit_op(c, OP_MAKE_FUNC) || js_emit_u16(c, 0) || js_emit_u8(c, 1) ||
                js_emit_u8(c, 1))
                return -1;
            uint32_t body = js_emit_here(c);
            scope_push(1);
            scope_add(name);
            if (parse_expr(c))
                return -1;
            js_emit_op(c, OP_RET);
            int locals = scopes[scope_sp - 1].n;
            scope_pop();
            /* patch make_func offsets: rewrite last MAKE_FUNC */
            c->rt->code[make_at + 1] = (uint8_t)(body & 0xFF);
            c->rt->code[make_at + 2] = (uint8_t)(body >> 8);
            c->rt->code[make_at + 3] = 1;
            c->rt->code[make_at + 4] = (uint8_t)locals;
            /* jump over body */
            uint32_t j = js_emit_here(c);
            js_emit_op(c, OP_JMP);
            js_emit_i16(c, 0);
            /* actually body is already emitted after MAKE_FUNC which is wrong.
             * Simpler approach: don't support single-arg arrow without parens for now.
             * Revert: treat as identifier load. */
            (void)j;
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
                js_emit_op(c, OP_MAKE_FUNC);
                js_emit_u16(c, (uint16_t)body);
                js_emit_u8(c, 0);
                js_emit_u8(c, (uint8_t)locals);
                return 0;
            }
            js_lex_error(c, "empty ()");
            return -1;
        }
        /* look ahead for arrow params: ident , ... ) => */
        if (c->tok == T_IDENT) {
            char params[8][48];
            int np = 0;
            const char *save_p = c->p;
            enum js_tok save_tok = c->tok;
            char save_text[256];
            memcpy(save_text, c->text, sizeof(save_text));
            double save_num = c->num;
            int save_line = c->line;
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
                    js_emit_op(c, OP_MAKE_FUNC);
                    js_emit_u16(c, (uint16_t)body);
                    js_emit_u8(c, (uint8_t)np);
                    js_emit_u8(c, (uint8_t)locals);
                    return 0;
                }
            }
            /* restore and parse as grouped expr */
            c->p = save_p;
            c->tok = save_tok;
            memcpy(c->text, save_text, sizeof(c->text));
            c->num = save_num;
            c->line = save_line;
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
        js_emit_op(c, OP_MAKE_FUNC);
        js_emit_u16(c, (uint16_t)body);
        js_emit_u8(c, (uint8_t)np);
        js_emit_u8(c, (uint8_t)locals);
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
        js_lex_next(c); /* await expr — treat as expr for now */
        return parse_unary(c);
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
    if (parse_add(c))
        return -1;
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
    /* Detect ident = expr or ident.prop = expr */
    if (c->tok == T_IDENT) {
        char name[48];
        snprintf(name, sizeof(name), "%s", c->text);
        const char *save_p = c->p;
        enum js_tok save_tok = c->tok;
        char save_text[256];
        memcpy(save_text, c->text, sizeof(save_text));
        int save_line = c->line;
        js_lex_next(c);
        if (c->tok == T_EQ) {
            js_lex_next(c);
            if (parse_assignment(c))
                return -1;
            int depth = 0;
            int loc = scope_find(name, &depth);
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
        /* restore */
        c->p = save_p;
        c->tok = save_tok;
        memcpy(c->text, save_text, sizeof(c->text));
        c->line = save_line;
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
        js_emit_op(c, OP_POP); /* for false branch falling through — actually JMP_IFN leaves value */
        /* fix: JMP_IFN should not leave value. VM pops on jump. */
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
        js_emit_op(c, OP_MAKE_FUNC);
        js_emit_u16(c, (uint16_t)body);
        js_emit_u8(c, 0);
        js_emit_u8(c, 0);
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
