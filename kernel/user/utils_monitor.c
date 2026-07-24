#include "libpeak.h"
#include "shell.h"
#include "console.h"
#include "timer.h"
#include "keyboard.h"
#include "sched.h"
#include "sysmon.h"
#include "util.h"

static void top_draw_bar(char *buf, size_t cap, uint32_t pct) {
    if (!buf || cap < 14)
        return;
    int width = 12;
    if ((size_t)(width + 2) > cap)
        width = (int)cap - 2;
    buf[0] = '[';
    int filled = (int)((pct * (uint32_t)width) / 100);
    for (int i = 0; i < width; i++)
        buf[1 + i] = (i < filled) ? '#' : '-';
    buf[1 + width] = ']';
    buf[2 + width] = '\0';
}

static void top_render_once(int oneshot) {
    sysmon_poll();
    const struct sysmon_sample *s = sysmon_latest();
    console_clear();

    uint64_t mins = s->uptime_secs / 60;
    uint64_t secs = s->uptime_secs % 60;
    console_printf("Peak top — up %lum%02lus  load %u%% idle %u%%  fps %u\n",
                   (unsigned long)mins, (unsigned long)secs,
                   (unsigned)s->load_pct, (unsigned)s->idle_pct,
                   (unsigned)s->gui_fps);
    if (!oneshot)
        console_write("q quit  r reset  samples ~0.5s\n");
    console_write("\n");

    char bar[20];
    char a[24], b[24], p[24];
    top_draw_bar(bar, sizeof(bar), s->mem_pct);
    sysmon_format_bytes(s->mem_used_pages * 4096ull, a, sizeof(a));
    sysmon_format_bytes(s->mem_total_pages * 4096ull, b, sizeof(b));
    sysmon_format_bytes(s->mem_peak_pages * 4096ull, p, sizeof(p));
    console_printf("Mem  %s %3u%%  %s / %s  peak %s\n",
                   bar, (unsigned)s->mem_pct, a, b, p);

    top_draw_bar(bar, sizeof(bar), s->heap_pct);
    sysmon_format_bytes(s->heap_used, a, sizeof(a));
    sysmon_format_bytes(s->heap_used + s->heap_free, b, sizeof(b));
    sysmon_format_bytes(s->heap_peak, p, sizeof(p));
    console_printf("Heap %s %3u%%  %s / %s  peak %s  (%lu blk)\n",
                   bar, (unsigned)s->heap_pct, a, b, p,
                   (unsigned long)s->heap_blocks);

    char rxr[16], txr[16], rxt[16], txt[16];
    sysmon_format_rate(s->rx_bps, rxr, sizeof(rxr));
    sysmon_format_rate(s->tx_bps, txr, sizeof(txr));
    sysmon_format_bytes(s->rx_bytes, rxt, sizeof(rxt));
    sysmon_format_bytes(s->tx_bytes, txt, sizeof(txt));
    console_printf("Net  RX %s  TX %s\n", rxr, txr);
    console_printf("     total RX %s TX %s  pkts %lu / %lu\n",
                   rxt, txt,
                   (unsigned long)s->rx_packets, (unsigned long)s->tx_packets);
    console_printf("Sched tasks %u  ctx %lu  irq %lu  vfs %lu\n",
                   (unsigned)s->tasks,
                   (unsigned long)s->ctx_switches,
                   (unsigned long)s->irq_count,
                   (unsigned long)s->vfs_nodes);
    console_printf("GFX  compose %uus  present %uus  surf %u%%\n",
                   (unsigned)s->compose_us, (unsigned)s->present_us,
                   (unsigned)s->surf_pressure);
    console_printf("Agent peakvec %uus  audit %uus\n\n",
                   (unsigned)s->peakvec_us, (unsigned)s->agent_audit_us);

    struct task list[MAX_TASKS];
    int tn = sched_list_tasks(list, MAX_TASKS);
    int cur = sched_current_pid();
    console_write("PID  STATE   TICKS   NAME\n");
    for (int i = 0; i < tn && i < 12; i++) {
        console_printf("%-4d %-7s %-7lu %s%s\n",
                       list[i].pid,
                       list[i].state == TASK_RUNNING ? "run" :
                       list[i].state == TASK_READY ? "ready" :
                       list[i].state == TASK_ZOMBIE ? "zombie" : "other",
                       (unsigned long)list[i].cpu_ticks,
                       list[i].name,
                       list[i].pid == cur ? " *" : "");
    }
    console_write("\n");

    struct sysmon_sample hist[SYSMON_HISTORY];
    int hn = sysmon_history(hist, SYSMON_HISTORY);
    uint32_t load_s[SYSMON_HISTORY], mem_s[SYSMON_HISTORY], net_s[SYSMON_HISTORY];
    for (int i = 0; i < hn; i++) {
        load_s[i] = hist[i].load_pct;
        mem_s[i] = hist[i].mem_pct;
        net_s[i] = hist[i].rx_bps + hist[i].tx_bps;
    }
    char spark[49];
    sysmon_sparkline(load_s, hn, spark, 48);
    console_printf("Load %s\n", spark);
    sysmon_sparkline(mem_s, hn, spark, 48);
    console_printf("Mem  %s\n", spark);
    sysmon_sparkline(net_s, hn, spark, 48);
    console_printf("Net  %s\n", spark);
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
    int cur = sched_current_pid();
    console_printf("PID  STATE   CPU     NAME\n");
    for (int i = 0; i < n; i++) {
        const char *st =
            list[i].state == TASK_RUNNING ? "run" :
            list[i].state == TASK_READY ? "ready" :
            list[i].state == TASK_BLOCKED ? "block" :
            list[i].state == TASK_ZOMBIE ? "zombie" : "?";
        console_printf("%-4d %-7s %-7lu %s%s\n",
                       list[i].pid, st,
                       (unsigned long)list[i].cpu_ticks,
                       list[i].name,
                       list[i].pid == cur ? " *" : "");
    }
    console_printf("tasks %d  ctx %lu\n", n, (unsigned long)sched_ctx_switches());
    return 0;
}
