#ifndef PEAK_SHELL_SPLIT_H
#define PEAK_SHELL_SPLIT_H

#ifdef PEAK_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#else
#include "types.h"
#endif

/*
 * In-place argv split (no heap). Supports "double" and 'single' quotes
 * (quotes stripped). Unclosed quote: remainder of the string is one argument.
 * Writes NULs into cmd; argv entries alias cmd. Returns argc; argv[argc] is
 * NULL. At most max-1 args.
 */
int shell_split_args(char *cmd, char **argv, int max);

/* Pipeline / redirect parse limits (host + guest). */
#define SHELL_PIPE_MAX     4
#define SHELL_ARGV_MAX     16
#define SHELL_CAPTURE_MAX  8192

enum shell_redir_kind {
    SHELL_REDIR_NONE = 0,
    SHELL_REDIR_OUT,   /* >  */
    SHELL_REDIR_APPEND,/* >> */
    SHELL_REDIR_IN,    /* <  */
};

struct shell_redir {
    enum shell_redir_kind kind;
    char *path; /* aliases into mutated line buffer */
};

struct shell_stage {
    char *argv[SHELL_ARGV_MAX];
    int argc;
    struct shell_redir redir_out; /* > or >> (at most one) */
    struct shell_redir redir_in;  /* < (at most one) */
};

struct shell_pipeline {
    struct shell_stage stages[SHELL_PIPE_MAX];
    int nstages;
};

/*
 * Parse a full command line into up to SHELL_PIPE_MAX stages separated by `|`.
 * Operators `|`, `>`, `>>`, `<` are recognized only outside quotes.
 * Mutates `line` in place (NULs). Returns 0 on success, -1 on parse error
 * (too many stages/args, bare operator, conflicting redirects).
 */
int shell_parse_pipeline(char *line, struct shell_pipeline *out);

#endif