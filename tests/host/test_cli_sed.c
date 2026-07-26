/* Host tests for Pass 40 sed-lite address / global / transliterate. */
#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int parse_addr(const char *script, int *lo, int *hi, const char **cmd) {
    *lo = 0;
    *hi = 0;
    if (!script[0] || script[0] < '0' || script[0] > '9') {
        *cmd = script;
        return 0;
    }
    int a = 0;
    const char *p = script;
    while (*p >= '0' && *p <= '9')
        a = a * 10 + (*p++ - '0');
    if (*p == ',') {
        p++;
        if (*p < '0' || *p > '9')
            return -1;
        int b = 0;
        while (*p >= '0' && *p <= '9')
            b = b * 10 + (*p++ - '0');
        if (a < 1 || b < a)
            return -1;
        *lo = a;
        *hi = b;
    } else if (a > 0) {
        *lo = a;
        *hi = a;
    } else {
        *cmd = script;
        return 0;
    }
    *cmd = p;
    return 0;
}

static int line_in_range(int line_no, int lo, int hi) {
    if (lo == 0)
        return 1;
    return line_no >= lo && line_no <= hi;
}

static void subst_line(char *out, size_t cap, const char *src, const char *old,
                       size_t old_len, const char *newv, size_t new_len, int global) {
    size_t o = 0;
    const char *p = src;
    while (*p) {
        const char *hit = 0;
        if (old_len == 0)
            hit = p;
        else {
            for (const char *q = p; *q; q++) {
                if (!memcmp(q, old, old_len)) {
                    hit = q;
                    break;
                }
            }
        }
        if (hit) {
            for (const char *q = p; q < hit && o + 1 < cap; q++)
                out[o++] = *q;
            for (size_t k = 0; k < new_len && o + 1 < cap; k++)
                out[o++] = newv[k];
            p = hit + old_len;
            if (!global)
                break;
        } else {
            if (o + 1 < cap)
                out[o++] = *p++;
            else
                p++;
        }
    }
    while (*p && o + 1 < cap)
        out[o++] = *p++;
    out[o] = '\0';
}

static void translit_line(char *out, size_t cap, const char *src, const char *from,
                          size_t from_len, const char *to) {
    unsigned char map[256];
    for (int i = 0; i < 256; i++)
        map[i] = (unsigned char)i;
    for (size_t i = 0; i < from_len; i++)
        map[(unsigned char)from[i]] = (unsigned char)to[i];
    size_t o = 0;
    for (const char *q = src; *q && o + 1 < cap; q++)
        out[o++] = (char)map[(unsigned char)*q];
    out[o] = '\0';
}

int main(void) {
    int lo, hi;
    const char *cmd;

    expect(parse_addr("2s/x/y/", &lo, &hi, &cmd) == 0, "parse single addr");
    expect(lo == 2 && hi == 2 && cmd[0] == 's', "addr 2");

    expect(parse_addr("1,3d", &lo, &hi, &cmd) == 0, "parse range");
    expect(lo == 1 && hi == 3 && cmd[0] == 'd', "range 1,3");

    expect(parse_addr("s/a/b/", &lo, &hi, &cmd) == 0, "no addr");
    expect(lo == 0 && hi == 0, "all lines default");

    expect(line_in_range(2, 1, 3) == 1, "in range");
    expect(line_in_range(4, 1, 3) == 0, "out of range");
    expect(line_in_range(99, 0, 0) == 1, "all lines");

    char out[64];
    subst_line(out, sizeof(out), "foo foo", "foo", 3, "bar", 3, 0);
    expect(strcmp(out, "bar foo") == 0, "subst once");

    subst_line(out, sizeof(out), "foo foo", "foo", 3, "bar", 3, 1);
    expect(strcmp(out, "bar bar") == 0, "subst global");

    translit_line(out, sizeof(out), "abc", "abc", 3, "xyz");
    expect(strcmp(out, "xyz") == 0, "transliterate");

    if (fails) {
        fprintf(stderr, "%d sed-lite test(s) failed\n", fails);
        return 1;
    }
    printf("test_cli_sed: ok\n");
    return 0;
}
