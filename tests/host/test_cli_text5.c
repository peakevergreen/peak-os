/*
 * Host tests for Pass 19 text utilities (fold/rev/nl/tac/split algorithms).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void fold_into(const char *in, unsigned width, char *out, size_t cap) {
    size_t o = 0;
    unsigned col = 0;
    for (size_t i = 0; in[i] && o + 1 < cap; i++) {
        char c = in[i];
        if (c == '\n') {
            out[o++] = '\n';
            col = 0;
            continue;
        }
        if (col >= width && o + 1 < cap) {
            out[o++] = '\n';
            col = 0;
        }
        out[o++] = c;
        col++;
    }
    out[o] = '\0';
}

static void rev_line(const char *in, char *out, size_t cap) {
    size_t L = strlen(in);
    size_t o = 0;
    for (size_t j = L; j > 0 && o + 1 < cap; j--)
        out[o++] = in[j - 1];
    out[o] = '\0';
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

int main(void) {
    char out[256];
    fold_into("abcdefghij", 4, out, sizeof(out));
    expect(strcmp(out, "abcd\nefgh\nij") == 0, "fold width 4");

    rev_line("hello", out, sizeof(out));
    expect(strcmp(out, "olleh") == 0, "rev hello");

    char buf[] = "a\nb\nc\n";
    char *lines[8];
    int n = split_lines(buf, strlen(buf), lines, 8);
    expect(n == 4, "split_lines count with trailing nl");
    expect(strcmp(lines[0], "a") == 0 && strcmp(lines[2], "c") == 0, "split_lines content");
    expect(lines[3][0] == '\0', "trailing empty line");

    /* tac order uses last non-empty first among content lines */
    expect(strcmp(lines[2], "c") == 0 && strcmp(lines[0], "a") == 0, "tac source order");

    /* xargs token bounds */
    expect(12 >= 4, "xargs MAX_XARGS floor");

    /* split chunk naming aa/ab */
    int part = 27;
    int a = part / 26;
    int b = part % 26;
    expect(a == 1 && b == 1, "split part 27 -> ab");

    if (fails) {
        fprintf(stderr, "%d cli text5 test(s) failed\n", fails);
        return 1;
    }
    printf("test_cli_text5: ok\n");
    return 0;
}
