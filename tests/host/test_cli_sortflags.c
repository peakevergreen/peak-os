/*
 * Host tests for Pass 61 sort/uniq/wc flag logic (numeric/reverse/uniq -c).
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

static int peak_atoi(const char *s) {
    int v = 0;
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
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

static void sort_lines(char **lines, int n, int numeric, int reverse) {
    for (int i = 1; i < n; i++) {
        char *key = lines[i];
        int j = i - 1;
        while (j >= 0) {
            int c = sort_key_cmp(lines[j], key, numeric);
            if (reverse)
                c = -c;
            if (c <= 0)
                break;
            lines[j + 1] = lines[j];
            j--;
        }
        lines[j + 1] = key;
    }
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

static int sort_unique_count(char **lines, int n) {
    int out = 0;
    for (int i = 0; i < n; i++) {
        if (i == 0 || strcmp(lines[i], lines[i - 1]))
            out++;
    }
    return out;
}

static int uniq_run_len(char **lines, int n, int start) {
    int run = 1;
    while (start + run < n && !strcmp(lines[start], lines[start + run]))
        run++;
    return run;
}

int main(void) {
    expect(sort_key_cmp("10x", "2y", 1) > 0, "numeric 10 before 2 as ints");
    expect(sort_key_cmp("abc", "abd", 0) < 0, "lex abc before abd");

    char b2[] = "10\n2\n1";
    char *l2[4];
    int n2 = split_lines(b2, strlen(b2), l2, 4);
    expect(n2 == 3, "split numeric lines");
    sort_lines(l2, n2, 1, 0);
    expect(!strcmp(l2[0], "1") && !strcmp(l2[1], "2") && !strcmp(l2[2], "10"), "numeric sort");

    char b3[] = "a\nb\nc";
    char *l3[4];
    int n3 = split_lines(b3, strlen(b3), l3, 4);
    sort_lines(l3, n3, 0, 1);
    expect(!strcmp(l3[0], "c") && !strcmp(l3[2], "a"), "reverse sort");

    char b4[] = "a\na\nb";
    char *l4[4];
    int n4 = split_lines(b4, strlen(b4), l4, 4);
    sort_lines(l4, n4, 0, 0);
    expect(sort_unique_count(l4, n4) == 2, "sort -u unique count");

    char b5[] = "x\nx\ny";
    char *l5[4];
    int n5 = split_lines(b5, strlen(b5), l5, 4);
    expect(uniq_run_len(l5, n5, 0) == 2, "uniq -c run at start");

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("ok test_cli_sortflags\n");
    return 0;
}
