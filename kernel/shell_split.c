#ifdef PEAK_HOST_TEST
#include "include/shell_split.h"
#include <string.h>
#else
#include "shell_split.h"
#include "util.h"
#endif

/*
 * In-place argv split. Writes NULs into cmd; argv[] points into cmd.
 * No heap / scratch allocation — caller supplies argv storage.
 */
int shell_split_args(char *cmd, char **argv, int max) {
    int argc = 0;
    char *p = cmd;

    if (!cmd || !argv || max < 2)
        return 0;

    while (*p && argc < max - 1) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;

        if (*p == '"' || *p == '\'') {
            char q = *p++;
            argv[argc++] = p;
            while (*p && *p != q)
                p++;
            if (*p)
                *p++ = '\0';
            continue;
        }

        argv[argc++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
    }
    argv[argc] = 0;
    return argc;
}

static void stage_init(struct shell_stage *st) {
    st->argc = 0;
    st->argv[0] = 0;
    st->redir_out.kind = SHELL_REDIR_NONE;
    st->redir_out.path = 0;
    st->redir_in.kind = SHELL_REDIR_NONE;
    st->redir_in.path = 0;
}

static int stage_push_arg(struct shell_stage *st, char *arg) {
    if (st->argc >= SHELL_ARGV_MAX - 1)
        return -1;
    st->argv[st->argc++] = arg;
    st->argv[st->argc] = 0;
    return 0;
}

static int is_op_start(char c) {
    return c == '|' || c == '>' || c == '<';
}

/*
 * Token kinds: 0=word, 1=|, 2=>, 3=>>, 4=<, -1=end
 * When a word abuts an operator (echo>f), the operator char is saved in
 * *pending_op and replaced with NUL so the word is terminated; the next
 * call returns that pending operator first.
 */
static int next_token(char **pp, char **tok, int *pending_op) {
    char *p = *pp;

    if (*pending_op) {
        int k = *pending_op;
        *pending_op = 0;
        return k;
    }

    while (*p == ' ')
        p++;
    if (!*p) {
        *pp = p;
        return -1;
    }

    if (*p == '|') {
        *p = '\0';
        *pp = p + 1;
        return 1;
    }
    if (*p == '<') {
        *p = '\0';
        *pp = p + 1;
        return 4;
    }
    if (*p == '>') {
        char *op = p;
        *op = '\0';
        p++;
        if (*p == '>') {
            *p = '\0';
            *pp = p + 1;
            return 3;
        }
        *pp = p;
        return 2;
    }

    if (*p == '"' || *p == '\'') {
        char q = *p++;
        *tok = p;
        while (*p && *p != q)
            p++;
        if (*p)
            *p++ = '\0';
        *pp = p;
        return 0;
    }

    *tok = p;
    while (*p && *p != ' ' && !is_op_start(*p))
        p++;
    if (*p && is_op_start(*p)) {
        /* Abutting operator: terminate word, remember op for next call. */
        if (*p == '|') {
            *pending_op = 1;
            *p = '\0';
            *pp = p + 1;
        } else if (*p == '<') {
            *pending_op = 4;
            *p = '\0';
            *pp = p + 1;
        } else { /* > or >> */
            char *op = p;
            *op = '\0';
            p++;
            if (*p == '>') {
                *pending_op = 3;
                *p = '\0';
                *pp = p + 1;
            } else {
                *pending_op = 2;
                *pp = p;
            }
        }
        return 0;
    }
    if (*p == ' ')
        *p++ = '\0';
    *pp = p;
    return 0;
}

int shell_parse_pipeline(char *line, struct shell_pipeline *out) {
    if (!line || !out)
        return -1;

    out->nstages = 0;
    stage_init(&out->stages[0]);

    char *p = line;
    struct shell_stage *st = &out->stages[0];
    int pending_redir = 0; /* 2=>, 3=>>, 4=< */
    int pending_op = 0;
    int saw_any = 0;

    for (;;) {
        char *tok = 0;
        int kind = next_token(&p, &tok, &pending_op);
        if (kind < 0)
            break;
        saw_any = 1;

        if (kind == 1) { /* | */
            if (pending_redir || st->argc < 1)
                return -1;
            st->argv[st->argc] = 0;
            out->nstages++;
            if (out->nstages >= SHELL_PIPE_MAX)
                return -1;
            st = &out->stages[out->nstages];
            stage_init(st);
            pending_redir = 0;
            continue;
        }

        if (kind == 2 || kind == 3 || kind == 4) {
            if (pending_redir)
                return -1;
            pending_redir = kind;
            continue;
        }

        /* word */
        if (!tok)
            return -1;

        if (pending_redir) {
            if (pending_redir == 4) {
                if (st->redir_in.kind != SHELL_REDIR_NONE)
                    return -1;
                st->redir_in.kind = SHELL_REDIR_IN;
                st->redir_in.path = tok;
            } else {
                if (st->redir_out.kind != SHELL_REDIR_NONE)
                    return -1;
                st->redir_out.kind =
                    (pending_redir == 3) ? SHELL_REDIR_APPEND : SHELL_REDIR_OUT;
                st->redir_out.path = tok;
            }
            pending_redir = 0;
            continue;
        }

        if (stage_push_arg(st, tok) != 0)
            return -1;
    }

    if (pending_redir || pending_op)
        return -1;
    if (!saw_any || st->argc < 1)
        return -1;
    st->argv[st->argc] = 0;
    out->nstages++;
    return 0;
}
