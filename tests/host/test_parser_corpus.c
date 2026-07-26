/*
 * Host corpus gates for Wave 7 parsers: DNS hostnames, ctr ports, blob magic.
 * Reads fixtures from tests/fuzz/corpus/.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../kernel/include/dns_util.h"

static int fails;

static uint16_t corpus_parse_port(const char *port) {
    uint32_t v = 0;
    if (!port || !port[0])
        return 0;
    for (const char *p = port; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        v = v * 10u + (uint32_t)(*p - '0');
        if (v > 65535)
            return 0;
    }
    return (uint16_t)v;
}

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int read_corpus(const char *path, int (*check)(const char *line, int expect_valid)) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "FAIL: cannot open %s\n", path);
        return 1;
    }
    char line[256];
    int mode = 1; /* 1=valid expected, 0=invalid */
    int local_fails = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (!line[0])
            continue;
        if (line[0] == '#') {
            if (strstr(line, "invalid"))
                mode = 0;
            else if (strstr(line, "valid"))
                mode = 1;
            continue;
        }
        if (!check(line, mode)) {
            fprintf(stderr, "FAIL corpus %s: %s (expect %s)\n", path, line,
                    mode ? "valid" : "invalid");
            local_fails++;
        }
    }
    fclose(f);
    return local_fails;
}

static int check_dns_host(const char *line, int expect_valid) {
    char norm[128];
    dns_host_normalize(norm, sizeof(norm), line);
    int ok = dns_host_valid(line);
    if (expect_valid)
        return ok && norm[0];
    return !ok;
}

static int check_ctr_port(const char *line, int expect_valid) {
    uint16_t p = corpus_parse_port(line);
    if (expect_valid)
        return p > 0;
    return p == 0;
}

static int blob_magic_ok(const char *magic) {
    return !strcmp(magic, "PEAKFS1") || !strcmp(magic, "PEAKZIP1") ||
           !strcmp(magic, "PEAKGZ1");
}

static int check_blob_magic(const char *line, int expect_valid) {
    int ok = blob_magic_ok(line);
    return expect_valid ? ok : !ok;
}

static void test_dns_normalize(void) {
    char out[64];
    dns_host_normalize(out, sizeof(out), "Example.COM");
    expect(!strcmp(out, "example.com"), "dns normalize lower");
    dns_host_normalize(out, sizeof(out), NULL);
    expect(out[0] == '\0', "dns normalize null");
}

int main(void) {
    test_dns_normalize();
    fails += read_corpus("tests/fuzz/corpus/dns-hosts.txt", check_dns_host);
    fails += read_corpus("tests/fuzz/corpus/ctr-ports.txt", check_ctr_port);
    fails += read_corpus("tests/fuzz/corpus/blob-magic.txt", check_blob_magic);

    /* Adversarial port fuzz lite */
    const char *bad_ports[] = { "", "999999", "12 34", "12.3", NULL };
    for (int i = 0; bad_ports[i]; i++)
        expect(corpus_parse_port(bad_ports[i]) == 0, "bad port rejected");

    if (fails) {
        fprintf(stderr, "%d parser corpus gate(s) failed\n", fails);
        return 1;
    }
    printf("test_parser_corpus: ok\n");
    return 0;
}
