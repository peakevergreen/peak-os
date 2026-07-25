/* Host tests for Pass 20 cal/dow and PEAKGZ1 encode/decode. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int dow(int y, int m, int d) {
    if (m < 3) {
        m += 12;
        y--;
    }
    int K = y % 100;
    int J = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    return (h + 6) % 7;
}

static int gzip_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t cap, size_t *out_len) {
    if (cap < 12)
        return -1;
    memcpy(out, "PEAKGZ1", 7);
    out[7] = 0;
    out[8] = (uint8_t)(in_len & 0xFF);
    out[9] = (uint8_t)((in_len >> 8) & 0xFF);
    out[10] = (uint8_t)((in_len >> 16) & 0xFF);
    out[11] = (uint8_t)((in_len >> 24) & 0xFF);
    size_t o = 12;
    size_t i = 0;
    while (i < in_len) {
        uint8_t b = in[i];
        size_t run = 1;
        while (i + run < in_len && in[i + run] == b && run < 255)
            run++;
        if (o + 2 > cap)
            return -1;
        out[o++] = b;
        out[o++] = (uint8_t)run;
        i += run;
    }
    *out_len = o;
    return 0;
}

static int gzip_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t cap, size_t *out_len) {
    if (in_len < 12 || memcmp(in, "PEAKGZ1", 7) != 0)
        return -1;
    size_t expect_n = (size_t)in[8] | ((size_t)in[9] << 8) | ((size_t)in[10] << 16) | ((size_t)in[11] << 24);
    if (expect_n > cap)
        return -1;
    size_t o = 0;
    size_t i = 12;
    while (i + 1 < in_len && o < expect_n) {
        uint8_t b = in[i++];
        uint8_t run = in[i++];
        for (uint8_t r = 0; r < run && o < expect_n; r++)
            out[o++] = b;
    }
    if (o != expect_n)
        return -1;
    *out_len = o;
    return 0;
}

int main(void) {
    /* 2026-07-24 is Friday */
    expect(dow(2026, 7, 24) == 5, "dow 2026-07-24 Friday");

    uint8_t in[] = "aaabbbbcc";
    uint8_t enc[64], dec[64];
    size_t el = 0, dl = 0;
    expect(gzip_encode(in, sizeof(in) - 1, enc, sizeof(enc), &el) == 0, "encode");
    expect(gzip_decode(enc, el, dec, sizeof(dec), &dl) == 0, "decode");
    expect(dl == sizeof(in) - 1 && memcmp(dec, in, dl) == 0, "roundtrip");

    if (fails) {
        fprintf(stderr, "%d cli sys2 test(s) failed\n", fails);
        return 1;
    }
    printf("test_cli_sys2: ok\n");
    return 0;
}
