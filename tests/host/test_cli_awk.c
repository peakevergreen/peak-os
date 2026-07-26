/* Host tests for Pass 39 awk-lite field split / pattern match. */
#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int split_fields(char *line, char fs, char **fields, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == fs || *p == '\t')
            p++;
        if (!*p)
            break;
        fields[n++] = p;
        while (*p && *p != fs && *p != '\t')
            p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    return n;
}

static int pattern_match(const char *pat, const char *line) {
    if (!pat || !pat[0])
        return 1;
    size_t plen = strlen(pat);
    size_t llen = strlen(line);
    if (plen > llen)
        return 0;
    for (size_t i = 0; i + plen <= llen; i++) {
        if (!memcmp(line + i, pat, plen))
            return 1;
    }
    return 0;
}

static int parse_prog(const char *prog, char *pat_out, size_t pat_cap,
                      char *body_out, size_t body_cap) {
    pat_out[0] = '\0';
    body_out[0] = '\0';
    const char *p = prog;
    while (*p == ' ')
        p++;
    if (*p == '/') {
        p++;
        size_t i = 0;
        while (*p && *p != '/' && i + 1 < pat_cap)
            pat_out[i++] = *p++;
        pat_out[i] = '\0';
        if (*p == '/')
            p++;
        while (*p == ' ')
            p++;
    }
    if (*p != '{')
        return -1;
    p++;
    while (*p == ' ')
        p++;
    size_t i = 0;
    while (*p && *p != '}' && i + 1 < body_cap)
        body_out[i++] = *p++;
    body_out[i] = '\0';
    while (i > 0 && (body_out[i - 1] == ' ' || body_out[i - 1] == '\t'))
        body_out[--i] = '\0';
    return 0;
}

int main(void) {
    char buf[] = "a:b:c";
    char *f[8];
    int nf = split_fields(buf, ':', f, 8);
    expect(nf == 3, "three fields");
    expect(strcmp(f[0], "a") == 0 && strcmp(f[2], "c") == 0, "field values");

    expect(pattern_match("foo", "xxfooyy") == 1, "pattern hit");
    expect(pattern_match("zzz", "xxfooyy") == 0, "pattern miss");
    expect(pattern_match("", "anything") == 1, "empty pattern");

    char pat[64], body[64];
    expect(parse_prog("/hi/ { print $1 }", pat, sizeof(pat), body, sizeof(body)) == 0, "parse");
    expect(strcmp(pat, "hi") == 0, "pat");
    expect(strcmp(body, "print $1") == 0, "body");

    if (fails) {
        fprintf(stderr, "%d awk-lite test(s) failed\n", fails);
        return 1;
    }
    printf("test_cli_awk: ok\n");
    return 0;
}
