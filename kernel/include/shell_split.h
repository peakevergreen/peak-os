#ifndef PEAK_SHELL_SPLIT_H
#define PEAK_SHELL_SPLIT_H

#ifdef PEAK_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#else
#include "types.h"
#endif

/*
 * In-place argv split. Supports "double" and 'single' quotes (quotes stripped).
 * Unclosed quote: remainder of the string is one argument.
 * Returns argc; argv[argc] is set to NULL. At most max-1 args.
 */
int shell_split_args(char *cmd, char **argv, int max);

#endif
