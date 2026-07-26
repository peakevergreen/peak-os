/*
 * Host tests for Pass 60 find match helpers (-iname / -maxdepth).
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

static int icase_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (fold(*a) != fold(*b))
            return 0;
    }
    return *a == 0 && *b == 0;
}

static int visit_ok(int depth, int maxdepth) {
    if (maxdepth >= 0 && depth > maxdepth)
        return 0;
    return 1;
}

static int descend_ok(int depth, int maxdepth) {
    if (maxdepth >= 0 && depth >= maxdepth)
        return 0;
    return 1;
}

static int simulate_maxdepth(int maxdepth) {
    int visits = 0;
    for (int d = 0; d <= 4; d++) {
        if (visit_ok(d, maxdepth))
            visits++;
    }
    return visits;
}

int main(void) {
    expect(icase_eq("README.md", "readme.md") == 1, "iname exact icase");
    expect(icase_eq("Foo", "foo") == 1, "short icase");
    expect(icase_eq("Foo", "Food") == 0, "icase length");
    expect(icase_eq("a", "A") == 1, "single char icase");

    expect(simulate_maxdepth(0) == 1, "maxdepth 0 one level");
    expect(simulate_maxdepth(1) == 2, "maxdepth 1 two levels");
    expect(simulate_maxdepth(-1) == 5, "maxdepth unset all levels");
    expect(descend_ok(0, 0) == 0, "no descend at depth 0 cap");
    expect(descend_ok(0, 1) == 1, "descend at depth 0 cap 1");

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("ok test_cli_find\n");
    return 0;
}
