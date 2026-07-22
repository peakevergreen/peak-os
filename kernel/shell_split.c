#ifdef PEAK_HOST_TEST
#include "include/shell_split.h"
#else
#include "shell_split.h"
#endif

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
