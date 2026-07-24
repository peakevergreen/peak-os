/* /bin text batch A: diff, sort, uniq, cut, tr */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define READ_MAX 8192
#define LINE_MAX 256
#define MAX_LINES 256

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

static int read_file(const char *path, char *buf, size_t cap, size_t *out) {
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

int udiff_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("diff", "<a> <b>");
        return argc < 3 ? 1 : 0;
    }
    char a[READ_MAX], b[READ_MAX];
    size_t al = 0, bl = 0;
    if (read_file(argv[1], a, sizeof(a), &al) != 0 ||
        read_file(argv[2], b, sizeof(b), &bl) != 0) {
        peak_perror("diff", "cannot read");
        return 1;
    }
    char *la[MAX_LINES], *lb[MAX_LINES];
    int na = split_lines(a, al, la, MAX_LINES);
    int nb = split_lines(b, bl, lb, MAX_LINES);
    int i = 0, j = 0, diffs = 0;
    while (i < na || j < nb) {
        if (i < na && j < nb && !strcmp(la[i], lb[j])) {
            i++;
            j++;
            continue;
        }
        if (i < na) {
            console_write("- ");
            console_write(la[i++]);
            console_write("\n");
            diffs++;
        }
        if (j < nb) {
            console_write("+ ");
            console_write(lb[j++]);
            console_write("\n");
            diffs++;
        }
    }
    return diffs ? 1 : 0;
}

static void sort_ptrs(char **arr, int n) {
    for (int i = 1; i < n; i++) {
        char *key = arr[i];
        int j = i - 1;
        while (j >= 0 && strcmp(arr[j], key) > 0) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

int usort_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("sort", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char data[READ_MAX];
    size_t len = 0;
    if (read_file(path, data, sizeof(data), &len) != 0) {
        peak_perror("sort", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(data, len, lines, MAX_LINES);
    sort_ptrs(lines, n);
    for (int i = 0; i < n; i++) {
        console_write(lines[i]);
        console_write("\n");
    }
    return 0;
}

int uuniq_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("uniq", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char data[READ_MAX];
    size_t len = 0;
    if (read_file(path, data, sizeof(data), &len) != 0) {
        peak_perror("uniq", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(data, len, lines, MAX_LINES);
    const char *prev = 0;
    for (int i = 0; i < n; i++) {
        if (prev && !strcmp(prev, lines[i]))
            continue;
        console_write(lines[i]);
        console_write("\n");
        prev = lines[i];
    }
    return 0;
}

int ucut_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("cut", "-f N [-d delim] [path|-]");
        return 0;
    }
    int field = 1;
    char delim = '\t';
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) {
            field = peak_atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            delim = argv[++i][0];
            continue;
        }
        if (argv[i][0] != '-')
            path = argv[i];
    }
    if (field < 1)
        field = 1;
    char data[READ_MAX];
    size_t len = 0;
    if (read_file(path, data, sizeof(data), &len) != 0) {
        peak_perror("cut", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(data, len, lines, MAX_LINES);
    for (int i = 0; i < n; i++) {
        int f = 1;
        const char *p = lines[i];
        const char *start = p;
        for (;; p++) {
            if (*p == delim || *p == '\0') {
                if (f == field) {
                    for (const char *q = start; q < p; q++)
                        console_putc(*q);
                    console_putc('\n');
                    break;
                }
                if (!*p)
                    break;
                f++;
                start = p + 1;
            }
        }
    }
    return 0;
}

int utr_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("tr", "<from> <to> [path|-]");
        return argc < 3 ? 1 : 0;
    }
    const char *from = argv[1];
    const char *to = argv[2];
    const char *path = argc >= 4 ? argv[3] : "-";
    size_t fl = strlen(from);
    size_t tl = strlen(to);
    if (!fl || !tl) {
        peak_perror("tr", "empty set");
        return 1;
    }
    char data[READ_MAX];
    size_t len = 0;
    if (read_file(path, data, sizeof(data), &len) != 0) {
        peak_perror("tr", "cannot read");
        return 1;
    }
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        for (size_t j = 0; j < fl; j++) {
            if (c == from[j]) {
                c = to[j < tl ? j : tl - 1];
                break;
            }
        }
        console_putc(c);
    }
    if (!len || data[len - 1] != '\n')
        console_putc('\n');
    return 0;
}
