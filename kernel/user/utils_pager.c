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
#define PAT_MAX 64

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

static void pager_show(const char *title, char **lines, int total, int start,
                       int full, int show_numbers) {
    console_clear();
    console_write(title);
    if (full)
        console_write(" — space next, b prev, g/G top/bottom, / search, q quit\n\n");
    else
        console_write(" — space next, / search, q quit\n\n");
    int end = start + PAGE_LINES;
    if (end > total)
        end = total;
    for (int i = start; i < end; i++) {
        if (show_numbers) {
            char ln[16];
            snprintf(ln, sizeof(ln), "%6d  ", i + 1);
            console_write(ln);
        }
        console_write(lines[i]);
        console_write("\n");
    }
    if (end < total) {
        console_write("\n-- more --");
    } else {
        console_write("\n-- end --");
    }
}

static int line_contains(const char *line, const char *pat, size_t plen, int icase) {
    if (plen == 0)
        return 1;
    size_t l = strlen(line);
    if (l < plen)
        return 0;
    for (size_t j = 0; j + plen <= l; j++) {
        int ok = 1;
        for (size_t k = 0; k < plen; k++) {
            char a = line[j + k];
            char b = pat[k];
            if (icase) {
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            }
            if (a != b) { ok = 0; break; }
        }
        if (ok)
            return 1;
    }
    return 0;
}

static int pager_find_next(char **lines, int total, int from, const char *pat, int icase) {
    size_t plen = strlen(pat);
    for (int i = from; i < total; i++) {
        if (line_contains(lines[i], pat, plen, icase))
            return i;
    }
    return -1;
}

static int pager_align_page(int line, int total) {
    int pos = line;
    if (pos + PAGE_LINES > total)
        pos = total - PAGE_LINES;
    if (pos < 0)
        pos = 0;
    return pos;
}

static int read_search_pat(char *buf, size_t cap) {
    size_t i = 0;
    console_write("/");
    for (;;) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') {
            console_putc('\n');
            buf[i] = '\0';
            return (int)i;
        }
        if (c == 27) {
            buf[0] = '\0';
            return -1;
        }
        if (c == '\b' || c == 127) {
            if (i > 0) {
                i--;
                console_write("\b \b");
            }
            continue;
        }
        if (i + 1 < cap) {
            buf[i++] = c;
            console_putc(c);
        }
    }
}

static int pager_load(const char *tool, int argc, char **argv,
                      char *data, size_t data_cap,
                      char **lines, int *total) {
    if (peak_wants_help(argc, argv)) {
        peak_usage(tool, "[path|-]");
        return -1;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    size_t len = 0;
    if (read_input(path, data, data_cap, &len) != 0) {
        peak_perror(tool, "cannot read");
        return 1;
    }
    *total = split_lines(data, len, lines, MAX_LINES);
    if (*total == 0) {
        console_write("(empty)\n");
        return -1;
    }
    return 0;
}

static int more_main(int argc, char **argv) {
    char data[PAGE_MAX];
    char *lines[MAX_LINES];
    int total = 0;
    int rc = pager_load("more", argc, argv, data, sizeof(data), lines, &total);
    if (rc < 0)
        return rc > 0 ? rc : 0;

    int pos = 0;
    char pat[PAT_MAX];
    pager_show("more", lines, total, pos, 0, 0);
    for (;;) {
        char c = keyboard_try_getchar();
        if (c == 'q' || c == 'Q' || c == 27)
            break;
        if (c == '/') {
            if (read_search_pat(pat, sizeof(pat)) > 0) {
                int hit = pager_find_next(lines, total, pos, pat, 0);
                if (hit >= 0) {
                    pos = hit;
                    pager_show("more", lines, total, pos, 0, 0);
                } else
                    console_write("Pattern not found\n");
            }
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r') {
            if (pos + PAGE_LINES < total) {
                pos += PAGE_LINES;
                pager_show("more", lines, total, pos, 0, 0);
            } else {
                break;
            }
        }
        hlt();
    }
    console_write("\n");
    return 0;
}

static int less_main(int argc, char **argv) {
    int show_numbers = 0;
    int icase = 0;
    int pathi = 1;
    for (; pathi < argc; pathi++) {
        if (!strcmp(argv[pathi], "-N")) { show_numbers = 1; continue; }
        if (!strcmp(argv[pathi], "-i")) { icase = 1; continue; }
        if (argv[pathi][0] == '-')
            continue;
        break;
    }
    char data[PAGE_MAX];
    char *lines[MAX_LINES];
    int total = 0;
    char fake_argv[3][64];
    char *fav[3];
    int fargc = 1;
    fav[0] = fake_argv[0];
    snprintf(fake_argv[0], sizeof(fake_argv[0]), "less");
    if (pathi < argc) {
        snprintf(fake_argv[1], sizeof(fake_argv[1]), "%s", argv[pathi]);
        fav[1] = fake_argv[1];
        fargc = 2;
    }
    int rc = pager_load("less", fargc, fav, data, sizeof(data), lines, &total);
    if (rc < 0)
        return rc > 0 ? rc : 0;

    int pos = 0;
    int search_from = 0;
    char pat[PAT_MAX];
    pager_show("less", lines, total, pos, 1, show_numbers);
    for (;;) {
        char c = keyboard_try_getchar();
        if (c == 'q' || c == 'Q' || c == 27)
            break;
        if (c == ' ' || c == '\n' || c == '\r') {
            if (pos + PAGE_LINES < total) {
                pos += PAGE_LINES;
                search_from = pos;
                pager_show("less", lines, total, pos, 1, show_numbers);
            } else {
                break;
            }
        } else if (c == 'b' || c == 'B') {
            if (pos > 0) {
                pos -= PAGE_LINES;
                if (pos < 0)
                    pos = 0;
                search_from = pos;
                pager_show("less", lines, total, pos, 1, show_numbers);
            }
        } else if (c == 'g') {
            pos = 0;
            search_from = 0;
            pager_show("less", lines, total, pos, 1, show_numbers);
        } else if (c == 'G') {
            pos = pager_align_page(total - 1, total);
            search_from = pos;
            pager_show("less", lines, total, pos, 1, show_numbers);
        } else if (c == '/') {
            if (read_search_pat(pat, sizeof(pat)) > 0) {
                int hit = pager_find_next(lines, total, search_from, pat, icase);
                if (hit < 0 && search_from > 0)
                    hit = pager_find_next(lines, total, 0, pat, icase);
                if (hit >= 0) {
                    pos = pager_align_page(hit, total);
                    search_from = hit + 1;
                    pager_show("less", lines, total, pos, 1, show_numbers);
                } else {
                    console_write("Pattern not found\n");
                }
            }
        }
        hlt();
    }
    console_write("\n");
    return 0;
}

int uless_main(int argc, char **argv) {
    return less_main(argc, argv);
}

int umore_main(int argc, char **argv) {
    return more_main(argc, argv);
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
