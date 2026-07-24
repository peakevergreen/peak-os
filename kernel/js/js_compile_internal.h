#ifndef PEAK_JS_COMPILE_INTERNAL_H
#define PEAK_JS_COMPILE_INTERNAL_H

#include "js_internal.h"

/* Token kinds produced by the lexer. */
enum js_tok {
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
    T_IMPORT, T_EXPORT, T_FROM,
};

struct js_compiler {
    struct js_runtime *rt;
    const char *src;
    const char *p;
    const char *filename;
    enum js_tok tok;
    char text[256];
    double num;
    int line;
    int pending_async; /* set when `async` precedes function/arrow */
    int module_mode;   /* allow export; bind exports object */
};

/* Lexer (js_lex.c) */
void js_lex_error(struct js_compiler *c, const char *msg);
void js_lex_next(struct js_compiler *c);

/* Codegen (js_codegen.c) */
int js_emit_op(struct js_compiler *c, enum js_op op);
int js_emit_u8(struct js_compiler *c, uint8_t v);
int js_emit_u16(struct js_compiler *c, uint16_t v);
int js_emit_i16(struct js_compiler *c, int16_t v);
int js_emit_f64(struct js_compiler *c, double n);
uint32_t js_emit_here(struct js_compiler *c);
void js_emit_patch_i16(struct js_compiler *c, uint32_t at, int16_t v);
int js_intern_str(struct js_compiler *c, const char *s);

/* Parser (js_parse.c) */
void js_parse_scope_reset(void);
int js_parse_stmt(struct js_compiler *c);

#endif
