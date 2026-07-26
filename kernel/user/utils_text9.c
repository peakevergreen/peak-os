/* /bin fmt, column, expand, unexpand — lite text layout */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define READ_MAX 8192

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

static int line_blank(const char *s) {
    for (; *s; s++)
        if (*s != ' ' && *s != '\t' && *s != '\r')
            return 0;
    return 1;
}

int ufmt_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("fmt", "[-w width] [path|-]");
        return 0;
    }
    unsigned width = 75;
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            width = (unsigned)peak_atoi(argv[++i]);
            if (width == 0)
                width = 75;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("fmt", "cannot read");
        return 1;
    }
    unsigned col = 0;
    int in_para = 0;
    for (size_t i = 0; i <= len; i++) {
        char c = (i < len) ? buf[i] : '\n';
        if (c == '\r')
            continue;
        if (c == '\n') {
            size_t start = i;
            while (start > 0 && buf[start - 1] != '\n')
                start--;
            if (line_blank(buf + start)) {
                if (in_para)
                    console_write("\n");
                console_write("\n");
                col = 0;
                in_para = 0;
            } else if (in_para) {
                console_putc(' ');
                col++;
            }
            continue;
        }
        if (!in_para && (c == ' ' || c == '\t'))
            continue;
        if (col >= width && col > 0) {
            console_write("\n");
            col = 0;
        }
        console_putc(c);
        col++;
        in_para = 1;
    }
    return 0;
}

#define COL_MAX 32
#define COL_LINE 256

int ucolumn_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("column", "[-t] [path|-]");
        return 0;
    }
    int tab_in = peak_has_flag(argc, argv, "-t");
    const char *path = "-";
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-')
            path = argv[i];
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("column", "cannot read");
        return 1;
    }
    char cells[COL_MAX][COL_LINE];
    int row = 0;
    int col = 0;
    int max_col = 0;
    int widths[COL_MAX];
    for (int i = 0; i < COL_MAX; i++)
        widths[i] = 0;
    size_t i = 0;
    while (i <= len) {
        char c = (i < len) ? buf[i] : '\n';
        if (c == '\r') {
            i++;
            continue;
        }
        if (c == '\n' || (tab_in && c == '\t')) {
            if (col < COL_MAX && col < COL_LINE)
                cells[row][col] = '\0';
            if (col + 1 > max_col)
                max_col = col + 1;
            if (c == '\n') {
                row++;
                col = 0;
                if (row >= COL_MAX)
                    break;
            } else
                col++;
            i++;
            continue;
        }
        if (row >= COL_MAX || col >= COL_MAX)
            break;
        size_t pos = 0;
        while (i < len && buf[i] != '\n' && buf[i] != '\r' &&
               (!tab_in || buf[i] != '\t')) {
            if (pos + 1 < COL_LINE)
                cells[row][pos++] = buf[i];
            i++;
        }
        cells[row][pos] = '\0';
        if ((int)pos > widths[col])
            widths[col] = (int)pos;
        col++;
    }
    if (max_col == 0)
        return 0;
    int nrows = row;
    if (col > 0 && row < COL_MAX)
        nrows = row + 1;
    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < max_col; c++) {
            const char *s = (c < COL_MAX) ? cells[r] : "";
            if (!s[0] && r >= row)
                continue;
            console_write(s);
            if (c + 1 < max_col) {
                int pad = widths[c] - (int)strlen(s);
                if (pad < 1)
                    pad = 1;
                for (int p = 0; p < pad; p++)
                    console_putc(' ');
            }
        }
        console_write("\n");
    }
    return 0;
}

int uexpand_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("expand", "[-t tabstop] [path|-]");
        return 0;
    }
    unsigned tabstop = 8;
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            tabstop = (unsigned)peak_atoi(argv[++i]);
            if (tabstop == 0)
                tabstop = 8;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("expand", "cannot read");
        return 1;
    }
    unsigned col = 0;
    for (size_t i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\t') {
            unsigned next = ((col / tabstop) + 1) * tabstop;
            while (col < next) {
                console_putc(' ');
                col++;
            }
        } else {
            console_putc(c);
            if (c == '\n')
                col = 0;
            else
                col++;
        }
    }
    return 0;
}

int uunexpand_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("unexpand", "[-t tabstop] [path|-]");
        return 0;
    }
    unsigned tabstop = 8;
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            tabstop = (unsigned)peak_atoi(argv[++i]);
            if (tabstop == 0)
                tabstop = 8;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("unexpand", "cannot read");
        return 1;
    }
    unsigned col = 0;
    unsigned spaces = 0;
    for (size_t i = 0; i <= len; i++) {
        char c = (i < len) ? buf[i] : '\n';
        if (c == ' ' && col % tabstop != 0) {
            spaces++;
            col++;
            continue;
        }
        if (spaces > 0) {
            while (spaces >= tabstop) {
                console_putc('\t');
                spaces -= tabstop;
                col = ((col / tabstop) + 1) * tabstop;
            }
            while (spaces > 0) {
                console_putc(' ');
                spaces--;
                col++;
            }
        }
        if (c == ' ')
            continue;
        if (c == '\n' || i == len) {
            console_putc('\n');
            col = 0;
            spaces = 0;
            if (i == len)
                break;
            continue;
        }
        console_putc(c);
        col++;
    }
    return 0;
}
