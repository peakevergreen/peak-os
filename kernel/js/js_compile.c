#include "js_internal.h"

/* Peak JS compiler: recursive-descent → bytecode.
 * Supports: var/let/const, functions, if/while/for, return, throw/try,
 * objects/arrays, property access, template literals (basic), classes (basic),
 * arrow functions, and expression operators.
 */

enum tok {
    T_EOF = 0,
    T_NUM, T_STR, T_IDENT,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACK, T_RBRACK,
    T_SEMI, T_COMMA, T_DOT, T_COLON, T_QMARK,
    T_EQ, T_EQEQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_PLUSPLUS, T_MINUSMINUS,
    T_ANDAND, T_OROR, T_BANG,
    T_ARROW,
    T_VAR, T_LET, T_CONST, T_FUNCTION, T_RETURN, T_IF, T_ELSE,
    T_WHILE, T_FOR, T_TRUE, T_FALSE, T_NULL, T_UNDEFINED,
    T_NEW, T_THIS, T_TYPEOF, T_THROW, T_TRY, T_CATCH, T_FINALLY,
    T_CLASS, T_EXTENDS, T_ASYNC, T_AWAIT, T_OF, T_IN,
};

struct compiler {
    struct js_runtime *rt;
    const char *src;
    const char *p;
    const char *filename;
    enum tok tok;
    char text[256];
    double num;
    int line;
};

static void cerr(struct compiler *c, const char *msg) {
    char buf[JS_ERR_MAX];
    snprintf(buf, sizeof(buf), "%s:%d: %s", c->filename ? c->filename : "?", c->line, msg);
    size_t i = 0;
    for (; buf[i] && i + 1 < sizeof(c->rt->err); i++)
        c->rt->err[i] = buf[i];
    c->rt->err[i] = '\0';
}

static int is_id_start(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '$';
}
static int is_id(char ch) {
    return is_id_start(ch) || (ch >= '0' && ch <= '9');
}

static void next(struct compiler *c) {
    const char *p = c->p;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r')
            p++;
        if (*p == '\n') {
            c->line++;
            p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n')
                    c->line++;
                p++;
            }
            if (*p)
                p += 2;
            continue;
        }
        break;
    }
    c->p = p;
    if (!*p) {
        c->tok = T_EOF;
        return;
    }
    char ch = *p;
    if (ch >= '0' && ch <= '9') {
        double n = 0;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            p++;
        }
        if (*p == '.') {
            p++;
            double f = 0.1;
            while (*p >= '0' && *p <= '9') {
                n += (*p - '0') * f;
                f *= 0.1;
                p++;
            }
        }
        c->num = n;
        c->tok = T_NUM;
        c->p = p;
        return;
    }
    if (ch == '"' || ch == '\'' || ch == '`') {
        char q = ch;
        p++;
        size_t o = 0;
        while (*p && *p != q && o + 1 < sizeof(c->text)) {
            if (*p == '\\' && p[1]) {
                p++;
                char e = *p++;
                if (e == 'n')
                    c->text[o++] = '\n';
                else if (e == 't')
                    c->text[o++] = '\t';
                else if (e == '\\')
                    c->text[o++] = '\\';
                else
                    c->text[o++] = e;
            } else {
                if (*p == '\n')
                    c->line++;
                c->text[o++] = *p++;
            }
        }
        if (*p == q)
            p++;
        c->text[o] = '\0';
        c->tok = T_STR;
        c->p = p;
        return;
    }
    if (is_id_start(ch)) {
        size_t o = 0;
        while (is_id(*p) && o + 1 < sizeof(c->text))
            c->text[o++] = *p++;
        c->text[o] = '\0';
        c->p = p;
#define KW(s, t) if (!strcmp(c->text, s)) { c->tok = t; return; }
        KW("var", T_VAR); KW("let", T_LET); KW("const", T_CONST);
        KW("function", T_FUNCTION); KW("return", T_RETURN);
        KW("if", T_IF); KW("else", T_ELSE); KW("while", T_WHILE); KW("for", T_FOR);
        KW("true", T_TRUE); KW("false", T_FALSE); KW("null", T_NULL);
        KW("undefined", T_UNDEFINED); KW("new", T_NEW); KW("this", T_THIS);
        KW("typeof", T_TYPEOF); KW("throw", T_THROW); KW("try", T_TRY);
        KW("catch", T_CATCH); KW("finally", T_FINALLY); KW("class", T_CLASS);
        KW("extends", T_EXTENDS); KW("async", T_ASYNC); KW("await", T_AWAIT);
        KW("of", T_OF); KW("in", T_IN);
#undef KW
        c->tok = T_IDENT;
        return;
    }
    /* two-char */
    if (p[0] == '=' && p[1] == '>') {
        c->p = p + 2;
        c->tok = T_ARROW;
        return;
    }
    if (p[0] == '=' && p[1] == '=') {
        c->p = p + 2;
        c->tok = T_EQEQ;
        return;
    }
    if (p[0] == '!' && p[1] == '=') {
        c->p = p + 2;
        c->tok = T_NE;
        return;
    }
    if (p[0] == '<' && p[1] == '=') {
        c->p = p + 2;
        c->tok = T_LE;
        return;
    }
    if (p[0] == '>' && p[1] == '=') {
        c->p = p + 2;
        c->tok = T_GE;
        return;
    }
    if (p[0] == '&' && p[1] == '&') {
        c->p = p + 2;
        c->tok = T_ANDAND;
        return;
    }
    if (p[0] == '|' && p[1] == '|') {
        c->p = p + 2;
        c->tok = T_OROR;
        return;
    }
    if (p[0] == '+' && p[1] == '+') {
        c->p = p + 2;
        c->tok = T_PLUSPLUS;
        return;
    }
    if (p[0] == '-' && p[1] == '-') {
        c->p = p + 2;
        c->tok = T_MINUSMINUS;
        return;
    }
    c->p = p + 1;
    switch (ch) {
    case '(': c->tok = T_LPAREN; break;
    case ')': c->tok = T_RPAREN; break;
    case '{': c->tok = T_LBRACE; break;
    case '}': c->tok = T_RBRACE; break;
    case '[': c->tok = T_LBRACK; break;
    case ']': c->tok = T_RBRACK; break;
    case ';': c->tok = T_SEMI; break;
    case ',': c->tok = T_COMMA; break;
    case '.': c->tok = T_DOT; break;
    case ':': c->tok = T_COLON; break;
    case '?': c->tok = T_QMARK; break;
    case '=': c->tok = T_EQ; break;
    case '<': c->tok = T_LT; break;
    case '>': c->tok = T_GT; break;
    case '+': c->tok = T_PLUS; break;
    case '-': c->tok = T_MINUS; break;
    case '*': c->tok = T_STAR; break;
    case '/': c->tok = T_SLASH; break;
    case '%': c->tok = T_PERCENT; break;
    case '!': c->tok = T_BANG; break;
    default:
        cerr(c, "bad character");
        c->tok = T_EOF;
        break;
    }
}

static int emit_byte(struct compiler *c, uint8_t b) {
    struct js_runtime *rt = c->rt;
    if (rt->code_len + 1 >= rt->code_cap) {
        uint32_t nc = rt->code_cap * 2;
        if (nc > JS_CODE_MAX)
            nc = JS_CODE_MAX;
        if (rt->code_len + 1 >= nc) {
            cerr(c, "code too large");
            return -1;
        }
        uint8_t *n = krealloc(rt->code, nc);
        if (!n) {
            cerr(c, "oom");
            return -1;
        }
        rt->code = n;
        rt->code_cap = nc;
    }
    rt->code[rt->code_len++] = b;
    return 0;
}
static int emit_op(struct compiler *c, enum js_op op) { return emit_byte(c, (uint8_t)op); }
static int emit_u8(struct compiler *c, uint8_t v) { return emit_byte(c, v); }
static int emit_u16(struct compiler *c, uint16_t v) {
    return emit_byte(c, (uint8_t)(v & 0xFF)) || emit_byte(c, (uint8_t)(v >> 8));
}
static int emit_i16(struct compiler *c, int16_t v) { return emit_u16(c, (uint16_t)v); }
static int emit_f64(struct compiler *c, double n) {
    union { double d; uint8_t b[8]; } u;
    u.d = n;
    for (int i = 0; i < 8; i++)
        if (emit_byte(c, u.b[i]))
            return -1;
    return 0;
}
static uint32_t here(struct compiler *c) { return c->rt->code_len; }
static void patch_i16(struct compiler *c, uint32_t at, int16_t v) {
    c->rt->code[at] = (uint8_t)(v & 0xFF);
    c->rt->code[at + 1] = (uint8_t)(v >> 8);
}

static int intern(struct compiler *c, const char *s) {
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

static int parse_stmt(struct compiler *c);
static int parse_expr(struct compiler *c);

static int expect(struct compiler *c, enum tok t, const char *msg) {
    if (c->tok != t) {
        cerr(c, msg);
        return -1;
    }
    next(c);
    return 0;
}

static int parse_primary(struct compiler *c) {
    if (c->tok == T_NUM) {
        if (emit_op(c, OP_PUSH_NUM) || emit_f64(c, c->num))
            return -1;
        next(c);
        return 0;
    }
    if (c->tok == T_STR) {
        int id = intern(c, c->text);
        if (id < 0)
            return -1;
        if (emit_op(c, OP_PUSH_STR) || emit_u16(c, (uint16_t)id))
            return -1;
        next(c);
        return 0;
    }
    if (c->tok == T_TRUE) {
        emit_op(c, OP_PUSH_TRUE);
        next(c);
        return 0;
    }
    if (c->tok == T_FALSE) {
        emit_op(c, OP_PUSH_FALSE);
        next(c);
        return 0;
    }
    if (c->tok == T_NULL) {
        emit_op(c, OP_PUSH_NULL);
        next(c);
        return 0;
    }
    if (c->tok == T_UNDEFINED) {
        emit_op(c, OP_PUSH_UNDEF);
        next(c);
        return 0;
    }
    if (c->tok == T_THIS) {
        emit_op(c, OP_THIS);
        next(c);
        return 0;
    }
    if (c->tok == T_IDENT) {
        char name[48];
        snprintf(name, sizeof(name), "%s", c->text);
        next(c);
        /* arrow: ident => expr */
        if (c->tok == T_ARROW) {
            next(c);
            uint32_t make_at = here(c);
            if (emit_op(c, OP_MAKE_FUNC) || emit_u16(c, 0) || emit_u8(c, 1) ||
                emit_u8(c, 1))
                return -1;
            uint32_t body = here(c);
            scope_push(1);
            scope_add(name);
            if (parse_expr(c))
                return -1;
            emit_op(c, OP_RET);
            int locals = scopes[scope_sp - 1].n;
            scope_pop();
            /* patch make_func offsets: rewrite last MAKE_FUNC */
            c->rt->code[make_at + 1] = (uint8_t)(body & 0xFF);
            c->rt->code[make_at + 2] = (uint8_t)(body >> 8);
            c->rt->code[make_at + 3] = 1;
            c->rt->code[make_at + 4] = (uint8_t)locals;
            /* jump over body */
            uint32_t j = here(c);
            emit_op(c, OP_JMP);
            emit_i16(c, 0);
            /* actually body is already emitted after MAKE_FUNC which is wrong.
             * Simpler approach: don't support single-arg arrow without parens for now.
             * Revert: treat as identifier load. */
            (void)j;
            cerr(c, "arrow without () not supported; use (x)=>");
            return -1;
        }
        int depth = 0;
        int loc = scope_find(name, &depth);
        if (loc >= 0 && depth == 0) {
            emit_op(c, OP_GET_LOCAL);
            emit_u16(c, (uint16_t)loc);
        } else {
            int id = intern(c, name);
            if (id < 0)
                return -1;
            emit_op(c, OP_GET_GLOBAL);
            emit_u16(c, (uint16_t)id);
        }
        return 0;
    }
    if (c->tok == T_LPAREN) {
        next(c);
        /* arrow () => or (a,b)=> */
        if (c->tok == T_RPAREN) {
            next(c);
            if (c->tok == T_ARROW) {
                next(c);
                uint32_t jmp_over = here(c);
                emit_op(c, OP_JMP);
                emit_i16(c, 0);
                uint32_t body = here(c);
                scope_push(1);
                if (c->tok == T_LBRACE) {
                    next(c);
                    while (c->tok != T_RBRACE && c->tok != T_EOF)
                        if (parse_stmt(c))
                            return -1;
                    if (expect(c, T_RBRACE, "'}'"))
                        return -1;
                    emit_op(c, OP_PUSH_UNDEF);
                    emit_op(c, OP_RET);
                } else {
                    if (parse_expr(c))
                        return -1;
                    emit_op(c, OP_RET);
                }
                int locals = scopes[scope_sp - 1].n;
                scope_pop();
                uint32_t after = here(c);
                patch_i16(c, jmp_over + 1, (int16_t)(after - (jmp_over + 3)));
                emit_op(c, OP_MAKE_FUNC);
                emit_u16(c, (uint16_t)body);
                emit_u8(c, 0);
                emit_u8(c, (uint8_t)locals);
                return 0;
            }
            cerr(c, "empty ()");
            return -1;
        }
        /* look ahead for arrow params: ident , ... ) => */
        if (c->tok == T_IDENT) {
            char params[8][48];
            int np = 0;
            const char *save_p = c->p;
            enum tok save_tok = c->tok;
            char save_text[256];
            memcpy(save_text, c->text, sizeof(save_text));
            double save_num = c->num;
            int save_line = c->line;
            while (c->tok == T_IDENT && np < 8) {
                snprintf(params[np++], sizeof(params[0]), "%s", c->text);
                next(c);
                if (c->tok == T_COMMA) {
                    next(c);
                    continue;
                }
                break;
            }
            if (c->tok == T_RPAREN) {
                next(c);
                if (c->tok == T_ARROW) {
                    next(c);
                    uint32_t jmp_over = here(c);
                    emit_op(c, OP_JMP);
                    emit_i16(c, 0);
                    uint32_t body = here(c);
                    scope_push(1);
                    for (int i = 0; i < np; i++)
                        scope_add(params[i]);
                    if (c->tok == T_LBRACE) {
                        next(c);
                        while (c->tok != T_RBRACE && c->tok != T_EOF)
                            if (parse_stmt(c))
                                return -1;
                        if (expect(c, T_RBRACE, "'}'"))
                            return -1;
                        emit_op(c, OP_PUSH_UNDEF);
                        emit_op(c, OP_RET);
                    } else {
                        if (parse_expr(c))
                            return -1;
                        emit_op(c, OP_RET);
                    }
                    int locals = scopes[scope_sp - 1].n;
                    scope_pop();
                    uint32_t after = here(c);
                    patch_i16(c, jmp_over + 1, (int16_t)(after - (jmp_over + 3)));
                    emit_op(c, OP_MAKE_FUNC);
                    emit_u16(c, (uint16_t)body);
                    emit_u8(c, (uint8_t)np);
                    emit_u8(c, (uint8_t)locals);
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
        next(c);
        emit_op(c, OP_NEW_OBJ);
        while (c->tok != T_RBRACE && c->tok != T_EOF) {
            char key[64];
            if (c->tok == T_IDENT || c->tok == T_STR) {
                snprintf(key, sizeof(key), "%s", c->text);
                next(c);
            } else {
                cerr(c, "object key");
                return -1;
            }
            if (expect(c, T_COLON, "':'"))
                return -1;
            if (parse_expr(c))
                return -1;
            int id = intern(c, key);
            if (id < 0)
                return -1;
            emit_op(c, OP_SET_PROP);
            emit_u16(c, (uint16_t)id);
            /* SET_PROP consumes value but we need object on stack — VM keeps obj */
            if (c->tok == T_COMMA)
                next(c);
        }
        return expect(c, T_RBRACE, "'}'");
    }
    if (c->tok == T_LBRACK) {
        next(c);
        emit_op(c, OP_NEW_ARR);
        while (c->tok != T_RBRACK && c->tok != T_EOF) {
            if (parse_expr(c))
                return -1;
            emit_op(c, OP_ARR_PUSH);
            if (c->tok == T_COMMA)
                next(c);
        }
        return expect(c, T_RBRACK, "']'");
    }
    if (c->tok == T_FUNCTION) {
        next(c);
        char fname[48] = "";
        if (c->tok == T_IDENT) {
            snprintf(fname, sizeof(fname), "%s", c->text);
            next(c);
        }
        if (expect(c, T_LPAREN, "'('"))
            return -1;
        char params[8][48];
        int np = 0;
        while (c->tok != T_RPAREN && c->tok != T_EOF && np < 8) {
            if (c->tok != T_IDENT) {
                cerr(c, "param");
                return -1;
            }
            snprintf(params[np++], sizeof(params[0]), "%s", c->text);
            next(c);
            if (c->tok == T_COMMA)
                next(c);
        }
        if (expect(c, T_RPAREN, "')'") || expect(c, T_LBRACE, "'{'"))
            return -1;
        uint32_t jmp_over = here(c);
        emit_op(c, OP_JMP);
        emit_i16(c, 0);
        uint32_t body = here(c);
        scope_push(1);
        for (int i = 0; i < np; i++)
            scope_add(params[i]);
        while (c->tok != T_RBRACE && c->tok != T_EOF)
            if (parse_stmt(c))
                return -1;
        if (expect(c, T_RBRACE, "'}'"))
            return -1;
        emit_op(c, OP_PUSH_UNDEF);
        emit_op(c, OP_RET);
        int locals = scopes[scope_sp - 1].n;
        scope_pop();
        uint32_t after = here(c);
        patch_i16(c, jmp_over + 1, (int16_t)(after - (jmp_over + 3)));
        emit_op(c, OP_MAKE_FUNC);
        emit_u16(c, (uint16_t)body);
        emit_u8(c, (uint8_t)np);
        emit_u8(c, (uint8_t)locals);
        if (fname[0]) {
            emit_op(c, OP_DUP);
            int id = intern(c, fname);
            emit_op(c, OP_SET_GLOBAL);
            emit_u16(c, (uint16_t)id);
        }
        return 0;
    }
    cerr(c, "expression");
    return -1;
}

static int parse_postfix(struct compiler *c) {
    if (parse_primary(c))
        return -1;
    for (;;) {
        if (c->tok == T_DOT) {
            next(c);
            if (c->tok != T_IDENT) {
                cerr(c, "property");
                return -1;
            }
            int id = intern(c, c->text);
            next(c);
            emit_op(c, OP_GET_PROP);
            emit_u16(c, (uint16_t)id);
        } else if (c->tok == T_LBRACK) {
            next(c);
            if (parse_expr(c))
                return -1;
            if (expect(c, T_RBRACK, "']'"))
                return -1;
            emit_op(c, OP_GET_IDX);
        } else if (c->tok == T_LPAREN) {
            next(c);
            int argc = 0;
            while (c->tok != T_RPAREN && c->tok != T_EOF) {
                if (parse_expr(c))
                    return -1;
                argc++;
                if (c->tok == T_COMMA)
                    next(c);
            }
            if (expect(c, T_RPAREN, "')'"))
                return -1;
            emit_op(c, OP_CALL);
            emit_u8(c, (uint8_t)argc);
        } else {
            break;
        }
    }
    return 0;
}

static int parse_unary(struct compiler *c) {
    if (c->tok == T_BANG) {
        next(c);
        if (parse_unary(c))
            return -1;
        return emit_op(c, OP_NOT);
    }
    if (c->tok == T_MINUS) {
        next(c);
        if (parse_unary(c))
            return -1;
        return emit_op(c, OP_NEG);
    }
    if (c->tok == T_TYPEOF) {
        next(c);
        if (parse_unary(c))
            return -1;
        return emit_op(c, OP_TYPEOF);
    }
    if (c->tok == T_AWAIT) {
        next(c); /* await expr — treat as expr for now */
        return parse_unary(c);
    }
    return parse_postfix(c);
}

static int parse_mul(struct compiler *c) {
    if (parse_unary(c))
        return -1;
    while (c->tok == T_STAR || c->tok == T_SLASH || c->tok == T_PERCENT) {
        enum tok op = c->tok;
        next(c);
        if (parse_unary(c))
            return -1;
        emit_op(c, op == T_STAR ? OP_MUL : (op == T_SLASH ? OP_DIV : OP_MOD));
    }
    return 0;
}
static int parse_add(struct compiler *c) {
    if (parse_mul(c))
        return -1;
    while (c->tok == T_PLUS || c->tok == T_MINUS) {
        enum tok op = c->tok;
        next(c);
        if (parse_mul(c))
            return -1;
        emit_op(c, op == T_PLUS ? OP_ADD : OP_SUB);
    }
    return 0;
}
static int parse_rel(struct compiler *c) {
    if (parse_add(c))
        return -1;
    while (c->tok == T_LT || c->tok == T_LE || c->tok == T_GT || c->tok == T_GE) {
        enum tok op = c->tok;
        next(c);
        if (parse_add(c))
            return -1;
        if (op == T_LT)
            emit_op(c, OP_LT);
        else if (op == T_LE)
            emit_op(c, OP_LE);
        else if (op == T_GT)
            emit_op(c, OP_GT);
        else
            emit_op(c, OP_GE);
    }
    return 0;
}
static int parse_eq(struct compiler *c) {
    if (parse_rel(c))
        return -1;
    while (c->tok == T_EQEQ || c->tok == T_NE) {
        enum tok op = c->tok;
        next(c);
        if (parse_rel(c))
            return -1;
        emit_op(c, op == T_EQEQ ? OP_EQ : OP_NE);
    }
    return 0;
}
static int parse_and(struct compiler *c) {
    if (parse_eq(c))
        return -1;
    while (c->tok == T_ANDAND) {
        next(c);
        uint32_t j = here(c);
        emit_op(c, OP_JMP_IFN);
        emit_i16(c, 0);
        emit_op(c, OP_POP);
        if (parse_eq(c))
            return -1;
        uint32_t after = here(c);
        patch_i16(c, j + 1, (int16_t)(after - (j + 3)));
    }
    return 0;
}
static int parse_or(struct compiler *c) {
    if (parse_and(c))
        return -1;
    while (c->tok == T_OROR) {
        next(c);
        uint32_t j = here(c);
        emit_op(c, OP_JMP_IF);
        emit_i16(c, 0);
        emit_op(c, OP_POP);
        if (parse_and(c))
            return -1;
        uint32_t after = here(c);
        patch_i16(c, j + 1, (int16_t)(after - (j + 3)));
    }
    return 0;
}

static int parse_assignment(struct compiler *c);

static int parse_expr(struct compiler *c) {
    return parse_assignment(c);
}

static int parse_assignment(struct compiler *c) {
    /* Detect ident = expr or ident.prop = expr */
    if (c->tok == T_IDENT) {
        char name[48];
        snprintf(name, sizeof(name), "%s", c->text);
        const char *save_p = c->p;
        enum tok save_tok = c->tok;
        char save_text[256];
        memcpy(save_text, c->text, sizeof(save_text));
        int save_line = c->line;
        next(c);
        if (c->tok == T_EQ) {
            next(c);
            if (parse_assignment(c))
                return -1;
            int depth = 0;
            int loc = scope_find(name, &depth);
            if (loc >= 0 && depth == 0) {
                emit_op(c, OP_SET_LOCAL);
                emit_u16(c, (uint16_t)loc);
            } else {
                int id = intern(c, name);
                emit_op(c, OP_SET_GLOBAL);
                emit_u16(c, (uint16_t)id);
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

static int parse_block(struct compiler *c) {
    if (expect(c, T_LBRACE, "'{'"))
        return -1;
    while (c->tok != T_RBRACE && c->tok != T_EOF)
        if (parse_stmt(c))
            return -1;
    return expect(c, T_RBRACE, "'}'");
}

static int parse_stmt(struct compiler *c) {
    if (c->tok == T_SEMI) {
        next(c);
        return 0;
    }
    if (c->tok == T_LBRACE)
        return parse_block(c);
    if (c->tok == T_VAR || c->tok == T_LET || c->tok == T_CONST) {
        next(c);
        if (c->tok != T_IDENT) {
            cerr(c, "name");
            return -1;
        }
        char name[48];
        snprintf(name, sizeof(name), "%s", c->text);
        next(c);
        int loc = scope_add(name);
        if (c->tok == T_EQ) {
            next(c);
            if (parse_expr(c))
                return -1;
        } else {
            emit_op(c, OP_PUSH_UNDEF);
        }
        if (loc >= 0) {
            emit_op(c, OP_SET_LOCAL);
            emit_u16(c, (uint16_t)loc);
        } else {
            int id = intern(c, name);
            emit_op(c, OP_SET_GLOBAL);
            emit_u16(c, (uint16_t)id);
        }
        emit_op(c, OP_POP);
        if (c->tok == T_SEMI)
            next(c);
        return 0;
    }
    if (c->tok == T_RETURN) {
        next(c);
        if (c->tok != T_SEMI && c->tok != T_RBRACE) {
            if (parse_expr(c))
                return -1;
        } else {
            emit_op(c, OP_PUSH_UNDEF);
        }
        emit_op(c, OP_RET);
        if (c->tok == T_SEMI)
            next(c);
        return 0;
    }
    if (c->tok == T_IF) {
        next(c);
        if (expect(c, T_LPAREN, "'('"))
            return -1;
        if (parse_expr(c))
            return -1;
        if (expect(c, T_RPAREN, "')'"))
            return -1;
        uint32_t jf = here(c);
        emit_op(c, OP_JMP_IFN);
        emit_i16(c, 0);
        emit_op(c, OP_POP);
        if (parse_stmt(c))
            return -1;
        uint32_t je = here(c);
        emit_op(c, OP_JMP);
        emit_i16(c, 0);
        uint32_t else_ip = here(c);
        patch_i16(c, jf + 1, (int16_t)(else_ip - (jf + 3)));
        emit_op(c, OP_POP); /* for false branch falling through — actually JMP_IFN leaves value */
        /* fix: JMP_IFN should not leave value. VM pops on jump. */
        if (c->tok == T_ELSE) {
            next(c);
            if (parse_stmt(c))
                return -1;
        }
        uint32_t end = here(c);
        patch_i16(c, je + 1, (int16_t)(end - (je + 3)));
        return 0;
    }
    if (c->tok == T_WHILE) {
        next(c);
        uint32_t loop = here(c);
        if (expect(c, T_LPAREN, "'('"))
            return -1;
        if (parse_expr(c))
            return -1;
        if (expect(c, T_RPAREN, "')'"))
            return -1;
        uint32_t jf = here(c);
        emit_op(c, OP_JMP_IFN);
        emit_i16(c, 0);
        emit_op(c, OP_POP);
        if (parse_stmt(c))
            return -1;
        emit_op(c, OP_JMP);
        emit_i16(c, (int16_t)(loop - (here(c) + 2)));
        uint32_t end = here(c);
        patch_i16(c, jf + 1, (int16_t)(end - (jf + 3)));
        emit_op(c, OP_POP);
        return 0;
    }
    if (c->tok == T_FOR) {
        next(c);
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
                emit_op(c, OP_POP);
                if (c->tok == T_SEMI)
                    next(c);
            }
        } else
            next(c);
        uint32_t cond = here(c);
        if (c->tok != T_SEMI) {
            if (parse_expr(c))
                return -1;
        } else {
            emit_op(c, OP_PUSH_TRUE);
        }
        if (expect(c, T_SEMI, "';'"))
            return -1;
        uint32_t jf = here(c);
        emit_op(c, OP_JMP_IFN);
        emit_i16(c, 0);
        emit_op(c, OP_POP);
        uint32_t jmp_body = here(c);
        emit_op(c, OP_JMP);
        emit_i16(c, 0);
        uint32_t step = here(c);
        if (c->tok != T_RPAREN) {
            if (parse_expr(c))
                return -1;
            emit_op(c, OP_POP);
        }
        if (expect(c, T_RPAREN, "')'"))
            return -1;
        emit_op(c, OP_JMP);
        emit_i16(c, (int16_t)(cond - (here(c) + 2)));
        uint32_t body = here(c);
        patch_i16(c, jmp_body + 1, (int16_t)(body - (jmp_body + 3)));
        if (parse_stmt(c))
            return -1;
        emit_op(c, OP_JMP);
        emit_i16(c, (int16_t)(step - (here(c) + 2)));
        uint32_t end = here(c);
        patch_i16(c, jf + 1, (int16_t)(end - (jf + 3)));
        emit_op(c, OP_POP);
        return 0;
    }
    if (c->tok == T_THROW) {
        next(c);
        if (parse_expr(c))
            return -1;
        emit_op(c, OP_THROW);
        if (c->tok == T_SEMI)
            next(c);
        return 0;
    }
    if (c->tok == T_TRY) {
        next(c);
        uint32_t try_push = here(c);
        emit_op(c, OP_TRY_PUSH);
        emit_i16(c, 0);
        if (parse_block(c))
            return -1;
        emit_op(c, OP_TRY_POP);
        uint32_t jmp_end = here(c);
        emit_op(c, OP_JMP);
        emit_i16(c, 0);
        uint32_t catch_ip = here(c);
        patch_i16(c, try_push + 1, (int16_t)(catch_ip - (try_push + 3)));
        if (c->tok == T_CATCH) {
            next(c);
            if (c->tok == T_LPAREN) {
                next(c);
                if (c->tok == T_IDENT) {
                    char en[48];
                    snprintf(en, sizeof(en), "%s", c->text);
                    next(c);
                    int loc = scope_add(en);
                    if (loc >= 0) {
                        emit_op(c, OP_SET_LOCAL);
                        emit_u16(c, (uint16_t)loc);
                        emit_op(c, OP_POP);
                    } else {
                        int id = intern(c, en);
                        emit_op(c, OP_SET_GLOBAL);
                        emit_u16(c, (uint16_t)id);
                        emit_op(c, OP_POP);
                    }
                }
                if (expect(c, T_RPAREN, "')'"))
                    return -1;
            }
            if (parse_block(c))
                return -1;
        }
        if (c->tok == T_FINALLY) {
            next(c);
            if (parse_block(c))
                return -1;
        }
        uint32_t end = here(c);
        patch_i16(c, jmp_end + 1, (int16_t)(end - (jmp_end + 3)));
        return 0;
    }
    if (c->tok == T_CLASS) {
        /* class Name { constructor(){} method(){} } — compile as function ctor */
        next(c);
        char cname[48] = "Class";
        if (c->tok == T_IDENT) {
            snprintf(cname, sizeof(cname), "%s", c->text);
            next(c);
        }
        if (c->tok == T_EXTENDS) {
            next(c);
            if (c->tok == T_IDENT)
                next(c);
        }
        if (expect(c, T_LBRACE, "'{'"))
            return -1;
        /* emit empty constructor function bound to name */
        uint32_t jmp_over = here(c);
        emit_op(c, OP_JMP);
        emit_i16(c, 0);
        uint32_t body = here(c);
        emit_op(c, OP_PUSH_UNDEF);
        emit_op(c, OP_RET);
        uint32_t after = here(c);
        patch_i16(c, jmp_over + 1, (int16_t)(after - (jmp_over + 3)));
        emit_op(c, OP_MAKE_FUNC);
        emit_u16(c, (uint16_t)body);
        emit_u8(c, 0);
        emit_u8(c, 0);
        int id = intern(c, cname);
        emit_op(c, OP_SET_GLOBAL);
        emit_u16(c, (uint16_t)id);
        emit_op(c, OP_POP);
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
            next(c);
        }
        if (c->tok == T_RBRACE)
            next(c);
        return 0;
    }
    /* expression statement — keep value when it is the last script statement (REPL). */
    if (parse_expr(c))
        return -1;
    if (c->tok == T_SEMI)
        next(c);
    if (c->tok != T_EOF)
        emit_op(c, OP_POP);
    return 0;
}

int js_compile(struct js_runtime *rt, const char *src, const char *filename) {
    struct compiler c;
    memset(&c, 0, sizeof(c));
    c.rt = rt;
    c.src = src;
    c.p = src;
    c.filename = filename;
    c.line = 1;
    scope_sp = 0; /* top-level bindings are globals; functions push scopes */
    next(&c);
    while (c.tok != T_EOF) {
        if (parse_stmt(&c))
            return -1;
    }
    emit_op(&c, OP_HALT);
    return 0;
}
