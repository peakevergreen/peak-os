/* /bin text utilities: cat, head, tail, wc, grep, hexdump, strings, echo, clear. */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define READ_MAX 8192

static int read_abs(const char *abs, char *buf, size_t cap, size_t *out) {
    size_t n = 0;
    if (vfs_read_file(abs, buf, cap - 1, &n) != 0)
        return -1;
    buf[n] = '\0';
    *out = n;
    return 0;
}

/* Resolve path or "-" / missing → shell stdin (pipes / < redirect). */
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

static int is_printable(unsigned char c) {
    return c >= 32 && c < 127;
}

int ucat_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("cat", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs))) {
        peak_perror("cat", "cannot open");
        return 1;
    }
    char data[READ_MAX];
    size_t len = 0;
    if (read_abs(abs, data, sizeof(data), &len) != 0) {
        peak_perror("cat", "cannot read");
        return 1;
    }
    for (size_t i = 0; i < len; i++)
        console_putc(data[i]);
    if (len == 0 || data[len - 1] != '\n')
        console_write("\n");
    return 0;
}

int uhead_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("head", "[-n N] <path>");
        return 0;
    }
    int n = 10;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            n = peak_atoi(argv[++i]);
            continue;
        }
        if (argv[i][0] != '-')
            path = argv[i];
    }
    if (!path)
        path = "-";
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs)))
        return 1;
    char data[READ_MAX];
    size_t len = 0;
    if (read_abs(abs, data, sizeof(data), &len) != 0)
        return 1;
    int lines = 0;
    for (size_t i = 0; i < len && lines < n; i++) {
        console_putc(data[i]);
        if (data[i] == '\n')
            lines++;
    }
    return 0;
}

int utail_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("tail", "[-n N] <path>");
        return 0;
    }
    int n = 10;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            n = peak_atoi(argv[++i]);
            continue;
        }
        if (argv[i][0] != '-')
            path = argv[i];
    }
    if (!path)
        path = "-";
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs)))
        return 1;
    char data[READ_MAX];
    size_t len = 0;
    if (read_abs(abs, data, sizeof(data), &len) != 0)
        return 1;
    int total = 0;
    for (size_t i = 0; i < len; i++)
        if (data[i] == '\n')
            total++;
    if (len && data[len - 1] != '\n')
        total++;
    int skip = total > n ? total - n : 0;
    int cur = 0;
    size_t i = 0;
    while (i < len && cur < skip) {
        if (data[i++] == '\n')
            cur++;
    }
    for (; i < len; i++)
        console_putc(data[i]);
    return 0;
}

int uwc_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("wc", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs)))
        return 1;
    char data[READ_MAX];
    size_t len = 0;
    if (read_abs(abs, data, sizeof(data), &len) != 0)
        return 1;
    size_t lines = 0, words = 0;
    int in_word = 0;
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n')
            lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    console_printf("%lu %lu %lu %s\n", (uint64_t)lines, (uint64_t)words, (uint64_t)len, abs);
    return 0;
}

static char grep_fold(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

static int grep_line_match(const char *line, size_t llen, const char *pat, size_t plen,
                           int icase) {
    if (plen == 0)
        return 1;
    if (llen < plen)
        return 0;
    for (size_t j = 0; j + plen <= llen; j++) {
        size_t k = 0;
        for (; k < plen; k++) {
            char a = line[j + k], b = pat[k];
            if (icase) {
                a = grep_fold(a);
                b = grep_fold(b);
            }
            if (a != b)
                break;
        }
        if (k == plen)
            return 1;
    }
    return 0;
}

struct grep_opts {
    const char *pat;
    size_t plen;
    int icase;
    int show_n;
    int invert;
    int show_path;
    int matches;
};

static void grep_emit_line(const struct grep_opts *o, const char *path, int lineno,
                           const char *line, size_t llen) {
    if (o->show_path && path && path[0]) {
        console_write(path);
        console_putc(':');
    }
    if (o->show_n)
        console_printf("%d:", lineno);
    for (size_t j = 0; j < llen; j++)
        console_putc(line[j]);
    console_putc('\n');
}

static int grep_file(struct grep_opts *o, const char *abs) {
    char data[READ_MAX];
    size_t len = 0;
    if (read_abs(abs, data, sizeof(data), &len) != 0)
        return -1;
    size_t start = 0;
    int lineno = 1;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || data[i] == '\n') {
            size_t l = i - start;
            int match = grep_line_match(data + start, l, o->pat, o->plen, o->icase);
            if (o->invert)
                match = !match;
            if (match) {
                grep_emit_line(o, abs, lineno, data + start, l);
                o->matches++;
            }
            lineno++;
            start = i + 1;
        }
    }
    return 0;
}

struct grep_walk_ctx {
    struct grep_opts *opts;
};

static int grep_walk_cb(const char *path, struct vfs_node *node, void *ud) {
    struct grep_walk_ctx *gc = ud;
    if (!node || node->type != VFS_FILE)
        return 0;
    grep_file(gc->opts, path);
    return 0;
}

int ugrep_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("grep", "[-i] [-n] [-v] [-r] <pattern> [path...]");
        return 0;
    }
    int icase = 0, show_n = 0, invert = 0, recur = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1]) {
        if (!strcmp(argv[argi], "-"))
            break;
        const char *f = argv[argi] + 1;
        int known = 1;
        for (; *f; f++) {
            if (*f == 'i')
                icase = 1;
            else if (*f == 'n')
                show_n = 1;
            else if (*f == 'v')
                invert = 1;
            else if (*f == 'r' || *f == 'R')
                recur = 1;
            else {
                known = 0;
                break;
            }
        }
        if (!known)
            break;
        argi++;
    }
    if (argi >= argc) {
        peak_usage("grep", "[-i] [-n] [-v] [-r] <pattern> [path...]");
        return 1;
    }
    const char *pat = argv[argi++];
    struct grep_opts opts = {
        .pat = pat,
        .plen = strlen(pat),
        .icase = icase,
        .show_n = show_n,
        .invert = invert,
        .show_path = 0,
        .matches = 0,
    };
    int npaths = argc - argi;
    if (npaths <= 0) {
        char abs[VFS_PATH_MAX];
        if (resolve_in_path("-", abs, sizeof(abs)))
            return 1;
        if (grep_file(&opts, abs) != 0)
            return 1;
        return opts.matches ? 0 : 1;
    }
    if (npaths > 1 || recur)
        opts.show_path = 1;
    for (int i = argi; i < argc; i++) {
        char abs[VFS_PATH_MAX];
        if (resolve_in_path(argv[i], abs, sizeof(abs)))
            return 1;
        if (recur && vfs_is_dir(abs)) {
            struct grep_walk_ctx gc = { .opts = &opts };
            vfs_walk(abs, grep_walk_cb, &gc);
        } else if (grep_file(&opts, abs) != 0) {
            return 1;
        }
    }
    return opts.matches ? 0 : 1;
}

int uhexdump_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("hexdump", "<path>");
        return argc < 2 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)))
        return 1;
    char data[READ_MAX];
    size_t len = 0;
    if (read_abs(abs, data, sizeof(data), &len) != 0)
        return 1;
    for (size_t off = 0; off < len; off += 16) {
        console_printf("%lx  ", (uint64_t)off);
        for (size_t i = 0; i < 16; i++) {
            if (off + i < len)
                console_printf("%x ", (unsigned)(unsigned char)data[off + i]);
            else
                console_write("   ");
        }
        console_write(" |");
        for (size_t i = 0; i < 16 && off + i < len; i++) {
            unsigned char c = (unsigned char)data[off + i];
            console_putc(is_printable(c) ? (char)c : '.');
        }
        console_write("|\n");
    }
    return 0;
}

int ustrings_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("strings", "<path>");
        return argc < 2 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)))
        return 1;
    char data[READ_MAX];
    size_t len = 0;
    if (read_abs(abs, data, sizeof(data), &len) != 0)
        return 1;
    size_t run = 0;
    char buf[80];
    for (size_t i = 0; i < len; i++) {
        if (is_printable((unsigned char)data[i])) {
            if (run < sizeof(buf) - 1)
                buf[run++] = data[i];
        } else {
            if (run >= 4) {
                buf[run] = 0;
                console_write(buf);
                console_write("\n");
            }
            run = 0;
        }
    }
    if (run >= 4) {
        buf[run] = 0;
        console_write(buf);
        console_write("\n");
    }
    return 0;
}

int uecho_main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            console_putc(' ');
        console_write(argv[i]);
    }
    console_write("\n");
    return 0;
}

int uclear_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_clear();
    return 0;
}
