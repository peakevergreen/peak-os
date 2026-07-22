#include "shell.h"
#include "shell_split.h"
#include "console.h"
#include "elf.h"
#include "util.h"

void shell_execute(const char *cmd_in) {
    char cmdbuf[256];
    size_t n = 0;
    while (cmd_in[n] && n + 1 < sizeof(cmdbuf)) {
        cmdbuf[n] = cmd_in[n];
        n++;
    }
    cmdbuf[n] = '\0';
    char *cmd = cmdbuf;
    while (*cmd == ' ')
        cmd++;
    if (!*cmd)
        return;

    /* export NAME=val as shorthand */
    if (!strncmp(cmd, "export ", 7)) {
        char *rest = cmd + 7;
        while (*rest == ' ')
            rest++;
        char *argv[4] = { "export", rest, NULL };
        char path[64] = "/bin/export";
        proc_exec(path, 2, argv);
        return;
    }

    char *argv[16];
    int argc = shell_split_args(cmd, argv, 16);
    if (argc < 1)
        return;

    char path[64] = "/bin/";
    size_t i = 5;
    for (size_t j = 0; argv[0][j] && i + 1 < sizeof(path); j++)
        path[i++] = argv[0][j];
    path[i] = '\0';

    int rc = proc_exec(path, argc, argv);
    if (rc == -999) {
        console_write("Unknown command. Try 'help'.\n");
    }
}
