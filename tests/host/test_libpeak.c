/*
 * Host tests for kernel/user/libpeak.c argv helpers.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../kernel/include/libpeak.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    expect(peak_atoi("0") == 0, "atoi 0");
    expect(peak_atoi("42") == 42, "atoi 42");
    expect(peak_atoi("-7") == -7, "atoi -7");
    expect(peak_atoi("") == 0, "atoi empty");
    expect(peak_atoi("12x") == 12, "atoi stops at non-digit");

    char *a1[] = { "rm", "-rf", "x", NULL };
    expect(peak_has_flag(3, a1, "-r") == 1, "clustered -r in -rf");
    expect(peak_has_flag(3, a1, "-f") == 1, "clustered -f in -rf");
    expect(peak_has_flag(3, a1, "-x") == 0, "no -x in -rf");

    char *a2[] = { "head", "-n", "5", "f", NULL };
    expect(peak_has_flag(4, a2, "-n") == 1, "exact -n");
    expect(strcmp(peak_flag_arg(4, a2, "-n"), "5") == 0, "flag_arg -n");
    expect(peak_flag_arg(4, a2, "-z") == NULL, "missing flag_arg");

    char *a3[] = { "cat", "-h", NULL };
    expect(peak_wants_help(2, a3) == 1, "-h help");
    char *a4[] = { "cat", "--help", NULL };
    expect(peak_wants_help(2, a4) == 1, "--help");
    char *a5[] = { "cat", "f", NULL };
    expect(peak_wants_help(2, a5) == 0, "no help");

    {
        char buf[64];
        char *j1[] = { "ask", "hello world", NULL };
        expect(peak_join_args(2, j1, 1, buf, sizeof(buf)) == 11, "join single len");
        expect(strcmp(buf, "hello world") == 0, "join single");
        char *j2[] = { "ask", "create", "fib.c", NULL };
        expect(peak_join_args(3, j2, 1, buf, sizeof(buf)) == 12, "join multi len");
        expect(strcmp(buf, "create fib.c") == 0, "join multi");
        expect(peak_join_args(1, j1, 1, buf, sizeof(buf)) == 0, "join empty range");
        expect(buf[0] == '\0', "join empty nul");
        expect(peak_join_args(2, j1, 1, NULL, 8) == (size_t)-1, "join bad buf");
    }

    if (fails) {
        fprintf(stderr, "%d libpeak test(s) failed\n", fails);
        return 1;
    }
    printf("test_libpeak: ok\n");
    return 0;
}
