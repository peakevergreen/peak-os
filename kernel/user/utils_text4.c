/* /bin: printf, tee, test, [, yes */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define READ_MAX 32768
#define YES_MAX_LINES 256

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
    if (vfs_read_file(abs, buf, cap, &n) != 0)
        return -1;
    if (n < cap)
        buf[n] = '\0';
    *out = n;
    return 0;
}

static void console_write_n(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        console_putc(s[i]);
}

static unsigned peak_atou(const char *s) {
    unsigned v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10u + (unsigned)(*s++ - '0');
    return v;
}

static void hex_lower(unsigned v, int width) {
    char tmp[16];
    int i = 0;
    if (v == 0)
        tmp[i++] = '0';
    else {
        while (v) {
            unsigned d = v & 0xFu;
            tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            v >>= 4;
        }
    }
    while (i < width)
        tmp[i++] = '0';
    while (i > 0)
        console_putc(tmp[--i]);
}

int uprintf_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("printf", "<format> [args...]");
        console_write("  escapes: \\n \\t \\\\n");
        return argc < 2 ? 1 : 0;
    }
    const char *fmt = argv[1];
    int argi = 2;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            if (*p == '\\' && p[1]) {
                p++;
                if (*p == 'n')
                    console_putc('\n');
                else if (*p == 't')
                    console_putc('\t');
                else if (*p == '\\')
                    console_putc('\\');
                else
                    console_putc(*p);
                continue;
            }
            console_putc(*p);
            continue;
        }
        p++;
        if (*p == '%') {
            console_putc('%');
            continue;
        }
        if (*p == 's') {
            if (argi >= argc) {
                peak_perror("printf", "missing arg for %s");
                return 1;
            }
            console_write(argv[argi++]);
            continue;
        }
        if (*p == 'd') {
            if (argi >= argc) {
                peak_perror("printf", "missing arg for %d");
                return 1;
            }
            console_printf("%d", peak_atoi(argv[argi++]));
            continue;
        }
        if (*p == 'u') {
            if (argi >= argc) {
                peak_perror("printf", "missing arg for %u");
                return 1;
            }
            console_printf("%u", peak_atou(argv[argi++]));
            continue;
        }
        if (*p == 'x') {
            if (argi >= argc) {
                peak_perror("printf", "missing arg for %x");
                return 1;
            }
            hex_lower(peak_atou(argv[argi++]), 0);
            continue;
        }
        peak_perror("printf", "unsupported conversion");
        return 1;
    }
    return 0;
}

static int tee_write_path(const char *path, const char *data, size_t len, int append) {
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)) != 0)
        return -1;
    if (append) {
        char old[READ_MAX];
        size_t old_n = 0;
        if (vfs_read_file(abs, old, sizeof(old), &old_n) != 0)
            old_n = 0;
        if (old_n + len > sizeof(old))
            return -1;
        memcpy(old + old_n, data, len);
        return vfs_write_file(abs, old, old_n + len);
    }
    return vfs_write_file(abs, data, len);
}

int utee_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("tee", "[-a] [file...]");
        return 0;
    }
    int append = peak_has_flag(argc, argv, "-a");
    char data[READ_MAX];
    size_t len = 0;
    if (read_in("-", data, sizeof(data), &len) != 0) {
        peak_perror("tee", "cannot read stdin");
        return 1;
    }
    console_write_n(data, len);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-a"))
            continue;
        if (argv[i][0] == '-' && argv[i][1]) {
            peak_perror("tee", "unknown option");
            return 1;
        }
        if (tee_write_path(argv[i], data, len, append) != 0) {
            peak_perror("tee", "write failed");
            return 1;
        }
    }
    return 0;
}

static int test_unary(const char *op, const char *arg) {
    if (!strcmp(op, "-f"))
        return vfs_is_file(arg);
    if (!strcmp(op, "-d"))
        return vfs_is_dir(arg);
    if (!strcmp(op, "-e"))
        return vfs_exists(arg);
    if (!strcmp(op, "-z"))
        return !arg[0];
    if (!strcmp(op, "-n"))
        return arg[0] != '\0';
    return -1;
}

static int test_binary(const char *a, const char *op, const char *b) {
    if (!strcmp(op, "=") || !strcmp(op, "=="))
        return !strcmp(a, b);
    if (!strcmp(op, "!="))
        return strcmp(a, b) != 0;
    int ia = peak_atoi(a);
    int ib = peak_atoi(b);
    if (!strcmp(op, "-eq"))
        return ia == ib;
    if (!strcmp(op, "-ne"))
        return ia != ib;
    if (!strcmp(op, "-lt"))
        return ia < ib;
    if (!strcmp(op, "-le"))
        return ia <= ib;
    if (!strcmp(op, "-gt"))
        return ia > ib;
    if (!strcmp(op, "-ge"))
        return ia >= ib;
    return -1;
}

static int test_eval(int argc, char **argv) {
    const char *cmd = argv[0] ? argv[0] : "test";
    int need_bracket = (cmd[0] == '[' && !cmd[1]);
    int end = argc;

    if (need_bracket) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            peak_perror("[", "missing ]");
            return 2;
        }
        end = argc - 1;
    }
    int n = end - 1;
    if (n <= 0)
        return 1;
    if (n == 2) {
        char path[VFS_PATH_MAX];
        const char *arg = argv[2];
        if (argv[1][0] == '-' &&
            (argv[1][1] == 'f' || argv[1][1] == 'd' || argv[1][1] == 'e')) {
            if (shell_resolve_path(arg, path, sizeof(path)) != 0)
                return 1;
            arg = path;
        }
        int r = test_unary(argv[1], arg);
        if (r < 0) {
            peak_perror(cmd, "unknown unary test");
            return 2;
        }
        return r ? 0 : 1;
    }
    if (n == 3) {
        int r = test_binary(argv[1], argv[2], argv[3]);
        if (r < 0) {
            peak_perror(cmd, "unknown operator");
            return 2;
        }
        return r ? 0 : 1;
    }
    peak_perror(cmd, "bad expression");
    return 2;
}

int utest_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("test", "EXPR  |  [ EXPR ]");
        return 0;
    }
    return test_eval(argc, argv);
}

int uyes_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("yes", "[string]");
        return 0;
    }
    const char *line = (argc >= 2) ? argv[1] : "y";
    for (int i = 0; i < YES_MAX_LINES; i++) {
        console_write(line);
        console_write("\n");
    }
    return 0;
}
