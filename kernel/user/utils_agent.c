#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "agent.h"

int upeak_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_write("Peak OS 0.2.0-ai — research workstation\n");
    console_write("Agent tools: fs.read fs.write fs.list fs.exec fs.stat fs.mkdir fs.rm\n");
    console_write("             fs.search sys.info mem.recall audit.tail console.print\n");
    console_write("Try: ask \"summarize workspace\"   ask \"search README\"   audit   memory\n");
    console_write("Desktop: gui → Agent app (approve writes with Y/N)\n");
    return 0;
}

int uask_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("ask", "<prompt...>  (quotes: ask \"create fib.c\"  ask \"run ls .\")");
        return argc < 2 ? 1 : 0;
    }
    /* Quoted prompts arrive as a single argv — skip join copy. */
    if (argc == 2) {
        agent_ask(argv[1]);
        return 0;
    }
    char buf[512];
    peak_join_args(argc, argv, 1, buf, sizeof(buf));
    agent_ask(buf);
    return 0;
}

static void print_session_tail(const char *cmd, const char *path, const char *empty_hint) {
    char buf[2048];
    size_t len = 0;
    if (vfs_read_file(path, buf, sizeof(buf) - 1, &len) != 0 || !len) {
        console_printf("%s: %s\n", cmd, empty_hint);
        return;
    }
    buf[len] = '\0';

    int lines = 0;
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '\n')
            lines++;

    console_printf("%s: %s (%zu bytes, %d lines)\n", cmd, path, len, lines);
    console_write("--- recent entries ---\n");

    const char *start = buf;
    int tail_lines = 0;
    for (const char *p = buf + len; p > buf; p--) {
        if (p[-1] == '\n') {
            tail_lines++;
            if (tail_lines > 12) {
                start = p;
                break;
            }
        }
    }

    int n = 1;
    for (const char *p = start; *p; p++) {
        if (p == start || p[-1] == '\n') {
            const char *eol = strchr(p, '\n');
            size_t ll = eol ? (size_t)(eol - p) : strlen(p);
            if (ll > 0) {
                char line[96];
                size_t show = ll < sizeof(line) - 1 ? ll : sizeof(line) - 1;
                memcpy(line, p, show);
                line[show] = '\0';
                console_printf("%3d  %s\n", n++, line);
            }
        }
    }
    if (n == 1)
        console_write("(no printable lines)\n");
    console_write("--- end ---\n");
}

int uaudit_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_session_tail("audit", "/var/peak/audit.log",
                       "(empty — agent actions appear here after ask)");
    return 0;
}

int umemory_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_session_tail("memory", "/var/peak/sessions/memory.txt",
                       "(empty — ask records turn summaries here)");
    return 0;
}

int upolicy_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[2048];
    size_t len = 0;
    console_write("policy: /etc/peak/agent.policy\n");
    if (vfs_read_file("/etc/peak/agent.policy", buf, sizeof(buf) - 1, &len) != 0) {
        console_write("(empty — defaults: workspace paths + fs.read/write/list/exec/search)\n");
        return 0;
    }
    buf[len] = '\0';
    console_write(buf);
    if (len && buf[len - 1] != '\n')
        console_putc('\n');
    return 0;
}
