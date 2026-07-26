/* Host tests for Pass 47 PEAKZIP1 store/RLE pack/unpack. */
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

#define ZIP_MAGIC "PEAKZIP1"
#define ZIP_HDR 10
#define ZIP_STORE 0
#define ZIP_RLE   1
#define ZIP_FILE_MAX 8192

static int rle_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t cap, size_t *out_len) {
    size_t o = 0;
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

static int rle_decode(const uint8_t *in, size_t in_len, size_t expect, uint8_t *out, size_t cap, size_t *out_len) {
    if (expect > cap)
        return -1;
    size_t o = 0;
    size_t i = 0;
    while (i + 1 < in_len && o < expect) {
        uint8_t b = in[i++];
        uint8_t run = in[i++];
        for (uint8_t r = 0; r < run && o < expect; r++)
            out[o++] = b;
    }
    if (o != expect)
        return -1;
    *out_len = o;
    return 0;
}

static int zip_pack_entry(const uint8_t *data, size_t len, uint8_t *out, size_t cap,
                          size_t *out_len, uint8_t *method_out) {
    uint8_t rle[ZIP_FILE_MAX * 2];
    size_t rlen = 0;
    if (rle_encode(data, len, rle, sizeof(rle), &rlen) != 0)
        return -1;
    if (rlen >= len) {
        if (len > cap)
            return -1;
        memcpy(out, data, len);
        *out_len = len;
        *method_out = ZIP_STORE;
        return 0;
    }
    if (rlen > cap)
        return -1;
    memcpy(out, rle, rlen);
    *out_len = rlen;
    *method_out = ZIP_RLE;
    return 0;
}

static int zip_unpack_entry(uint8_t method, const uint8_t *data, size_t data_len,
                            size_t orig_size, uint8_t *out, size_t cap, size_t *out_len) {
    if (method == ZIP_STORE) {
        if (data_len != orig_size || orig_size > cap)
            return -1;
        memcpy(out, data, orig_size);
        *out_len = orig_size;
        return 0;
    }
    if (method == ZIP_RLE)
        return rle_decode(data, data_len, orig_size, out, cap, out_len);
    return -1;
}

static int build_archive(const char *name, const uint8_t *data, size_t len,
                         uint8_t *archive, size_t cap, size_t *out_len) {
    if (cap < ZIP_HDR + 64)
        return -1;
    memcpy(archive, ZIP_MAGIC, 8);
    archive[8] = 1;
    archive[9] = 0;
    size_t nlen = strlen(name);
    uint8_t packed[ZIP_FILE_MAX * 2];
    size_t plen = 0;
    uint8_t method = 0;
    if (zip_pack_entry(data, len, packed, sizeof(packed), &plen, &method) != 0)
        return -1;
    size_t off = ZIP_HDR;
    if (off + 1 + nlen + 1 + 8 + plen > cap)
        return -1;
    archive[off++] = (uint8_t)nlen;
    memcpy(archive + off, name, nlen);
    off += nlen;
    archive[off++] = method;
    archive[off++] = (uint8_t)(len & 0xFF);
    archive[off++] = (uint8_t)((len >> 8) & 0xFF);
    archive[off++] = (uint8_t)((len >> 16) & 0xFF);
    archive[off++] = (uint8_t)((len >> 24) & 0xFF);
    archive[off++] = (uint8_t)(plen & 0xFF);
    archive[off++] = (uint8_t)((plen >> 8) & 0xFF);
    archive[off++] = (uint8_t)((plen >> 16) & 0xFF);
    archive[off++] = (uint8_t)((plen >> 24) & 0xFF);
    memcpy(archive + off, packed, plen);
    off += plen;
    *out_len = off;
    return 0;
}

static int parse_archive(const uint8_t *archive, size_t alen, uint8_t *out, size_t cap, size_t *out_len) {
    if (alen < ZIP_HDR || memcmp(archive, ZIP_MAGIC, 8) != 0)
        return -1;
    if (archive[8] != 1 || archive[9] != 0)
        return -1;
    size_t off = ZIP_HDR;
    uint8_t nlen = archive[off++];
    if (off + nlen + 9 > alen)
        return -1;
    off += nlen;
    uint8_t method = archive[off++];
    uint32_t orig = (uint32_t)archive[off] | ((uint32_t)archive[off + 1] << 8) |
                    ((uint32_t)archive[off + 2] << 16) | ((uint32_t)archive[off + 3] << 24);
    off += 4;
    uint32_t comp = (uint32_t)archive[off] | ((uint32_t)archive[off + 1] << 8) |
                    ((uint32_t)archive[off + 2] << 16) | ((uint32_t)archive[off + 3] << 24);
    off += 4;
    if (off + comp > alen)
        return -1;
    return zip_unpack_entry(method, archive + off, (size_t)comp, (size_t)orig, out, cap, out_len);
}

int main(void) {
    uint8_t rle_in[] = "aaabbbbcc";
    uint8_t store_in[] = "xyzzy mixed bytes 123";
    uint8_t enc[512], dec[512];
    size_t el = 0, dl = 0;
    uint8_t method = 0;

    expect(zip_pack_entry(rle_in, sizeof(rle_in) - 1, enc, sizeof(enc), &el, &method) == 0, "rle pack");
    expect(method == ZIP_RLE, "rle method chosen");
    expect(zip_unpack_entry(method, enc, el, sizeof(rle_in) - 1, dec, sizeof(dec), &dl) == 0, "rle unpack");
    expect(dl == sizeof(rle_in) - 1 && memcmp(dec, rle_in, dl) == 0, "rle roundtrip");

    expect(zip_pack_entry(store_in, sizeof(store_in) - 1, enc, sizeof(enc), &el, &method) == 0, "store pack");
    expect(method == ZIP_STORE, "store method chosen");
    expect(zip_unpack_entry(method, enc, el, sizeof(store_in) - 1, dec, sizeof(dec), &dl) == 0, "store unpack");
    expect(dl == sizeof(store_in) - 1 && memcmp(dec, store_in, dl) == 0, "store roundtrip");

    expect(build_archive("a.txt", rle_in, sizeof(rle_in) - 1, enc, sizeof(enc), &el) == 0, "archive build");
    expect(parse_archive(enc, el, dec, sizeof(dec), &dl) == 0, "archive parse");
    expect(dl == sizeof(rle_in) - 1 && memcmp(dec, rle_in, dl) == 0, "archive roundtrip");

    if (fails) {
        fprintf(stderr, "%d cli zip test(s) failed\n", fails);
        return 1;
    }
    printf("test_cli_zip: ok\n");
    return 0;
}
