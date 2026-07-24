#include "shell.h"
#include "shell_split.h"
#include "console.h"
#include "elf.h"
#include "util.h"

/*
 * Mutates cmd in place (quote-split writes NULs). Callers must pass a
 * writable buffer — avoids a per-command 256B stack copy.
 */
void shell_execute(char *cmd) {
    if (!cmd)
        return;
    while (*cmd == ' ')
        cmd++;
    if (!*cmd)
        return;

    /* export NAME=val as shorthand — rest is one argv, no re-split */
    if (!strncmp(cmd, "export ", 7)) {
        char *rest = cmd + 7;
        while (*rest == ' ')
            rest++;
        if (!*rest)
            return;
        char *argv[3] = { "export", rest, NULL };
        char path[] = "/bin/export";
        proc_exec(path, 2, argv);
        return;
    }

    char *argv[16];
    int argc = shell_split_args(cmd, argv, 16);
    if (argc < 1)
        return;

    char path[64];
    path[0] = '/';
    path[1] = 'b';
    path[2] = 'i';
    path[3] = 'n';
    path[4] = '/';
    size_t i = 5;
    for (size_t j = 0; argv[0][j] && i + 1 < sizeof(path); j++)
        path[i++] = argv[0][j];
    path[i] = '\0';

    int rc = proc_exec(path, argc, argv);
    if (rc == -999) {
        console_write("Unknown command. Try 'help'.\n");
    }
}
