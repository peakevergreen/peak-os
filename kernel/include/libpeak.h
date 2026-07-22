#ifndef PEAK_LIBPEAK_H
#define PEAK_LIBPEAK_H

#include "types.h"

void peak_puts(const char *s);
void peak_perror(const char *tool, const char *msg);
void peak_usage(const char *tool, const char *usage);
int  peak_atoi(const char *s);
int  peak_has_flag(int argc, char **argv, const char *flag);
const char *peak_flag_arg(int argc, char **argv, const char *flag); /* value after -n */
int  peak_wants_help(int argc, char **argv);

#endif
