#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"
#include "pmm.h"
#include "heap.h"
#include "sysmon.h"
#include "theme.h"
#include "wallpaper.h"
#include "fb.h"
#include "settings.h"
#include "timer.h"
#include "gui.h"
#include "rtc.h"
#include "cap.h"
#include "privacy.h"
#include "peakdisk.h"
#include "bootinfo.h"
#include "ubin.h"

int upwd_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_write(shell_getcwd());
    console_write("\n");
    return 0;
}

int ucd_main(int argc, char **argv) {
    const char *p = argc >= 2 ? argv[1] : "/home/dev/workspace";
    if (argc >= 2 && !strcmp(argv[1], "-")) {
        p = shell_env_get("OLDPWD");
        if (!p || !p[0]) {
            peak_perror("cd", "OLDPWD not set");
            return 1;
        }
    }
    if (shell_chdir(p) != 0) {
        shell_perror_path("cd", p);
        return 1;
    }
    if (argc >= 2 && !strcmp(argv[1], "-")) {
        console_write(shell_getcwd());
        console_write("\n");
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

#define FIND_USAGE "<dir> [-name pat] [-iname pat] [-type f|d] [-maxdepth N] [-print0] [-exec cmd {} ;]"
#define FIND_EXEC_MAX 8

struct find_ctx {
    const char *name;
    const char *iname;
    int has_type;
    enum vfs_type type_want;
    int maxdepth;
    int print0;
    char exec_tpl[128];
    int exec_max;
    int exec_count;
    int found;
};

static const char *find_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            base = p + 1;
    return base;
}

static char find_fold(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

static int find_icase_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (find_fold(*a) != find_fold(*b))
            return 0;
    }
    return *a == 0 && *b == 0;
}

static int find_exec_run(struct find_ctx *ctx, const char *path) {
    if (!ctx->exec_tpl[0] || ctx->exec_count >= ctx->exec_max)
        return 0;
    char tokbuf[8][64];
    char *av[12];
    int ac = 0;
    const char *p = ctx->exec_tpl;
    int ti = 0;
    while (*p && ac < 11) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (*p == '{' && p[1] == '}') {
            av[ac++] = (char *)path;
            p += 2;
            continue;
        }
        if (ti >= 8)
            break;
        size_t o = 0;
        while (*p && *p != ' ' && o + 1 < sizeof(tokbuf[ti]))
            tokbuf[ti][o++] = *p++;
        tokbuf[ti][o] = '\0';
        av[ac++] = tokbuf[ti++];
    }
    if (ac == 0)
        return 0;
    av[ac] = NULL;
    char bpath[64];
    size_t pi = 0;
    bpath[pi++] = '/';
    bpath[pi++] = 'b';
    bpath[pi++] = 'i';
    bpath[pi++] = 'n';
    bpath[pi++] = '/';
    for (const char *s = av[0]; *s && pi + 1 < sizeof(bpath); s++)
        bpath[pi++] = *s;
    bpath[pi] = '\0';
    (void)ubin_run(bpath, ac, av);
    ctx->exec_count++;
    return 0;
}

static int find_matches(struct find_ctx *ctx, const char *path, struct vfs_node *node) {
    if (ctx->has_type && node->type != ctx->type_want)
        return 0;
    const char *base = find_basename(path);
    if (ctx->name && strcmp(base, ctx->name) != 0)
        return 0;
    if (ctx->iname && !find_icase_eq(base, ctx->iname))
        return 0;
    return 1;
}

static void find_emit_match(struct find_ctx *ctx, const char *path) {
    ctx->found++;
    if (ctx->exec_tpl[0]) {
        find_exec_run(ctx, path);
        return;
    }
    console_write(path);
    if (ctx->print0)
        console_putc('\0');
    else
        console_write("\n");
}

static int find_walk_rec(struct vfs_node *n, char *path, size_t path_len, int depth,
                         struct find_ctx *ctx) {
    if (ctx->maxdepth >= 0 && depth > ctx->maxdepth)
        return 0;
    if (find_matches(ctx, path, n))
        find_emit_match(ctx, path);
    if (n->type != VFS_DIR)
        return 0;
    if (ctx->maxdepth >= 0 && depth >= ctx->maxdepth)
        return 0;
    for (struct vfs_node *c = n->child; c; c = c->sibling) {
        char child[VFS_PATH_MAX];
        size_t cl = 0;
        if (path_len == 1 && path[0] == '/')
            child[cl++] = '/';
        else {
            memcpy(child, path, path_len);
            cl = path_len;
            child[cl++] = '/';
        }
        for (size_t k = 0; c->name[k] && cl + 1 < sizeof(child); k++)
            child[cl++] = c->name[k];
        child[cl] = '\0';
        if (find_walk_rec(c, child, cl, depth + 1, ctx) != 0)
            return PEAK_EIO;
    }
    return 0;
}

static int find_walk(const char *path, struct find_ctx *ctx) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n)
        return PEAK_ENOENT;
    char pbuf[VFS_PATH_MAX];
    size_t pl = 0;
    while (path[pl] && pl + 1 < sizeof(pbuf)) {
        pbuf[pl] = path[pl];
        pl++;
    }
    pbuf[pl] = '\0';
    return find_walk_rec(n, pbuf, pl, 0, ctx);
}

int ufind_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("find", FIND_USAGE);
        return argc < 2 ? 1 : 0;
    }
    const char *dir = argv[1];
    const char *name = NULL;
    const char *iname = NULL;
    int has_type = 0;
    enum vfs_type type_want = 0;
    int maxdepth = -1;
    int print0 = 0;
    char exec_tpl[128];
    exec_tpl[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-print0")) {
            print0 = 1;
            continue;
        }
        if (!strcmp(argv[i], "-exec")) {
            size_t o = 0;
            for (i++; i < argc && strcmp(argv[i], ";") != 0; i++) {
                if (o && o + 1 < sizeof(exec_tpl))
                    exec_tpl[o++] = ' ';
                const char *a = argv[i];
                for (; *a && o + 1 < sizeof(exec_tpl); a++)
                    exec_tpl[o++] = *a;
            }
            exec_tpl[o] = '\0';
            if (i >= argc || strcmp(argv[i], ";") != 0) {
                peak_usage("find", FIND_USAGE);
                return 1;
            }
            continue;
        }
        if (!strcmp(argv[i], "-name")) {
            if (i + 1 >= argc) {
                peak_usage("find", FIND_USAGE);
                return 1;
            }
            name = argv[++i];
        } else if (!strcmp(argv[i], "-iname")) {
            if (i + 1 >= argc) {
                peak_usage("find", FIND_USAGE);
                return 1;
            }
            iname = argv[++i];
        } else if (!strcmp(argv[i], "-type")) {
            if (i + 1 >= argc) {
                peak_usage("find", FIND_USAGE);
                return 1;
            }
            const char *t = argv[++i];
            if (!strcmp(t, "f")) {
                type_want = VFS_FILE;
                has_type = 1;
            } else if (!strcmp(t, "d")) {
                type_want = VFS_DIR;
                has_type = 1;
            }
            else {
                peak_usage("find", FIND_USAGE);
                return 1;
            }
        } else if (!strcmp(argv[i], "-maxdepth")) {
            if (i + 1 >= argc) {
                peak_usage("find", FIND_USAGE);
                return 1;
            }
            maxdepth = peak_atoi(argv[++i]);
            if (maxdepth < 0) {
                peak_usage("find", FIND_USAGE);
                return 1;
            }
        } else {
            peak_usage("find", FIND_USAGE);
            return 1;
        }
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(dir, abs, sizeof(abs)))
        return 1;
    struct find_ctx ctx = {
        .name = name,
        .iname = iname,
        .has_type = has_type,
        .type_want = type_want,
        .maxdepth = maxdepth,
        .print0 = print0,
        .exec_max = FIND_EXEC_MAX,
        .exec_count = 0,
        .found = 0,
    };
    if (exec_tpl[0]) {
        size_t o = 0;
        for (; exec_tpl[o] && o + 1 < sizeof(ctx.exec_tpl); o++)
            ctx.exec_tpl[o] = exec_tpl[o];
        ctx.exec_tpl[o] = '\0';
    }
    if (find_walk(abs, &ctx) != 0)
        return 1;
    return 0;
}


static int rtc_is_leap(unsigned y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint64_t rtc_unix_secs(const struct rtc_time *t) {
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint64_t days = 0;
    for (unsigned y = 1970; y < t->year; y++)
        days += rtc_is_leap(y) ? 366u : 365u;
    for (unsigned m = 1; m < t->month; m++) {
        days += (uint64_t)mdays[m - 1];
        if (m == 2 && rtc_is_leap(t->year))
            days++;
    }
    days += (uint64_t)(t->day - 1);
    return days * 86400ull + (uint64_t)t->hour * 3600ull + (uint64_t)t->min * 60ull +
           (uint64_t)t->sec;
}

static void date_pad2(unsigned v, char *out, size_t *o, size_t cap) {
    if (*o + 2 >= cap)
        return;
    out[(*o)++] = (char)('0' + (v / 10) % 10);
    out[(*o)++] = (char)('0' + v % 10);
}

static void date_format(const struct rtc_time *t, const char *fmt, char *out, size_t cap) {
    size_t o = 0;
    for (const char *p = fmt; *p && o + 1 < cap; p++) {
        if (*p != '%') {
            out[o++] = *p;
            continue;
        }
        p++;
        if (!*p)
            break;
        if (*p == '%') {
            out[o++] = '%';
            continue;
        }
        if (*p == 's') {
            uint64_t u = rtc_unix_secs(t);
            char tmp[24];
            snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)u);
            for (size_t i = 0; tmp[i] && o + 1 < cap; i++)
                out[o++] = tmp[i];
            continue;
        }
        if (*p == 'Y') {
            if (o + 4 >= cap)
                break;
            unsigned y = t->year;
            out[o++] = (char)('0' + (y / 1000) % 10);
            out[o++] = (char)('0' + (y / 100) % 10);
            out[o++] = (char)('0' + (y / 10) % 10);
            out[o++] = (char)('0' + y % 10);
            continue;
        }
        if (*p == 'm' || *p == 'd' || *p == 'H' || *p == 'M' || *p == 'S') {
            unsigned v = 0;
            if (*p == 'm')
                v = t->month;
            else if (*p == 'd')
                v = t->day;
            else if (*p == 'H')
                v = t->hour;
            else if (*p == 'M')
                v = t->min;
            else
                v = t->sec;
            date_pad2(v, out, &o, cap);
            continue;
        }
        out[o++] = '%';
        out[o++] = *p;
    }
    out[o] = '\0';
}

int udate_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("date", "[+format]");
        console_write("  formats: +%s +%Y-%m-%d (RTC when available)\n");
        return 0;
    }
    struct rtc_time t;
    if (rtc_read(&t) != 0) {
        peak_perror("date", "RTC unavailable");
        return 1;
    }
    if (argc >= 2 && argv[1][0] == '+' && argv[1][1]) {
        char buf[64];
        date_format(&t, argv[1] + 1, buf, sizeof(buf));
        console_printf("%s\n", buf);
        return 0;
    }
    char wall[40];
    rtc_format_date(wall, sizeof(wall));
    if (wall[0])
        console_printf("%s\n", wall);
    console_printf("uptime ticks=%lu (~%lus)\n", timer_ticks(), timer_uptime_secs());
    return 0;
}

int ufree_main(int argc, char **argv) {
    int human = peak_has_flag(argc, argv, "-h");
    uint64_t free_p = pmm_free_pages();
    uint64_t total_p = pmm_total_pages();
    if (human) {
        char fb[24], tb[24];
        sysmon_format_bytes(free_p * 4096ull, fb, sizeof(fb));
        sysmon_format_bytes(total_p * 4096ull, tb, sizeof(tb));
        console_printf("Mem:  %s free / %s total (pages %lu / %lu)\n",
                       fb, tb, (unsigned long)free_p, (unsigned long)total_p);
    } else
        console_printf("pages free: %lu / %lu\n", free_p, total_p);
    uint64_t used = 0, freeb = 0, blocks = 0;
    heap_get_stats(&used, &freeb, &blocks);
    struct heap_freelist_stats fl;
    heap_get_freelist_stats(&fl);
    char u[24], f[24], l[24];
    sysmon_format_bytes(used, u, sizeof(u));
    sysmon_format_bytes(freeb, f, sizeof(f));
    sysmon_format_bytes(fl.largest_free, l, sizeof(l));
    console_printf("heap used:  %s  free: %s  blocks: %lu\n", u, f,
                   (unsigned long)blocks);
    console_printf("heap frag:  %u%%  free blocks: %u  freelists: %u  largest: %s\n",
                   (unsigned)heap_fragmentation_pct(), (unsigned)fl.free_blocks,
                   (unsigned)fl.freelist_heads, l);
    if (heap_oom_count())
        console_printf("heap oom:   %u allocation failure(s)\n",
                       (unsigned)heap_oom_count());
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
    if (argc >= 2) {
        if (!strcmp(argv[1], "nav") || !strcmp(argv[1], "file") ||
            !strcmp(argv[1], "text") || !strcmp(argv[1], "sys") ||
            !strcmp(argv[1], "meta") || !strcmp(argv[1], "net"))
            shell_help_category(argv[1]);
        else
            shell_help_cmd(argv[1]);
    } else
        shell_help_topics();
    return 0;
}

int uhistory_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    shell_history_list();
    return 0;
}

int ualias_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("alias", "[name[=value]]");
        return 0;
    }
    if (argc < 2) {
        shell_alias_list();
        return 0;
    }
    const char *eq = strchr(argv[1], '=');
    if (!eq) {
        const char *v = shell_alias_lookup(argv[1]);
        if (!v) {
            peak_perror("alias", "not found");
            return 1;
        }
        console_write(argv[1]);
        console_write("='");
        console_write(v);
        console_write("'\n");
        return 0;
    }
    char name[32];
    size_t nlen = (size_t)(eq - argv[1]);
    if (nlen >= sizeof(name))
        nlen = sizeof(name) - 1;
    memcpy(name, argv[1], nlen);
    name[nlen] = '\0';
    if (shell_alias_set(name, eq + 1) != 0) {
        peak_perror("alias", "table full");
        return 1;
    }
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

int udisksave_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!peakdisk_available()) {
        console_write("disksave: no block device (need ATA disk in QEMU/PI)\n");
        return 1;
    }
    if (privacy_persist_profile() <= 0) {
        console_write("disksave: enable with `privacy persist workspace` first\n");
        return 1;
    }
    if (peakdisk_busy()) {
        console_write("disksave: save already in progress\n");
        return 1;
    }
    console_write("disksave: writing workspace to disk…\n");
    if (peakdisk_save() != 0) {
        const char *why = peakdisk_last_error();
        if (why && why[0])
            console_printf("disksave: failed: %s\n", why);
        else
            console_write("disksave: failed\n");
        return 1;
    }
    console_printf("disksave: saved %u bytes to block device\n",
                   (unsigned)peakdisk_last_save_bytes());
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
    if (argc >= 2 && !strcmp(argv[1], "clear-session")) {
        privacy_clear_session();
        console_write("session cleared (net grants, clipboard, toasts)\n");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "net-allow")) {
        privacy_grant_net_client(0);
        console_write("outbound network granted for this session\n");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "kill-switch")) {
        int on = (argc < 3 || strcmp(argv[2], "off") != 0);
        if (on && (argc < 4 || strcmp(argv[3], "--confirm") != 0)) {
            console_write("kill-switch: enabling blocks all outbound/listen.\n");
            console_write("confirm with: privacy kill-switch on --confirm\n");
            return 1;
        }
        privacy_set_net_kill_switch(on);
        console_printf("kill-switch: %s\n", on ? "on" : "off");
        return 0;
    }
    console_printf("persist=%d kill=%d net_client=%d localhost_listen=%d\n",
                   privacy_persist_profile(), privacy_net_kill_switch(),
                   privacy_net_client_allowed(), privacy_listeners_localhost_only());
    console_write("usage: privacy [persist private|workspace|full|clear-session|net-allow|kill-switch [on|off]]\n");
    return 0;
}

int ugui_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (shell_mode() == MODE_GUI) {
        console_write("Already on desktop — press Ctrl+Alt+Esc to return to CLI.\n");
        return 0;
    }
    console_write("Entering desktop... Press Ctrl+Alt+Esc anytime to return to CLI.\n");
    shell_set_mode(MODE_GUI);
    fb_set_ui_scale(settings_gui_scale());
    desktop_run();
    fb_set_ui_scale(settings_gui_scale());
    shell_set_mode(MODE_CLI);
    console_init();
    theme_apply_console();
    console_write("Back in CLI. Ctrl+Alt+Esc leaves desktop; type 'help' or 'gui'.\n");
    return 0;
}

static const char *uname_node(void) {
    const char *h = shell_env_get("HOSTNAME");
    return (h && h[0]) ? h : "peak";
}

int uuname_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("uname", "[-asnmr]");
        return 0;
    }

    int all = peak_has_flag(argc, argv, "-a");
    int show_s = all || peak_has_flag(argc, argv, "-s");
    int show_n = all || peak_has_flag(argc, argv, "-n");
    int show_r = all || peak_has_flag(argc, argv, "-r");
    int show_m = all || peak_has_flag(argc, argv, "-m");
    if (!show_s && !show_n && !show_r && !show_m)
        show_s = 1;

    char line[192];
    size_t o = 0;
#define UNAME_APPEND(s) do { \
    const char *p = (s); \
    if (o > 0 && o + 1 < sizeof(line)) \
        line[o++] = ' '; \
    while (*p && o + 1 < sizeof(line)) \
        line[o++] = *p++; \
} while (0)

    if (show_s)
        UNAME_APPEND(bootinfo_sysname());
    if (show_n)
        UNAME_APPEND(uname_node());
    if (show_r)
        UNAME_APPEND(bootinfo_release());
    if (show_m)
        UNAME_APPEND(bootinfo_machine());
    if (all) {
        UNAME_APPEND(bootinfo_sysname());
        UNAME_APPEND(bootinfo_release());
        char ver[48];
        bootinfo_format_version(ver, sizeof(ver));
        UNAME_APPEND(ver);
    }
#undef UNAME_APPEND
    line[o] = '\0';
    console_printf("%s\n", line);
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
