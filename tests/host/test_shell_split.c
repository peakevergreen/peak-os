/*
 * Host tests for quote-aware shell_split_args.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../kernel/include/shell_split.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void check_split(const char *input, int want_argc, const char **want) {
    char buf[256];
    size_t n = strlen(input);
    if (n + 1 > sizeof(buf)) {
        expect(0, "input too long");
        return;
    }
    memcpy(buf, input, n + 1);
    char *argv[16];
    int argc = shell_split_args(buf, argv, 16);
    expect(argc == want_argc, input);
    if (argc != want_argc)
        return;
    for (int i = 0; i < want_argc; i++)
        expect(strcmp(argv[i], want[i]) == 0, want[i]);
    expect(argv[argc] == NULL, "NULL terminator");
}

int main(void) {
    {
        const char *w[] = { "ls", "-l" };
        check_split("ls -l", 2, w);
    }
    {
        const char *w[] = { "ask", "hello world" };
        check_split("ask \"hello world\"", 2, w);
    }
    {
        const char *w[] = { "js", "-e", "1+2" };
        check_split("js -e '1+2'", 3, w);
    }
    {
        const char *w[] = { "echo", "a  b" };
        check_split("echo 'a  b'", 2, w);
    }
    {
        /* Unclosed quote: remainder is one arg */
        const char *w[] = { "ask", "partial" };
        check_split("ask \"partial", 2, w);
    }
    {
        const char *w[] = { "one" };
        check_split("   one   ", 1, w);
    }
    {
        char buf[64];
        strcpy(buf, "a b c d e f g h i j k l m n o p q");
        char *argv[4];
        int argc = shell_split_args(buf, argv, 4);
        expect(argc == 3, "argv cap max-1");
        expect(argv[3] == NULL, "cap NULL");
    }
    expect(shell_split_args(NULL, NULL, 16) == 0, "null cmd");

    if (fails) {
        fprintf(stderr, "%d shell_split test(s) failed\n", fails);
        return 1;
    }
    printf("test_shell_split: ok\n");
    return 0;
}
