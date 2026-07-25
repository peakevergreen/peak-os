/* /bin text batch: fold, rev, od, split, paste, nl, tac, xargs */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"
#include "ubin.h"

#define READ_MAX 8192
#define LINE_MAX 256
#define MAX_LINES 256
#define MAX_XARGS 12
#define SPLIT_CHUNK 512

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

static int read_in(const char *path, char *buf, size_t cap, size_t *out) {
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

static unsigned peak_atou(const char *s) {
    unsigned v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10u + (unsigned)(*s++ - '0');
    return v;
}

static void put_hex2(unsigned char c) {
    static const char hx[] = "0123456789abcdef";
    console_putc(hx[c >> 4]);
    console_putc(hx[c & 0xF]);
}

static void put_oct3(unsigned char c) {
    console_putc((char)('0' + ((c >> 6) & 7)));
    console_putc((char)('0' + ((c >> 3) & 7)));
    console_putc((char)('0' + (c & 7)));
}

static void put_off6(uint64_t off) {
    char tmp[8];
    for (int i = 5; i >= 0; i--) {
        unsigned d = (unsigned)(off & 0xF);
        tmp[i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        off >>= 4;
    }
    for (int i = 0; i < 6; i++)
        console_putc(tmp[i]);
}

static void put_dec_pad(int v, int width) {
    char tmp[16];
    int n = 0;
    int x = v < 0 ? -v : v;
    if (x == 0)
        tmp[n++] = '0';
    else {
        while (x) {
            tmp[n++] = (char)('0' + (x % 10));
            x /= 10;
        }
    }
    if (v < 0)
        tmp[n++] = '-';
    while (n < width)
        console_putc(' ');
    while (n > 0)
        console_putc(tmp[--n]);
}

/* ---- fold ---- */
int ufold_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("fold", "[-w width] [path|-]");
        return 0;
    }
    unsigned width = 80;
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            width = peak_atou(argv[++i]);
            if (width == 0)
                width = 80;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("fold", "cannot read");
        return 1;
    }
    unsigned col = 0;
    for (size_t i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') {
            console_putc('\n');
            col = 0;
            continue;
        }
        if (col >= width) {
            console_putc('\n');
            col = 0;
        }
        console_putc(c);
        col++;
    }
    return 0;
}

/* ---- rev ---- */
int urev_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("rev", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("rev", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(buf, len, lines, MAX_LINES);
    for (int i = 0; i < n; i++) {
        size_t L = strlen(lines[i]);
        for (size_t j = L; j > 0; j--)
            console_putc(lines[i][j - 1]);
        console_putc('\n');
    }
    return 0;
}

/* ---- od (octal/hex dump lite) ---- */
int uod_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("od", "[-tx1|-to1] [path|-]");
        return 0;
    }
    int hex = 1;
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-to1"))
            hex = 0;
        else if (!strcmp(argv[i], "-tx1") || !strcmp(argv[i], "-t"))
            hex = 1;
        else if (argv[i][0] != '-')
            path = argv[i];
    }
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("od", "cannot read");
        return 1;
    }
    for (size_t off = 0; off < len; off += 16) {
        put_off6((uint64_t)off);
        console_putc(' ');
        for (size_t i = 0; i < 16 && off + i < len; i++) {
            unsigned char c = (unsigned char)buf[off + i];
            console_putc(' ');
            if (hex)
                put_hex2(c);
            else
                put_oct3(c);
        }
        console_putc('\n');
    }
    put_off6((uint64_t)len);
    console_putc('\n');
    return 0;
}

/* ---- split ---- */
int usplit_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("split", "[-b bytes] <path> [prefix]");
        return argc < 2 ? 1 : 0;
    }
    unsigned chunk = SPLIT_CHUNK;
    const char *path = NULL;
    const char *prefix = "x";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            chunk = peak_atou(argv[++i]);
            if (chunk == 0)
                chunk = SPLIT_CHUNK;
        } else if (!path) {
            path = argv[i];
        } else {
            prefix = argv[i];
        }
    }
    if (!path) {
        peak_usage("split", "[-b bytes] <path> [prefix]");
        return 1;
    }
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("split", "cannot read");
        return 1;
    }
    int part = 0;
    for (size_t off = 0; off < len; off += chunk) {
        size_t n = len - off;
        if (n > chunk)
            n = chunk;
        char out[VFS_PATH_MAX];
        char name[64];
        /* aa, ab, … style: two letters from part index */
        int a = part / 26;
        int b = part % 26;
        if (a > 25) {
            peak_perror("split", "too many parts");
            return 1;
        }
        name[0] = '\0';
        size_t ni = 0;
        for (const char *p = prefix; *p && ni + 3 < sizeof(name); p++)
            name[ni++] = *p;
        name[ni++] = (char)('a' + a);
        name[ni++] = (char)('a' + b);
        name[ni] = '\0';
        if (shell_resolve_path(name, out, sizeof(out)) != 0) {
            peak_perror("split", "bad prefix path");
            return 1;
        }
        if (vfs_write_file(out, buf + off, n) != 0) {
            peak_perror("split", "write failed");
            return 1;
        }
        console_printf("split: wrote %s (%u bytes)\n", out, (unsigned)n);
        part++;
    }
    if (len == 0) {
        char out[VFS_PATH_MAX];
        char name[64];
        size_t ni = 0;
        for (const char *p = prefix; *p && ni + 3 < sizeof(name); p++)
            name[ni++] = *p;
        name[ni++] = 'a';
        name[ni++] = 'a';
        name[ni] = '\0';
        if (shell_resolve_path(name, out, sizeof(out)) == 0)
            (void)vfs_write_file(out, "", 0);
    }
    return 0;
}

/* ---- paste ---- */
int upaste_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("paste", "<file1> <file2>");
        return argc < 3 ? 1 : 0;
    }
    char a[READ_MAX], b[READ_MAX];
    size_t al = 0, bl = 0;
    if (read_in(argv[1], a, sizeof(a), &al) != 0 ||
        read_in(argv[2], b, sizeof(b), &bl) != 0) {
        peak_perror("paste", "cannot read");
        return 1;
    }
    char *la[MAX_LINES], *lb[MAX_LINES];
    int na = split_lines(a, al, la, MAX_LINES);
    int nb = split_lines(b, bl, lb, MAX_LINES);
    int n = na > nb ? na : nb;
    for (int i = 0; i < n; i++) {
        if (i < na)
            console_write(la[i]);
        console_putc('\t');
        if (i < nb)
            console_write(lb[i]);
        console_putc('\n');
    }
    return 0;
}

/* ---- nl ---- */
int unl_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("nl", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("nl", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(buf, len, lines, MAX_LINES);
    int num = 1;
    for (int i = 0; i < n; i++) {
        if (lines[i][0]) {
            put_dec_pad(num++, 6);
            console_putc('\t');
            console_write(lines[i]);
            console_putc('\n');
        } else {
            console_putc('\n');
        }
    }
    return 0;
}

/* ---- tac ---- */
int utac_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("tac", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("tac", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(buf, len, lines, MAX_LINES);
    for (int i = n - 1; i >= 0; i--) {
        console_write(lines[i]);
        console_putc('\n');
    }
    return 0;
}

/* ---- xargs ---- */
int uxargs_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("xargs", "<command> [fixed-args...]");
        return argc < 2 ? 1 : 0;
    }
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in("-", buf, sizeof(buf), &len) != 0) {
        peak_perror("xargs", "no stdin (use pipe or <)");
        return 1;
    }
    /* Tokenize stdin on whitespace into args */
    char *toks[MAX_XARGS];
    int nt = 0;
    size_t i = 0;
    while (i < len && nt < MAX_XARGS) {
        while (i < len && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r'))
            i++;
        if (i >= len)
            break;
        toks[nt++] = buf + i;
        while (i < len && buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\n' && buf[i] != '\r')
            i++;
        if (i < len)
            buf[i++] = '\0';
    }
    if (nt == 0)
        return 0;

    /* argv: command + fixed args + tokens */
    char *av[16];
    int ac = 0;
    for (int j = 1; j < argc && ac < 15; j++)
        av[ac++] = argv[j];
    for (int j = 0; j < nt && ac < 15; j++)
        av[ac++] = toks[j];
    av[ac] = NULL;

    char path[64];
    size_t pi = 0;
    path[pi++] = '/';
    path[pi++] = 'b';
    path[pi++] = 'i';
    path[pi++] = 'n';
    path[pi++] = '/';
    for (const char *s = argv[1]; *s && pi + 1 < sizeof(path); s++)
        path[pi++] = *s;
    path[pi] = '\0';

    int rc = ubin_run(path, ac, av);
    if (rc == -999) {
        peak_perror("xargs", "unknown command");
        return 127;
    }
    return rc;
}
