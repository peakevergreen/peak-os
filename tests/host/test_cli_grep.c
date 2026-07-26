/*
 * Host tests for Pass 59 grep match helpers (-i / -v / line scan).
 */
#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static char fold(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

static int line_match(const char *line, size_t llen, const char *pat, size_t plen, int icase) {
    if (plen == 0)
        return 1;
    if (llen < plen)
        return 0;
    for (size_t j = 0; j + plen <= llen; j++) {
        size_t k = 0;
        for (; k < plen; k++) {
            char a = line[j + k], b = pat[k];
            if (icase) {
                a = fold(a);
                b = fold(b);
            }
            if (a != b)
                break;
        }
        if (k == plen)
            return 1;
    }
    return 0;
}

int main(void) {
    expect(line_match("Hello World", 11, "hello", 5, 1) == 1, "icase hello");
    expect(line_match("Hello World", 11, "hello", 5, 0) == 0, "case sensitive miss");
    expect(line_match("abc", 3, "a", 1, 0) == 1, "prefix match");
    expect((!line_match("abc", 3, "z", 1, 0)) == 1, "invert miss is match");
    expect(line_match("", 0, "", 0, 0) == 1, "empty pat matches");
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("ok test_cli_grep\n");
    return 0;
}
