#include "js_compile_internal.h"

void js_lex_error(struct js_compiler *c, const char *msg) {
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

void js_lex_next(struct js_compiler *c) {
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
        KW("import", T_IMPORT); KW("export", T_EXPORT); KW("from", T_FROM);
#undef KW
        c->tok = T_IDENT;
        return;
    }
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
        js_lex_error(c, "bad character");
        c->tok = T_EOF;
        break;
    }
}
