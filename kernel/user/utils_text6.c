/* /bin awk-lite: field split, $0/$n, print, pattern { }, NR/NF */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define READ_MAX 32768
#define LINE_MAX 256
#define MAX_LINES 256
#define MAX_FIELDS 32
#define FS_DEFAULT ' '

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

static int split_fields(char *line, char fs, char **fields, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == fs || *p == '\t')
            p++;
        if (!*p)
            break;
        fields[n++] = p;
        while (*p && *p != fs && *p != '\t')
            p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    return n;
}

/* Match simple substring pattern, or empty = always. */
static int pattern_match(const char *pat, const char *line) {
    if (!pat || !pat[0])
        return 1;
    size_t plen = strlen(pat);
    size_t llen = strlen(line);
    if (plen > llen)
        return 0;
    for (size_t i = 0; i + plen <= llen; i++) {
        if (!memcmp(line + i, pat, plen))
            return 1;
    }
    return 0;
}

/*
 * Evaluate print expression: print, print $1, print $1,$2, print NR, print NF
 * Comma-separated; spaces optional. Fields already split into f[]/nf; $0 = line.
 */
static void awk_print(const char *expr, const char *line, char **f, int nf, int nr) {
    if (!expr || !expr[0] || !strcmp(expr, "print")) {
        console_write(line);
        console_putc('\n');
        return;
    }
    /* skip leading "print" */
    const char *p = expr;
    if (!strncmp(p, "print", 5)) {
        p += 5;
        while (*p == ' ')
            p++;
    }
    if (!*p) {
        console_write(line);
        console_putc('\n');
        return;
    }
    int first = 1;
    while (*p) {
        while (*p == ' ' || *p == ',')
            p++;
        if (!*p)
            break;
        if (!first)
            console_putc(' ');
        first = 0;
        if (*p == '$') {
            p++;
            if (*p == '0') {
                console_write(line);
                p++;
            } else if (*p >= '1' && *p <= '9') {
                int idx = 0;
                while (*p >= '0' && *p <= '9')
                    idx = idx * 10 + (*p++ - '0');
                if (idx >= 1 && idx <= nf)
                    console_write(f[idx - 1]);
            } else {
                console_write("$");
            }
        } else if (!strncmp(p, "NR", 2) && (p[2] == '\0' || p[2] == ' ' || p[2] == ',')) {
            console_printf("%d", nr);
            p += 2;
        } else if (!strncmp(p, "NF", 2) && (p[2] == '\0' || p[2] == ' ' || p[2] == ',')) {
            console_printf("%d", nf);
            p += 2;
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                console_putc(*p);
                p++;
            }
            if (*p == '"')
                p++;
        } else {
            /* bare word: print until comma/space */
            while (*p && *p != ' ' && *p != ',') {
                console_putc(*p);
                p++;
            }
        }
    }
    console_putc('\n');
}

/*
 * Program forms:
 *   '{ print }'
 *   '{ print $1 }'
 *   '/pat/ { print $0 }'
 *   '{ print NR, NF }'
 */
static int parse_prog(const char *prog, char *pat_out, size_t pat_cap,
                      char *body_out, size_t body_cap) {
    pat_out[0] = '\0';
    body_out[0] = '\0';
    const char *p = prog;
    while (*p == ' ')
        p++;
    if (*p == '/') {
        p++;
        size_t i = 0;
        while (*p && *p != '/' && i + 1 < pat_cap)
            pat_out[i++] = *p++;
        pat_out[i] = '\0';
        if (*p == '/')
            p++;
        while (*p == ' ')
            p++;
    }
    if (*p != '{') {
        /* bare: treat whole as body print expr — e.g. awk '{print}' already braced */
        size_t i = 0;
        while (*p && i + 1 < body_cap)
            body_out[i++] = *p++;
        body_out[i] = '\0';
        return 0;
    }
    p++;
    while (*p == ' ')
        p++;
    size_t i = 0;
    while (*p && *p != '}' && i + 1 < body_cap)
        body_out[i++] = *p++;
    body_out[i] = '\0';
    /* trim trailing spaces */
    while (i > 0 && (body_out[i - 1] == ' ' || body_out[i - 1] == '\t'))
        body_out[--i] = '\0';
    return 0;
}

int uawk_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("awk", "[-F fs] '<prog>' [path|-]");
        return argc < 2 ? 1 : 0;
    }
    char fs = FS_DEFAULT;
    const char *prog = NULL;
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-F") && i + 1 < argc) {
            fs = argv[++i][0] ? argv[i][0] : FS_DEFAULT;
        } else if (!prog) {
            prog = argv[i];
        } else {
            path = argv[i];
        }
    }
    if (!prog) {
        peak_usage("awk", "[-F fs] '<prog>' [path|-]");
        return 1;
    }

    char pat[128], body[128];
    parse_prog(prog, pat, sizeof(pat), body, sizeof(body));
    if (!body[0]) {
        /* default action */
        size_t j = 0;
        const char *def = "print";
        for (; def[j] && j + 1 < sizeof(body); j++)
            body[j] = def[j];
        body[j] = '\0';
    }

    char data[READ_MAX];
    size_t len = 0;
    if (read_in(path, data, sizeof(data), &len) != 0) {
        peak_perror("awk", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int nlines = split_lines(data, len, lines, MAX_LINES);

    for (int li = 0; li < nlines; li++) {
        char line_copy[LINE_MAX];
        size_t ll = strlen(lines[li]);
        if (ll >= sizeof(line_copy))
            ll = sizeof(line_copy) - 1;
        memcpy(line_copy, lines[li], ll);
        line_copy[ll] = '\0';

        if (!pattern_match(pat, line_copy))
            continue;

        char field_buf[LINE_MAX];
        memcpy(field_buf, line_copy, ll + 1);
        char *fields[MAX_FIELDS];
        int nf = split_fields(field_buf, fs, fields, MAX_FIELDS);
        awk_print(body, line_copy, fields, nf, li + 1);
    }
    return 0;
}
