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
        /* Empty input / whitespace-only */
        char empty[] = "";
        char *av0[16];
        expect(shell_split_args(empty, av0, 16) == 0, "empty");
        char sp[] = "   ";
        char *av[16];
        expect(shell_split_args(sp, av, 16) == 0, "spaces only");
    }
    {
        /* Empty quoted args */
        const char *w[] = { "echo", "" };
        check_split("echo \"\"", 2, w);
        check_split("echo ''", 2, w);
    }
    {
        /* Adjacent quoted tokens */
        const char *w[] = { "a", "b" };
        check_split("\"a\"\"b\"", 2, w);
    }
    {
        /* Mixed quote styles on one line */
        const char *w[] = { "cp", "src x", "dst y" };
        check_split("cp \"src x\" 'dst y'", 3, w);
    }
    {
        /* Leading/trailing spaces around quoted arg */
        const char *w[] = { "ask", "hi there" };
        check_split("  ask  \"hi there\"  ", 2, w);
    }
    {
        /* In-place: argv aliases into mutated buffer (no heap) */
        char buf[] = "echo 'x y'";
        char *argv[8];
        int argc = shell_split_args(buf, argv, 8);
        expect(argc == 2, "inplace argc");
        expect(argv[0] == buf, "argv0 aliases buf");
        expect(argv[1] > buf && argv[1] < buf + sizeof(buf), "argv1 in buf");
        expect(strcmp(argv[0], "echo") == 0, "inplace echo");
        expect(strcmp(argv[1], "x y") == 0, "inplace quoted");
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
    {
        char buf[] = "x";
        char *av[2] = {0};
        expect(shell_split_args(buf, av, 1) == 0, "max < 2");
    }

    /* Pipeline / redirect parse */
    {
        char buf[] = "echo hello | grep hello";
        struct shell_pipeline pl;
        expect(shell_parse_pipeline(buf, &pl) == 0, "pipe parse");
        expect(pl.nstages == 2, "pipe stages");
        expect(pl.stages[0].argc == 2, "stage0 argc");
        expect(strcmp(pl.stages[0].argv[0], "echo") == 0, "stage0 cmd");
        expect(strcmp(pl.stages[0].argv[1], "hello") == 0, "stage0 arg");
        expect(pl.stages[1].argc == 2, "stage1 argc");
        expect(strcmp(pl.stages[1].argv[0], "grep") == 0, "stage1 cmd");
    }
    {
        char buf[] = "echo hi > out.txt";
        struct shell_pipeline pl;
        expect(shell_parse_pipeline(buf, &pl) == 0, "redir out parse");
        expect(pl.nstages == 1, "redir stages");
        expect(pl.stages[0].argc == 2, "redir argc");
        expect(pl.stages[0].redir_out.kind == SHELL_REDIR_OUT, "redir kind");
        expect(strcmp(pl.stages[0].redir_out.path, "out.txt") == 0, "redir path");
    }
    {
        char buf[] = "cat <in.txt >>out.txt";
        struct shell_pipeline pl;
        expect(shell_parse_pipeline(buf, &pl) == 0, "redir in/append");
        expect(pl.stages[0].redir_in.kind == SHELL_REDIR_IN, "redir in");
        expect(strcmp(pl.stages[0].redir_in.path, "in.txt") == 0, "in path");
        expect(pl.stages[0].redir_out.kind == SHELL_REDIR_APPEND, "append");
        expect(strcmp(pl.stages[0].redir_out.path, "out.txt") == 0, "append path");
    }
    {
        char buf[] = "echo>f";
        struct shell_pipeline pl;
        expect(shell_parse_pipeline(buf, &pl) == 0, "abut >");
        expect(pl.stages[0].argc == 1, "abut argc");
        expect(pl.stages[0].redir_out.kind == SHELL_REDIR_OUT, "abut kind");
        expect(strcmp(pl.stages[0].redir_out.path, "f") == 0, "abut path");
    }
    {
        char buf[] = "echo a |";
        struct shell_pipeline pl;
        expect(shell_parse_pipeline(buf, &pl) != 0, "bare pipe fail");
    }
    {
        char buf[] = "echo \"a | b\" > x";
        struct shell_pipeline pl;
        expect(shell_parse_pipeline(buf, &pl) == 0, "quoted pipe");
        expect(pl.nstages == 1, "quoted no split");
        expect(strcmp(pl.stages[0].argv[1], "a | b") == 0, "quoted keeps |");
    }

    if (fails) {
        fprintf(stderr, "%d shell_split test(s) failed\n", fails);
        return 1;
    }
    printf("test_shell_split: ok\n");
    return 0;
}
