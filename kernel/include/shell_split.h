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

#endif
