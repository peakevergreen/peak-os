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
#include "timer.h"
#include "gui.h"
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
