#include <stdio.h>
#include <string.h>

static const char *ubin_names[] = {
#define UBIN_CMD(name, fn) name,
#include "../../kernel/user/ubin_cmds.def"
#undef UBIN_CMD
    NULL
};

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int ubin_def_count(void) {
    int n = 0;
#define UBIN_CMD(name, fn) n++;
#include "../../kernel/user/ubin_cmds.def"
#undef UBIN_CMD
    return n;
}

int main(void) {
    const int expected = ubin_def_count();
    int n = 0;
    while (ubin_names[n])
        n++;
    expect(n == expected, "ubin count matches ubin_cmds.def");
    expect(expected >= 25, "minimum built-in count");

    int found_nslookup = 0, found_printf = 0, found_test = 0;
    for (int i = 0; i < n; i++) {
        expect(ubin_names[i][0], "non-empty name");
        if (!strcmp(ubin_names[i], "nslookup"))
            found_nslookup = 1;
        if (!strcmp(ubin_names[i], "printf"))
            found_printf = 1;
        if (!strcmp(ubin_names[i], "test"))
            found_test = 1;
        for (int j = i + 1; j < n; j++)
            expect(strcmp(ubin_names[i], ubin_names[j]) != 0, "duplicate built-in name");
    }
    expect(found_nslookup, "nslookup in registry");
    expect(found_printf, "printf in registry");
    expect(found_test, "test in registry");

    int found_disksave = 0;
    for (int i = 0; i < n; i++)
        if (!strcmp(ubin_names[i], "disksave"))
            found_disksave = 1;
    expect(found_disksave, "disksave present in registry");

    if (fails) {
        fprintf(stderr, "%d ubin registry test(s) failed\n", fails);
        return 1;
    }
    printf("test_ubin_registry: ok (%d cmds)\n", n);
    return 0;
}
