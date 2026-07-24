/* /bin: sed-lite, cmp, basename, dirname, realpath */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define READ_MAX 8192
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

/* sed subset: s/old/new/, d, p, -n */
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

    /* Parse script: s/old/new/ or single-letter d/p */
    int do_delete = 0, do_print = 0;
    const char *old = 0, *newv = 0;
    size_t old_len = 0, new_len = 0;
    char old_buf[128], new_buf[128];

    if (script[0] == 's' && script[1] == '/') {
        const char *p = script + 2;
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
        old = old_buf;
        newv = new_buf;
        old_len = oi;
        new_len = ni;
    } else if (!strcmp(script, "d")) {
        do_delete = 1;
    } else if (!strcmp(script, "p")) {
        do_print = 1;
        quiet = 1; /* -n style: only print on p */
    } else {
        peak_perror("sed", "unsupported script");
        return 1;
    }

    for (int i = 0; i < n; i++) {
        if (do_delete)
            continue;
        char out[LINE_MAX];
        const char *src = lines[i];
        if (old) {
            /* First occurrence replace only */
            const char *hit = 0;
            if (old_len == 0)
                hit = src;
            else {
                for (const char *q = src; *q; q++) {
                    if (!memcmp(q, old, old_len)) {
                        hit = q;
                        break;
                    }
                }
            }
            if (hit) {
                size_t pre = (size_t)(hit - src);
                size_t o = 0;
                for (size_t k = 0; k < pre && o + 1 < sizeof(out); k++)
                    out[o++] = src[k];
                for (size_t k = 0; k < new_len && o + 1 < sizeof(out); k++)
                    out[o++] = newv[k];
                for (const char *q = hit + old_len; *q && o + 1 < sizeof(out); q++)
                    out[o++] = *q;
                out[o] = '\0';
                src = out;
            }
        }
        if (!quiet || do_print) {
            console_write(src);
            console_write("\n");
        }
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
    console_write(abs);
    console_write("\n");
    return 0;
}
