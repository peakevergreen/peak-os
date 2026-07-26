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
        peak_usage("diff", "[-u] <a> <b>");
        return argc < 3 ? 1 : 0;
    }
    int unified = peak_has_flag(argc, argv, "-u");
    const char *fa = NULL, *fb = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        if (!fa)
            fa = argv[i];
        else if (!fb)
            fb = argv[i];
    }
    if (!fa || !fb) {
        peak_usage("diff", "[-u] <a> <b>");
        return 1;
    }
    char a[READ_MAX], b[READ_MAX];
    size_t al = 0, bl = 0;
    if (read_file(fa, a, sizeof(a), &al) != 0 ||
        read_file(fb, b, sizeof(b), &bl) != 0) {
        peak_perror("diff", "cannot read");
        return 1;
    }
    char *la[MAX_LINES], *lb[MAX_LINES];
    int na = split_lines(a, al, la, MAX_LINES);
    int nb = split_lines(b, bl, lb, MAX_LINES);
    int i = 0, j = 0, diffs = 0;
    if (unified) {
        console_printf("--- %s\n", fa);
        console_printf("+++ %s\n", fb);
        console_printf("@@ -1,%d +1,%d @@\n", na, nb);
    }
    while (i < na || j < nb) {
        if (i < na && j < nb && !strcmp(la[i], lb[j])) {
            if (unified) {
                console_write(" ");
                console_write(la[i]);
                console_write("\n");
            }
            i++;
            j++;
            continue;
        }
        if (i < na) {
            if (unified)
                console_write("- ");
            else
                console_write("- ");
            console_write(la[i++]);
            console_write("\n");
            diffs++;
        }
        if (j < nb) {
            if (unified)
                console_write("+ ");
            else
                console_write("+ ");
            console_write(lb[j++]);
            console_write("\n");
            diffs++;
        }
    }
    return diffs ? 1 : 0;
}

/* patch lite: apply +/- hunks from patch file to target */
int upatch_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("patch", "<target> [patch-file|-]");
        return argc < 2 ? 1 : 0;
    }
    const char *target = argv[1];
    const char *patchp = argc >= 3 ? argv[2] : "-";
    char tgt[READ_MAX], pat[READ_MAX];
    size_t tl = 0, pl = 0;
    if (read_file(target, tgt, sizeof(tgt), &tl) != 0) {
        peak_perror("patch", "cannot read target");
        return 1;
    }
    if (read_file(patchp, pat, sizeof(pat), &pl) != 0) {
        peak_perror("patch", "cannot read patch");
        return 1;
    }
    char *tlines[MAX_LINES];
    int nt = split_lines(tgt, tl, tlines, MAX_LINES);
    char *plines[MAX_LINES];
    int np = split_lines(pat, pl, plines, MAX_LINES);
    char out[READ_MAX];
    size_t oo = 0;
    int ti = 0;
    for (int pi = 0; pi < np; pi++) {
        const char *pln = plines[pi];
        if (!strncmp(pln, "---", 3) || !strncmp(pln, "+++", 3))
            continue;
        if (pln[0] == '@')
            continue;
        if (pln[0] == '-' && pln[1] == ' ') {
            if (ti < nt && !strcmp(pln + 2, tlines[ti]))
                ti++;
            continue;
        }
        if (pln[0] == '+' && pln[1] == ' ') {
            const char *add = pln + 2;
            for (; *add && oo + 1 < sizeof(out); add++)
                out[oo++] = *add;
            out[oo++] = '\n';
            continue;
        }
        if (pln[0] == ' ' && pln[1] == ' ') {
            if (ti < nt) {
                const char *keep = tlines[ti++];
                for (; *keep && oo + 1 < sizeof(out); keep++)
                    out[oo++] = *keep;
                out[oo++] = '\n';
            }
            continue;
        }
    }
    while (ti < nt) {
        const char *keep = tlines[ti++];
        for (; *keep && oo + 1 < sizeof(out); keep++)
            out[oo++] = *keep;
        out[oo++] = '\n';
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(target, abs, sizeof(abs)))
        return 1;
    if (vfs_write_file(abs, out, oo) != 0) {
        peak_perror("patch", "write failed");
        return 1;
    }
    console_printf("patch: updated %s (%u bytes)\n", abs, (unsigned)oo);
    return 0;
}

static int sort_key_cmp(const char *a, const char *b, int numeric) {
    if (numeric) {
        int ia = peak_atoi(a);
        int ib = peak_atoi(b);
        if (ia != ib)
            return ia - ib;
    }
    return strcmp(a, b);
}

static void sort_ptrs(char **arr, int n, int numeric, int reverse) {
    for (int i = 1; i < n; i++) {
        char *key = arr[i];
        int j = i - 1;
        while (j >= 0) {
            int c = sort_key_cmp(arr[j], key, numeric);
            if (reverse)
                c = -c;
            if (c <= 0)
                break;
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

static void sort_parse_flags(int argc, char **argv, int *reverse, int *numeric, int *unique,
                             const char **path) {
    *reverse = *numeric = *unique = 0;
    *path = "-";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) {
            for (const char *p = argv[i] + 1; *p; p++) {
                if (*p == 'r')
                    *reverse = 1;
                else if (*p == 'n')
                    *numeric = 1;
                else if (*p == 'u')
                    *unique = 1;
            }
            continue;
        }
        *path = argv[i];
    }
}

int usort_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("sort", "[-r] [-n] [-u] [path|-]");
        return 0;
    }
    int reverse = 0, numeric = 0, unique = 0;
    const char *path = "-";
    sort_parse_flags(argc, argv, &reverse, &numeric, &unique, &path);
    char data[READ_MAX];
    size_t len = 0;
    if (read_file(path, data, sizeof(data), &len) != 0) {
        peak_perror("sort", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(data, len, lines, MAX_LINES);
    sort_ptrs(lines, n, numeric, reverse);
    for (int i = 0; i < n; i++) {
        if (unique && i > 0 && !strcmp(lines[i], lines[i - 1]))
            continue;
        console_write(lines[i]);
        console_write("\n");
    }
    return 0;
}

int uuniq_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("uniq", "[-c] [path|-]");
        return 0;
    }
    int count_prefix = peak_has_flag(argc, argv, "-c");
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c"))
            continue;
        if (argv[i][0] != '-')
            path = argv[i];
    }
    char data[READ_MAX];
    size_t len = 0;
    if (read_file(path, data, sizeof(data), &len) != 0) {
        peak_perror("uniq", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(data, len, lines, MAX_LINES);
    for (int i = 0; i < n;) {
        int run = 1;
        while (i + run < n && !strcmp(lines[i], lines[i + run]))
            run++;
        if (count_prefix)
            console_printf("%d ", run);
        console_write(lines[i]);
        console_write("\n");
        i += run;
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

static const char *line_field_delim(const char *line, int field, char delim,
                                    char *buf, size_t cap) {
    if (!line || field < 1 || !buf || cap < 2)
        return "";
    const char *p = line;
    int f = 1;
    while (*p && f < field) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (delim != ' ') {
            while (*p && *p != delim)
                p++;
            if (*p == delim)
                p++;
        } else {
            while (*p && *p != ' ' && *p != '\t')
                p++;
        }
        f++;
    }
    while (*p == ' ' || *p == '\t')
        p++;
    size_t i = 0;
    if (delim != ' ') {
        while (*p && *p != delim && i + 1 < cap)
            buf[i++] = *p++;
    } else {
        while (*p && *p != ' ' && *p != '\t' && i + 1 < cap)
            buf[i++] = *p++;
    }
    buf[i] = '\0';
    return buf;
}

int ujoin_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("join", "[-1|-2] [-t c] <file1> <file2>");
        return argc < 3 ? 1 : 0;
    }
    int field = 1;
    char delim = ' ';
    const char *f1 = NULL, *f2 = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-1")) {
            field = 1;
            continue;
        }
        if (!strcmp(argv[i], "-2")) {
            field = 2;
            continue;
        }
        if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            delim = argv[++i][0] ? argv[i][0] : ' ';
            continue;
        }
        if (argv[i][0] == '-')
            continue;
        if (!f1)
            f1 = argv[i];
        else
            f2 = argv[i];
    }
    if (!f1 || !f2) {
        peak_usage("join", "[-1|-2] [-t c] <file1> <file2>");
        return 1;
    }
    char a[READ_MAX], b[READ_MAX];
    size_t al = 0, bl = 0;
    if (read_file(f1, a, sizeof(a), &al) != 0 || read_file(f2, b, sizeof(b), &bl) != 0) {
        peak_perror("join", "cannot read");
        return 1;
    }
    char *la[MAX_LINES], *lb[MAX_LINES];
    int na = split_lines(a, al, la, MAX_LINES);
    int nb = split_lines(b, bl, lb, MAX_LINES);
    for (int i = 0; i < na; i++) {
        char key[LINE_MAX];
        line_field_delim(la[i], field, delim, key, sizeof(key));
        if (!key[0])
            continue;
        for (int j = 0; j < nb; j++) {
            char keyb[LINE_MAX];
            line_field_delim(lb[j], field, delim, keyb, sizeof(keyb));
            if (!strcmp(key, keyb)) {
                console_write(la[i]);
                console_putc(' ');
                console_write(lb[j]);
                console_putc('\n');
                break;
            }
        }
    }
    return 0;
}

int ucomm_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("comm", "[-1|-2|-3] <file1> <file2>");
        return argc < 3 ? 1 : 0;
    }
    int suppress1 = 0, suppress2 = 0, suppress3 = 0;
    const char *f1 = NULL, *f2 = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-1")) {
            suppress1 = 1;
            continue;
        }
        if (!strcmp(argv[i], "-2")) {
            suppress2 = 1;
            continue;
        }
        if (!strcmp(argv[i], "-3")) {
            suppress3 = 1;
            continue;
        }
        if (argv[i][0] == '-')
            continue;
        if (!f1)
            f1 = argv[i];
        else
            f2 = argv[i];
    }
    if (!f1 || !f2) {
        peak_usage("comm", "[-1|-2|-3] <file1> <file2>");
        return 1;
    }
    char a[READ_MAX], b[READ_MAX];
    size_t al = 0, bl = 0;
    if (read_file(f1, a, sizeof(a), &al) != 0 || read_file(f2, b, sizeof(b), &bl) != 0) {
        peak_perror("comm", "cannot read");
        return 1;
    }
    char *la[MAX_LINES], *lb[MAX_LINES];
    int na = split_lines(a, al, la, MAX_LINES);
    int nb = split_lines(b, bl, lb, MAX_LINES);
    int i = 0, j = 0;
    while (i < na || j < nb) {
        int cmp = 0;
        if (i < na && j < nb)
            cmp = strcmp(la[i], lb[j]);
        else if (i < na)
            cmp = -1;
        else
            cmp = 1;
        if (cmp < 0) {
            if (!suppress1) {
                console_write(la[i]);
                console_putc('\n');
            }
            i++;
        } else if (cmp > 0) {
            if (!suppress2) {
                console_write("\t");
                console_write(lb[j]);
                console_putc('\n');
            }
            j++;
        } else {
            if (!suppress3) {
                console_write("\t\t");
                console_write(la[i]);
                console_putc('\n');
            }
            i++;
            j++;
        }
    }
    return 0;
}

