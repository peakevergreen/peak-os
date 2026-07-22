#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "agent.h"

int upeak_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_write("Peak OS 0.2.0-ai — research workstation\n");
    console_write("Commands: help, theme, ask, gui, ctr\n");
    console_write("Agent: in-guest mock planner (ask)\n");
    return 0;
}

int uask_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("ask", "<prompt...>  (quotes: ask \"create fib.c\")");
        return argc < 2 ? 1 : 0;
    }
    char buf[512];
    size_t o = 0;
    for (int i = 1; i < argc && o + 1 < sizeof(buf); i++) {
        if (i > 1 && o < sizeof(buf) - 1)
            buf[o++] = ' ';
        for (const char *p = argv[i]; *p && o + 1 < sizeof(buf); p++)
            buf[o++] = *p;
    }
    buf[o] = 0;
    agent_ask(buf);
    return 0;
}

int uaudit_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[2048];
    size_t len = 0;
    if (vfs_read_file("/var/peak/audit.log", buf, sizeof(buf) - 1, &len) != 0)
        console_write("(empty)\n");
    else {
        buf[len] = '\0';
        console_write(buf[0] ? buf : "(empty)\n");
        if (len && buf[len - 1] != '\n')
            console_putc('\n');
    }
    return 0;
}

int umemory_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[2048];
    size_t len = 0;
    if (vfs_read_file("/var/peak/sessions/memory.txt", buf, sizeof(buf) - 1, &len) != 0)
        console_write("(empty)\n");
    else {
        buf[len] = '\0';
        console_write(buf[0] ? buf : "(empty)\n");
        if (len && buf[len - 1] != '\n')
            console_putc('\n');
    }
    return 0;
}

int upolicy_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[2048];
    size_t len = 0;
    if (vfs_read_file("/etc/peak/agent.policy", buf, sizeof(buf) - 1, &len) != 0)
        console_write("(empty)\n");
    else {
        buf[len] = '\0';
        console_write(buf);
        if (len && buf[len - 1] != '\n')
            console_putc('\n');
    }
    return 0;
}
