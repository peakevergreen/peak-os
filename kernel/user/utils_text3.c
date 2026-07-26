/* /bin: sed-lite, cmp, basename, dirname, realpath */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define READ_MAX 32768
#define MAX_LINES 256
#define LINE_MAX 512

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

static int parse_addr(const char *script, int *lo, int *hi, const char **cmd) {
    *lo = 0;
    *hi = 0;
    if (!script[0] || script[0] < '0' || script[0] > '9') {
        *cmd = script;
        return 0;
    }
    int a = 0;
    const char *p = script;
    while (*p >= '0' && *p <= '9')
        a = a * 10 + (*p++ - '0');
    if (*p == ',') {
        p++;
        if (*p < '0' || *p > '9')
            return -1;
        int b = 0;
        while (*p >= '0' && *p <= '9')
            b = b * 10 + (*p++ - '0');
        if (a < 1 || b < a)
            return -1;
        *lo = a;
        *hi = b;
    } else if (a > 0) {
        *lo = a;
        *hi = a;
    } else {
        *cmd = script;
        return 0;
    }
    *cmd = p;
    return 0;
}

static int line_in_range(int line_no, int lo, int hi) {
    if (lo == 0)
        return 1;
    return line_no >= lo && line_no <= hi;
}

static void subst_line(char *out, size_t cap, const char *src, const char *old,
                       size_t old_len, const char *newv, size_t new_len, int global) {
    size_t o = 0;
    const char *p = src;
    while (*p) {
        const char *hit = 0;
        if (old_len == 0)
            hit = p;
        else {
            for (const char *q = p; *q; q++) {
                if (!memcmp(q, old, old_len)) {
                    hit = q;
                    break;
                }
            }
        }
        if (hit) {
            for (const char *q = p; q < hit && o + 1 < cap; q++)
                out[o++] = *q;
            for (size_t k = 0; k < new_len && o + 1 < cap; k++)
                out[o++] = newv[k];
            p = hit + old_len;
            if (!global)
                break;
        } else {
            if (o + 1 < cap)
                out[o++] = *p++;
            else
                p++;
        }
    }
    while (*p && o + 1 < cap)
        out[o++] = *p++;
    out[o] = '\0';
}

static void translit_line(char *out, size_t cap, const char *src, const char *from,
                          size_t from_len, const char *to) {
    unsigned char map[256];
    for (int i = 0; i < 256; i++)
        map[i] = (unsigned char)i;
    for (size_t i = 0; i < from_len; i++)
        map[(unsigned char)from[i]] = (unsigned char)to[i];
    size_t o = 0;
    for (const char *q = src; *q && o + 1 < cap; q++)
        out[o++] = (char)map[(unsigned char)*q];
    out[o] = '\0';
}

/* sed subset: [N|[N,M]] s/old/new/[g], y/from/to/, d, p, -n */
int used_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("sed", "[-n] <script> [path|-]");
        return argc < 2 ? 1 : 0;
    }
    int quiet = 0;
    const char *script = 0;
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) {
            quiet = 1;
            continue;
        }
        if (!script)
            script = argv[i];
        else
            path = argv[i];
    }
    if (!script) {
        peak_usage("sed", "[-n] <script> [path|-]");
        return 1;
    }

    char data[READ_MAX];
    size_t len = 0;
    if (read_file(path, data, sizeof(data), &len) != 0) {
        peak_perror("sed", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(data, len, lines, MAX_LINES);

    int addr_lo = 0, addr_hi = 0;
    const char *cmd = script;
    if (parse_addr(script, &addr_lo, &addr_hi, &cmd) != 0) {
        peak_perror("sed", "bad address");
        return 1;
    }

    int do_delete = 0, do_print = 0, do_subst = 0, do_tr = 0, subst_global = 0;
    const char *old = 0, *newv = 0, *from = 0, *to = 0;
    size_t old_len = 0, new_len = 0, from_len = 0;
    char old_buf[128], new_buf[128], from_buf[128], to_buf[128];

    if (cmd[0] == 's' && cmd[1] == '/') {
        const char *p = cmd + 2;
        size_t oi = 0;
        while (*p && *p != '/' && oi + 1 < sizeof(old_buf))
            old_buf[oi++] = *p++;
        old_buf[oi] = '\0';
        if (*p != '/') {
            peak_perror("sed", "bad s///");
            return 1;
        }
        p++;
        size_t ni = 0;
        while (*p && *p != '/' && ni + 1 < sizeof(new_buf))
            new_buf[ni++] = *p++;
        new_buf[ni] = '\0';
        if (*p != '/') {
            peak_perror("sed", "bad s///");
            return 1;
        }
        p++;
        if (*p == 'g') {
            subst_global = 1;
            p++;
        }
        if (*p) {
            peak_perror("sed", "bad s///");
            return 1;
        }
        old = old_buf;
        newv = new_buf;
        old_len = oi;
        new_len = ni;
        do_subst = 1;
    } else if (cmd[0] == 'y' && cmd[1] == '/') {
        const char *p = cmd + 2;
        size_t fi = 0;
        while (*p && *p != '/' && fi + 1 < sizeof(from_buf))
            from_buf[fi++] = *p++;
        from_buf[fi] = '\0';
        if (*p != '/') {
            peak_perror("sed", "bad y///");
            return 1;
        }
        p++;
        size_t ti = 0;
        while (*p && *p != '/' && ti + 1 < sizeof(to_buf))
            to_buf[ti++] = *p++;
        to_buf[ti] = '\0';
        if (*p != '/' || fi != ti || fi == 0) {
            peak_perror("sed", "bad y///");
            return 1;
        }
        from = from_buf;
        to = to_buf;
        from_len = fi;
        do_tr = 1;
    } else if (!strcmp(cmd, "d")) {
        do_delete = 1;
    } else if (!strcmp(cmd, "p")) {
        do_print = 1;
        quiet = 1;
    } else if (!strcmp(cmd, "q")) {
        do_delete = 1; /* handled below: quit after this line */
    } else if (!strcmp(cmd, "=")) {
        do_print = 1;
        quiet = 0;
    } else {
        peak_perror("sed", "unsupported script");
        return 1;
    }

    int do_quit = !strcmp(cmd, "q");
    int do_lineno = !strcmp(cmd, "=");

    for (int i = 0; i < n; i++) {
        int line_no = i + 1;
        if (!line_in_range(line_no, addr_lo, addr_hi))
            continue;
        if (do_delete && !do_lineno)
            continue;
        char out[LINE_MAX];
        const char *src = lines[i];
        if (do_subst) {
            subst_line(out, sizeof(out), src, old, old_len, newv, new_len, subst_global);
            src = out;
        } else if (do_tr) {
            translit_line(out, sizeof(out), src, from, from_len, to);
            src = out;
        }
        if (do_lineno) {
            console_printf("%d\n", line_no);
        }
        if (!quiet || do_print) {
            if (!do_lineno) {
                console_write(src);
                console_write("\n");
            }
        }
        if (do_quit)
            break;
    }
    return 0;
}

int ucmp_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("cmp", "<a> <b>");
        return argc < 3 ? 1 : 0;
    }
    char a[READ_MAX], b[READ_MAX];
    size_t al = 0, bl = 0;
    if (read_file(argv[1], a, sizeof(a), &al) != 0 ||
        read_file(argv[2], b, sizeof(b), &bl) != 0) {
        peak_perror("cmp", "cannot read");
        return 2;
    }
    size_t n = al < bl ? al : bl;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            console_printf("cmp: differ byte %lu\n", (unsigned long)(i + 1));
            return 1;
        }
    }
    if (al != bl) {
        console_printf("cmp: EOF on %s after %lu bytes\n",
                       al < bl ? argv[1] : argv[2], (unsigned long)n);
        return 1;
    }
    return 0;
}

int ubasename_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("basename", "<path>");
        return argc < 2 ? 1 : 0;
    }
    const char *p = argv[1];
    const char *base = p;
    for (const char *q = p; *q; q++)
        if (*q == '/')
            base = q + 1;
    console_write(*base ? base : "/");
    console_write("\n");
    return 0;
}

int udirname_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("dirname", "<path>");
        return argc < 2 ? 1 : 0;
    }
    char buf[VFS_PATH_MAX];
    size_t n = 0;
    for (; argv[1][n] && n + 1 < sizeof(buf); n++)
        buf[n] = argv[1][n];
    buf[n] = '\0';
    if (n == 0) {
        console_write(".\n");
        return 0;
    }
    while (n > 1 && buf[n - 1] == '/')
        buf[--n] = '\0';
    char *slash = 0;
    for (char *q = buf; *q; q++)
        if (*q == '/')
            slash = q;
    if (!slash) {
        console_write(".\n");
        return 0;
    }
    if (slash == buf) {
        console_write("/\n");
        return 0;
    }
    *slash = '\0';
    console_write(buf);
    console_write("\n");
    return 0;
}

int urealpath_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("realpath", "<path>");
        return argc < 2 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs))) {
        peak_perror("realpath", "bad path");
        return 1;
    }
    char resolved[VFS_PATH_MAX];
    if (vfs_resolve(abs, resolved, sizeof(resolved)) != 0) { peak_perror("realpath", "bad path"); return 1; }
    console_write(resolved);
    console_write("\n");
    return 0;
}
