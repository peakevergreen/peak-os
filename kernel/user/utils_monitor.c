#include "libpeak.h"
#include "shell.h"
#include "console.h"
#include "timer.h"
#include "keyboard.h"
#include "sched.h"
#include "sysmon.h"
#include "heap.h"
#include "util.h"

static void top_render_once(int oneshot) {
    static char snap[4096];
    sysmon_poll();
    int n = sysmon_snapshot(snap, sizeof(snap));
    console_clear();
    if (n > 0)
        console_write(snap);
    if (!oneshot)
        console_write("\nq quit  r reset  e export  samples ~0.5s\n");
}

int utop_main(int argc, char **argv) {
    int oneshot = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--once"))
            oneshot = 1;
    }
    if (oneshot) {
        top_render_once(1);
        return 0;
    }
    console_write("Peak top (live). Press q to quit.\n");
    uint64_t last = 0;
    for (;;) {
        char c = keyboard_try_getchar();
        if (c == 'q' || c == 'Q' || c == 27)
            break;
        if (c == 'r' || c == 'R')
            sysmon_reset_history();
        if (c == 'e' || c == 'E') {
            if (sysmon_export(SYSMON_EXPORT_PATH) == 0)
                console_write("exported /tmp/sysmon.txt\n");
            else
                console_write("export failed\n");
        }
        uint64_t now = timer_ticks();
        if (now - last >= 50 || last == 0) {
            last = now;
            top_render_once(0);
        }
        sysmon_idle_enter();
        hlt();
        sysmon_idle_leave();
        sysmon_poll();
    }
    console_clear();
    console_write("top: stopped.\n");
    return 0;
}

int usysmon_main(int argc, char **argv) {
    return utop_main(argc, argv);
}

int ups_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct task list[MAX_TASKS];
    int n = sched_list_tasks(list, MAX_TASKS);
    sched_sort_tasks(list, n);
    int cur = sched_current_pid();
    uint64_t total_ticks = 0;
    for (int i = 0; i < n; i++)
        total_ticks += list[i].cpu_ticks;
    if (!total_ticks)
        total_ticks = 1;
    uint64_t now = timer_ticks();
    console_printf("PID  STATE   TICKS   AGE     WAKE    SHARE  NAME\n");
    for (int i = 0; i < n; i++) {
        const char *st =
            list[i].state == TASK_RUNNING ? "run" :
            list[i].state == TASK_READY ? "ready" :
            list[i].state == TASK_BLOCKED ? "block" :
            list[i].state == TASK_ZOMBIE ? "zombie" : "?";
        uint64_t age = now > list[i].spawned_at ? now - list[i].spawned_at : 0;
        uint32_t share = (uint32_t)((list[i].cpu_ticks * 100ull) / total_ticks);
        if (list[i].state == TASK_BLOCKED && list[i].wake_tick)
            console_printf("%-4d %-7s %-7lu %-7lu %-7lu %-5u %s%s\n",
                           list[i].pid, st,
                           (unsigned long)list[i].cpu_ticks,
                           (unsigned long)age,
                           (unsigned long)list[i].wake_tick,
                           (unsigned)share,
                           list[i].name,
                           list[i].pid == cur ? " *" : "");
        else
            console_printf("%-4d %-7s %-7lu %-7lu %-7s %-5u %s%s\n",
                           list[i].pid, st,
                           (unsigned long)list[i].cpu_ticks,
                           (unsigned long)age,
                           "-",
                           (unsigned)share,
                           list[i].name,
                           list[i].pid == cur ? " *" : "");
    }
    console_printf("tasks %d  ctx %lu  (sorted by SHR CPU ticks)\n",
                   n, (unsigned long)sched_ctx_switches());
    return 0;
}

int ukill_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("kill", "<pid|name>");
        return argc < 2 ? 1 : 0;
    }
    const char *arg = argv[1];
    int pid = 0;
    int numeric = 1;
    for (const char *p = arg; *p; p++) {
        if (*p < '0' || *p > '9') {
            numeric = 0;
            break;
        }
        pid = pid * 10 + (*p - '0');
    }
    if (numeric) {
        int rc = sched_kill(pid);
        if (rc == -1) {
            peak_perror("kill", "no such pid");
            return 1;
        }
        if (rc == -2) {
            peak_perror("kill", "refused");
            return 1;
        }
        return 0;
    }
    struct task list[MAX_TASKS];
    int n = sched_list_tasks(list, MAX_TASKS);
    int killed = 0;
    for (int i = 0; i < n; i++) {
        if (!strcmp(list[i].name, arg)) {
            int rc = sched_kill(list[i].pid);
            if (rc == 0)
                killed++;
        }
    }
    if (!killed) {
        peak_perror("kill", "no matching task");
        return 1;
    }
    return 0;
}
