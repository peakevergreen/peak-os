/*
 * Host tests for MD5/SHA-256 digests used by sha256sum/md5sum.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../kernel/include/crypto.h"

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void expect_hex(const uint8_t *got, const char *want_hex, size_t n, const char *msg) {
    char buf[128];
    static const char hx[] = "0123456789abcdef";
    size_t o = 0;
    for (size_t i = 0; i < n && o + 2 < sizeof(buf); i++) {
        buf[o++] = hx[got[i] >> 4];
        buf[o++] = hx[got[i] & 0xF];
    }
    buf[o] = '\0';
    expect(strcmp(buf, want_hex) == 0, msg);
}

int main(void) {
    const char *abc = "abc";
    uint8_t out[32];

    sha256((const uint8_t *)abc, 3, out);
    expect_hex(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32,
               "sha256 abc");

    md5((const uint8_t *)abc, 3, out);
    expect_hex(out, "900150983cd24fb0d6963f7d28e17f72", 16, "md5 abc");

    const char *empty = "";
    sha256((const uint8_t *)empty, 0, out);
    expect_hex(out, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32,
               "sha256 empty");

    md5((const uint8_t *)empty, 0, out);
    expect_hex(out, "d41d8cd98f00b204e9800998ecf8427e", 16, "md5 empty");

    if (fails) {
        fprintf(stderr, "%d cli crypto test(s) failed\n", fails);
        return 1;
    }
    printf("test_cli_crypto: ok\n");
    return 0;
}
