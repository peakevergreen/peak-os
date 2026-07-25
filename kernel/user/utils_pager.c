/* /bin: less, more, time */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "timer.h"
#include "ubin.h"
#include "util.h"

#define PAGE_MAX (64 * 1024)
#define PAGE_LINES 20
#define LINE_MAX 512
#define MAX_LINES 512

static int resolve_in_path(const char *path, char *abs, size_t abs_len) {
    if (!path || !strcmp(path, "-")) {
        const char *sin = shell_stdin_path();
        if (!sin)
            return -1;
        size_t i = 0;
        for (; sin[i] && i + 1 < abs_len; i++)
            abs[i] = sin[i];
        abs[i] = '\0';
        return 0;
    }
    return shell_resolve_path(path, abs, abs_len);
}

static int read_input(const char *path, char *buf, size_t cap, size_t *out) {
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs)))
        return -1;
    size_t n = 0;
    if (vfs_read_file(abs, buf, cap - 1, &n) != 0)
        return -1;
    buf[n] = '\0';
    *out = n;
    return 0;
}

static int split_lines(char *data, size_t len, char **lines, int max) {
    int n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || data[i] == '\n') {
            if (n >= max)
                break;
            lines[n++] = data + start;
            if (i < len)
                data[i] = '\0';
            start = i + 1;
        }
    }
    return n;
}

static void pager_show(const char *title, char **lines, int total, int start) {
    console_clear();
    console_write(title);
    console_write(" — space next, q quit\n\n");
    int end = start + PAGE_LINES;
    if (end > total)
        end = total;
    for (int i = start; i < end; i++) {
        console_write(lines[i]);
        console_write("\n");
    }
    if (end < total) {
        console_write("\n-- more --");
    } else {
        console_write("\n-- end --");
    }
}

static int pager_main(const char *tool, int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage(tool, "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char data[PAGE_MAX];
    size_t len = 0;
    if (read_input(path, data, sizeof(data), &len) != 0) {
        peak_perror(tool, "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int total = split_lines(data, len, lines, MAX_LINES);
    if (total == 0) {
        console_write("(empty)\n");
        return 0;
    }
    int pos = 0;
    pager_show(tool, lines, total, pos);
    for (;;) {
        char c = keyboard_try_getchar();
        if (c == 'q' || c == 'Q' || c == 27)
            break;
        if (c == ' ' || c == '\n' || c == '\r') {
            if (pos + PAGE_LINES < total) {
                pos += PAGE_LINES;
                pager_show(tool, lines, total, pos);
            } else {
                break;
            }
        }
        hlt();
    }
    console_write("\n");
    return 0;
}

int uless_main(int argc, char **argv) {
    return pager_main("less", argc, argv);
}

int umore_main(int argc, char **argv) {
    return pager_main("more", argc, argv);
}

int utime_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("time", "<command> [args...]");
        return argc < 2 ? 1 : 0;
    }
    char path[64];
    size_t i = 0;
    path[i++] = '/';
    path[i++] = 'b';
    path[i++] = 'i';
    path[i++] = 'n';
    path[i++] = '/';
    for (const char *s = argv[1]; *s && i + 1 < sizeof(path); s++)
        path[i++] = *s;
    path[i] = '\0';

    uint64_t t0 = timer_ticks();
    int rc = ubin_run(path, argc - 1, argv + 1);
    uint64_t dt = timer_ticks() - t0;
    if (rc == -999) {
        peak_perror("time", "unknown command");
        return 127;
    }
    console_printf("time: %lu ticks (~%lu ms)\n",
                   (unsigned long)dt, (unsigned long)(dt * 10));
    return rc;
}
