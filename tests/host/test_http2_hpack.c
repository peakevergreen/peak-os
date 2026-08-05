/*
 * Host gates for HPACK Huffman + integer decode (B-HTTPS-H2 / H2-1).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../kernel/include/hpack.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void test_indexed_status_200(void) {
    const uint8_t blk[] = {0x88}; /* static :status 200 */
    int status = 0;
    char hdrs[256];
    size_t off = 0;
    expect(hpack_decode_block(blk, sizeof(blk), &status, hdrs, sizeof(hdrs), &off) == 0,
           "indexed decode rc");
    expect(status == 200, "indexed :status 200");
}

static void test_huffman_status_200(void) {
    /* Literal without indexing, name index 8, Huffman "200" → 10 01 */
    const uint8_t blk[] = {0x08, 0x82, 0x10, 0x01};
    int status = 0;
    char hdrs[256];
    size_t off = 0;
    expect(hpack_decode_block(blk, sizeof(blk), &status, hdrs, sizeof(hdrs), &off) == 0,
           "huffman decode rc");
    expect(status == 200, "huffman :status 200");
}

static void test_incremental_huffman_status(void) {
    const uint8_t blk[] = {0x48, 0x82, 0x10, 0x01};
    int status = 0;
    char hdrs[256];
    size_t off = 0;
    expect(hpack_decode_block(blk, sizeof(blk), &status, hdrs, sizeof(hdrs), &off) == 0,
           "inc huffman rc");
    expect(status == 200, "inc huffman :status 200");
}

static void test_no_fake_status(void) {
    /* Indexed date (33) only — no :status. */
    const uint8_t blk[] = {0xa1};
    int status = 0;
    char hdrs[256];
    size_t off = 0;
    expect(hpack_decode_block(blk, sizeof(blk), &status, hdrs, sizeof(hdrs), &off) == 0,
           "date-only decode");
    expect(status == 0, "unseen :status stays 0");
}

static void test_huff_plain_string(void) {
    char out[32];
    const uint8_t enc[] = {0x10, 0x01};
    expect(hpack_huff_decode(enc, sizeof(enc), out, sizeof(out)) == 0, "huff 200 rc");
    expect(!strcmp(out, "200"), "huff 200 text");
}

static void test_lit_int_beyond_4bits(void) {
    uint8_t buf[64];
    size_t o = 0;
    /* user-agent is static index 58 — does not fit in 4-bit prefix. */
    expect(hpack_add_lit(buf, sizeof(buf), &o, 58, "Peak/0.2") == 0, "add_lit ua");
    expect(o > 2 && buf[0] == 0x0f, "name idx uses 4-bit overflow");
    expect(buf[1] == (uint8_t)(58 - 15), "name idx continuation");
}

int main(void) {
    test_indexed_status_200();
    test_huffman_status_200();
    test_incremental_huffman_status();
    test_no_fake_status();
    test_huff_plain_string();
    test_lit_int_beyond_4bits();
    if (fails) {
        fprintf(stderr, "test_http2_hpack: %d fail(s)\n", fails);
        return 1;
    }
    printf("test_http2_hpack: ok\n");
    return 0;
}
