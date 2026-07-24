#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "agent.h"

int upeak_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_write("Peak OS 0.2.0-ai — research workstation\n");
    console_write("Agent tools: fs.read fs.write fs.list fs.exec console.print\n");
    console_write("Try: ask \"summarize workspace\"   audit   memory   man ls\n");
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

int uaudit_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[2048];
    size_t len = 0;
    if (vfs_read_file("/var/peak/audit.log", buf, sizeof(buf) - 1, &len) != 0) {
        console_write("audit: (empty — agent actions appear here after ask)\n");
        return 0;
    }
    buf[len] = '\0';
    console_write("audit: /var/peak/audit.log\n");
    console_write(buf[0] ? buf : "(empty)\n");
    if (len && buf[len - 1] != '\n')
        console_putc('\n');
    return 0;
}

int umemory_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[2048];
    size_t len = 0;
    if (vfs_read_file("/var/peak/sessions/memory.txt", buf, sizeof(buf) - 1, &len) != 0) {
        console_write("memory: (empty — ask records turn summaries here)\n");
        return 0;
    }
    buf[len] = '\0';
    console_write("memory: /var/peak/sessions/memory.txt\n");
    console_write(buf[0] ? buf : "(empty)\n");
    if (len && buf[len - 1] != '\n')
        console_putc('\n');
    return 0;
}

int upolicy_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[2048];
    size_t len = 0;
    console_write("policy: /etc/peak/agent.policy\n");
    if (vfs_read_file("/etc/peak/agent.policy", buf, sizeof(buf) - 1, &len) != 0) {
        console_write("(empty — defaults: workspace paths + fs.read/write/list/exec)\n");
        return 0;
    }
    buf[len] = '\0';
    console_write(buf);
    if (len && buf[len - 1] != '\n')
        console_putc('\n');
    return 0;
}
