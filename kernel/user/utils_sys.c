#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"
#include "pmm.h"
#include "theme.h"
#include "wallpaper.h"
#include "fb.h"
#include "settings.h"
#include "agent.h"
#include "timer.h"
#include "gui.h"
#include "net.h"
#include "sysmon.h"
#include "keyboard.h"
#include "sched.h"
#include "rtc.h"
#include "cap.h"
#include "peakdisk.h"

int upwd_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_write(shell_getcwd());
    console_write("\n");
    return 0;
}

int ucd_main(int argc, char **argv) {
    const char *p = argc >= 2 ? argv[1] : "/home/dev/workspace";
    if (shell_chdir(p) != 0) {
        peak_perror("cd", "no such directory");
        return 1;
    }
    return 0;
}

int uls_main(int argc, char **argv) {
    int longf = peak_has_flag(argc, argv, "-l");
    const char *path = ".";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            path = argv[i];
            break;
        }
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)))
        return 1;
    if (!vfs_is_dir(abs)) {
        struct vfs_stat st;
        if (vfs_stat(abs, &st) != 0) {
            peak_perror("ls", "not found");
            return 1;
        }
        if (longf)
            console_printf("%c %6lu %s\n", 'f', (uint64_t)st.size, abs);
        else {
            console_write(abs);
            console_write("\n");
        }
        return 0;
    }
    struct vfs_dirent ents[64];
    int n = vfs_readdir(abs, ents, 64);
    if (n < 0)
        return 1;
    for (int i = 0; i < n; i++) {
        if (longf) {
            char child[VFS_PATH_MAX];
            if (!strcmp(abs, "/"))
                snprintf(child, sizeof(child), "/%s", ents[i].name);
            else
                snprintf(child, sizeof(child), "%s/%s", abs, ents[i].name);
            struct vfs_stat st;
            vfs_stat(child, &st);
            console_printf("%c %6lu %s\n",
                           ents[i].type == VFS_DIR ? 'd' : 'f',
                           (uint64_t)st.size, ents[i].name);
        } else {
            console_write(ents[i].name);
            if (ents[i].type == VFS_DIR)
                console_write("/");
            console_write("\n");
        }
    }
    return 0;
}

static void tree_walk(const char *path, int depth, int max_depth) {
    if (depth > max_depth)
        return;
    struct vfs_dirent ents[64];
    int n = vfs_readdir(path, ents, 64);
    if (n < 0)
        return;
    for (int i = 0; i < n; i++) {
        for (int d = 0; d < depth; d++)
            console_write("  ");
        console_write(ents[i].name);
        if (ents[i].type == VFS_DIR)
            console_write("/");
        console_write("\n");
        if (ents[i].type == VFS_DIR) {
            char child[VFS_PATH_MAX];
            if (!strcmp(path, "/"))
                snprintf(child, sizeof(child), "/%s", ents[i].name);
            else
                snprintf(child, sizeof(child), "%s/%s", path, ents[i].name);
            tree_walk(child, depth + 1, max_depth);
        }
    }
}

int utree_main(int argc, char **argv) {
    const char *path = ".";
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-') {
            path = argv[i];
            break;
        }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)))
        return 1;
    console_printf("%s\n", abs);
    tree_walk(abs, 1, 8);
    return 0;
}

struct find_ctx {
    const char *name;
    int found;
};

static int find_cb(const char *path, struct vfs_node *node, void *ud) {
    (void)node;
    struct find_ctx *ctx = ud;
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            base = p + 1;
    if (!strcmp(base, ctx->name)) {
        console_write(path);
        console_write("\n");
        ctx->found++;
    }
    return 0;
}

int ufind_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("find", "<dir> -name <name>");
        return argc < 3 ? 1 : 0;
    }
    const char *dir = argv[1];
    const char *name = NULL;
    for (int i = 2; i < argc - 1; i++) {
        if (!strcmp(argv[i], "-name"))
            name = argv[i + 1];
    }
    if (!name) {
        peak_usage("find", "<dir> -name <name>");
        return 1;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(dir, abs, sizeof(abs)))
        return 1;
    struct find_ctx ctx = { .name = name, .found = 0 };
    vfs_walk(abs, find_cb, &ctx);
    return 0;
}

int udate_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char wall[40];
    rtc_format_date(wall, sizeof(wall));
    if (wall[0])
        console_printf("%s\n", wall);
    console_printf("uptime ticks=%lu (~%lus)\n", timer_ticks(), timer_uptime_secs());
    return 0;
}

int ufree_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_printf("pages free: %lu / %lu\n", pmm_free_pages(), pmm_total_pages());
    return 0;
}

int uenv_main(int argc, char **argv) {
    if (argc >= 2 && strchr(argv[1], '=')) {
        char name[64];
        const char *eq = strchr(argv[1], '=');
        size_t nlen = (size_t)(eq - argv[1]);
        if (nlen >= sizeof(name))
            nlen = sizeof(name) - 1;
        memcpy(name, argv[1], nlen);
        name[nlen] = 0;
        shell_env_set(name, eq + 1);
        return 0;
    }
    shell_env_list();
    return 0;
}

int uexport_main(int argc, char **argv) {
    return uenv_main(argc, argv);
}

int uwhich_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("which", "<cmd>");
        return argc < 2 ? 1 : 0;
    }
    char path[VFS_PATH_MAX];
    snprintf(path, sizeof(path), "/bin/%s", argv[1]);
    if (vfs_exists(path)) {
        console_write(path);
        console_write("\n");
        return 0;
    }
    peak_perror("which", "not found");
    return 1;
}

int useq_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("seq", "[start] <end>");
        return argc < 2 ? 1 : 0;
    }
    int start = 1, end;
    if (argc == 2) {
        end = peak_atoi(argv[1]);
    } else {
        start = peak_atoi(argv[1]);
        end = peak_atoi(argv[2]);
    }
    for (int i = start; i <= end; i++)
        console_printf("%d\n", i);
    return 0;
}

int usleep_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("sleep", "<seconds>");
        return argc < 2 ? 1 : 0;
    }
    int sec = peak_atoi(argv[1]);
    if (sec < 0)
        sec = 0;
    uint64_t target = timer_ticks() + (uint64_t)sec * 100;
    while (timer_ticks() < target)
        hlt();
    return 0;
}

int utheme_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("theme", "[list|next|set <name>]");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        char buf[256];
        theme_list(buf, sizeof(buf));
        console_write(buf);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "set")) {
        if (theme_set(argv[2]) != 0) {
            peak_perror("theme", "unknown theme");
            return 1;
        }
        theme_persist();
        console_printf("theme: %s\n", theme_get()->name);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "next")) {
        theme_next();
        theme_persist();
        console_printf("theme: %s\n", theme_get()->name);
        return 0;
    }
    console_printf("current: %s\n", theme_get()->name);
    console_write("usage: theme [list|next|set <name>]\n");
    return 0;
}

int uwallpaper_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("wallpaper", "[list|none|next|set <path>]");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        console_write("none\n");
        console_write("/usr/share/peak/wallpapers/evergreen.ppm");
        if (wallpaper_enabled() && !strcmp(wallpaper_path(),
                "/usr/share/peak/wallpapers/evergreen.ppm"))
            console_write(" *\n");
        else
            console_write("\n");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "none")) {
        wallpaper_set("none");
        wallpaper_persist();
        console_write("wallpaper: none\n");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "next")) {
        wallpaper_next();
        wallpaper_persist();
        if (wallpaper_enabled())
            console_printf("wallpaper: %s\n", wallpaper_path());
        else
            console_write("wallpaper: none\n");
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "set")) {
        if (wallpaper_set(argv[2]) != 0) {
            peak_perror("wallpaper", "need binary PPM P6 path (or none)");
            return 1;
        }
        wallpaper_persist();
        if (wallpaper_enabled())
            console_printf("wallpaper: %s\n", wallpaper_path());
        else
            console_write("wallpaper: none\n");
        return 0;
    }
    if (wallpaper_enabled())
        console_printf("current: %s\n", wallpaper_path());
    else
        console_write("current: none\n");
    console_write("usage: wallpaper [list|next|none|set <ppm-path>]\n");
    return 0;
}

int uscale_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("scale", "[1-4]");
        return 0;
    }
    if (argc < 2) {
        console_printf("ui scale: %u\n", fb_ui_scale());
        return 0;
    }
    int s = peak_atoi(argv[1]);
    if (s < 1 || s > 4) {
        peak_perror("scale", "use 1..4");
        return 1;
    }
    fb_set_ui_scale((uint32_t)s);
    settings_set_gui_scale((uint32_t)s);
    settings_persist();
    console_init();
    theme_apply_console();
    console_printf("ui scale: %d\n", s);
    return 0;
}

int uhelp_main(int argc, char **argv) {
    if (argc >= 2)
        shell_help_cmd(argv[1]);
    else
        shell_help_topics();
    return 0;
}

int uman_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("man", "<cmd>");
        return argc < 2 ? 1 : 0;
    }
    shell_help_cmd(argv[1]);
    return 0;
}

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

int udisksave_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!peakdisk_available()) {
        console_write("disksave: no block device\n");
        return 1;
    }
    if (privacy_persist_profile() <= 0) {
        console_write("disksave: enable with `privacy persist workspace` first\n");
        return 1;
    }
    if (peakdisk_save() != 0) {
        console_write("disksave: failed\n");
        return 1;
    }
    console_write("disksave: ok\n");
    return 0;
}

int uprivacy_main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "persist")) {
        if (argc < 3) {
            console_write("usage: privacy persist private|workspace|full\n");
            return 1;
        }
        if (!strcmp(argv[2], "private"))
            privacy_set_persist_profile(0);
        else if (!strcmp(argv[2], "workspace"))
            privacy_set_persist_profile(1);
        else if (!strcmp(argv[2], "full"))
            privacy_set_persist_profile(2);
        else {
            console_write("unknown profile\n");
            return 1;
        }
        console_printf("persist profile: %s\n", argv[2]);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "net-allow")) {
        privacy_grant_net_client(0);
        console_write("outbound network granted for this session\n");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "kill-switch")) {
        int on = (argc < 3 || strcmp(argv[2], "off") != 0);
        privacy_set_net_kill_switch(on);
        console_printf("kill-switch: %s\n", on ? "on" : "off");
        return 0;
    }
    console_printf("persist=%d kill=%d net_client=%d localhost_listen=%d\n",
                   privacy_persist_profile(), privacy_net_kill_switch(),
                   privacy_net_client_allowed(), privacy_listeners_localhost_only());
    console_write("usage: privacy [persist private|workspace|full|net-allow|kill-switch [on|off]]\n");
    return 0;
}

int ugui_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (shell_mode() == MODE_GUI) {
        console_write("Already in desktop. Ctrl+Alt+Esc returns to CLI.\n");
        return 0;
    }
    console_write("Entering desktop... (Ctrl+Alt+Esc returns to CLI)\n");
    shell_set_mode(MODE_GUI);
    fb_set_ui_scale(settings_gui_scale());
    desktop_run();
    fb_set_ui_scale(settings_gui_scale());
    shell_set_mode(MODE_CLI);
    console_init();
    theme_apply_console();
    console_write("Back in Peak CLI. Type 'help'.\n");
    return 0;
}

int uuname_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
#if defined(__aarch64__)
    console_write("PeakOS 0.2.0-ai aarch64\n");
#else
    console_write("PeakOS 0.2.0-ai x86_64\n");
#endif
    return 0;
}

int utrue_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}

int ufalse_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 1;
}

int ureboot_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_write("Rebooting...\n");
    reboot();
    return 0;
}

int uifconfig_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct net_info ni;
    net_get_info(&ni);
    if (!ni.up) {
        console_write("e1000: down\n");
        return 1;
    }
    char ip[32], mask[32], gw[32], dns[32];
    net_format_ip(ni.ip, ip, sizeof(ip));
    net_format_ip(ni.mask, mask, sizeof(mask));
    net_format_ip(ni.gw, gw, sizeof(gw));
    net_format_ip(ni.dns, dns, sizeof(dns));
    console_printf("%s: flags=UP\n", ni.driver);
    console_printf("  ether %x:%x:%x:%x:%x:%x\n",
                   ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5]);
    console_printf("  inet %s  netmask %s  (%s)\n", ip, mask,
                   ni.addr_mode ? ni.addr_mode : "?");
    console_printf("  gateway %s  dns %s\n", gw, dns);
    return 0;
}

int uping_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("ping", "<host>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("ping", "network down");
        return 1;
    }
    uint32_t ip = net_dns_resolve(argv[1], 300);
    if (!ip) {
        peak_perror("ping", "DNS failed");
        return 1;
    }
    char buf[32];
    net_format_ip(ip, buf, sizeof(buf));
    console_printf("PING %s (%s)\n", argv[1], buf);
    /* TCP connect probe to :80 as reachability check (ICMP echo TX not exported) */
    uint64_t t0 = timer_ticks();
    int ok = (net_tcp_connect(ip, 80, 300) == 0);
    uint64_t dt = timer_ticks() - t0;
    if (ok) {
        net_tcp_close();
        console_printf("tcp/:80 open from %s time=%lums\n", buf, (unsigned long)(dt * 10));
        return 0;
    }
    console_printf("tcp/:80 no response from %s (host may filter)\n", buf);
    console_printf("DNS ok - stack is talking to the network.\n");
    return 0;
}

int uwget_main(int argc, char **argv) {
    /* Explicit user command = network consent for this session. */
    extern void privacy_grant_net_client(int remember);
    privacy_grant_net_client(0);
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("wget", "<url>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("wget", "network down");
        return 1;
    }
    char body[8192];
    int st = 0;
    console_printf("GET %s\n", argv[1]);
    if (net_http_get(argv[1], body, sizeof(body), &st) != 0) {
        console_printf("failed (status %d)\n", st);
        if (body[0])
            console_write(body);
        console_write("\n");
        return 1;
    }
    console_printf("HTTP %d  %lu bytes\n", st, (unsigned long)strlen(body));
    size_t show = strlen(body);
    if (show > 1500)
        show = 1500;
    for (size_t i = 0; i < show; i++)
        console_putc(body[i]);
    if (strlen(body) > show)
        console_write("\n... truncated ...\n");
    else
        console_write("\n");
    return 0;
}

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
    console_printf("Sched tasks %u  ctx %lu  irq %lu  vfs %lu\n\n",
                   (unsigned)s->tasks,
                   (unsigned long)s->ctx_switches,
                   (unsigned long)s->irq_count,
                   (unsigned long)s->vfs_nodes);

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
