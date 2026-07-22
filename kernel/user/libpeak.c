#ifdef PEAK_HOST_TEST
#include "../include/libpeak.h"
#include <string.h>
#include <stdio.h>

void peak_puts(const char *s) {
    fputs(s, stdout);
}

void peak_perror(const char *tool, const char *msg) {
    fputs(tool, stdout);
    fputs(": ", stdout);
    fputs(msg, stdout);
    fputs("\n", stdout);
}

void peak_usage(const char *tool, const char *usage) {
    fputs("usage: ", stdout);
    fputs(tool, stdout);
    fputs(" ", stdout);
    fputs(usage, stdout);
    fputs("\n", stdout);
}
#else
#include "libpeak.h"
#include "console.h"
#include "util.h"

void peak_puts(const char *s) {
    console_write(s);
}

void peak_perror(const char *tool, const char *msg) {
    console_write(tool);
    console_write(": ");
    console_write(msg);
    console_write("\n");
}

void peak_usage(const char *tool, const char *usage) {
    console_write("usage: ");
    console_write(tool);
    console_write(" ");
    console_write(usage);
    console_write("\n");
}
#endif

int peak_atoi(const char *s) {
    int v = 0;
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

int peak_has_flag(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], flag))
            return 1;
        /* clustered short flags: -rf */
        if (flag[0] == '-' && flag[1] && !flag[2] && argv[i][0] == '-' && argv[i][1] != '-') {
            for (int j = 1; argv[i][j]; j++)
                if (argv[i][j] == flag[1])
                    return 1;
        }
    }
    return 0;
}

const char *peak_flag_arg(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], flag))
            return argv[i + 1];
    }
    return NULL;
}

int peak_wants_help(int argc, char **argv) {
    return peak_has_flag(argc, argv, "-h") || peak_has_flag(argc, argv, "--help");
}
