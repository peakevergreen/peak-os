/* Host tests for Pass 63 file(1) magic sniffing. */
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

static int file_is_text(const uint8_t *buf, size_t n) {
    if (!n)
        return 1;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = buf[i];
        if (c == 0)
            return 0;
        if (c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c < 32 || c == 127)
            return 0;
    }
    return 1;
}

static void file_describe(const uint8_t *buf, size_t n, char *out, size_t cap) {
    out[0] = '\0';
    if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
        snprintf(out, cap, "ELF executable");
        return;
    }
    if (n >= 8 && !memcmp(buf, "PEAKZIP1", 8)) {
        snprintf(out, cap, "Peak PEAKZIP1 archive");
        return;
    }
    if (n >= 7 && !memcmp(buf, "PEAKGZ1", 7)) {
        snprintf(out, cap, "Peak PEAKGZ1 compressed data");
        return;
    }
    if (n >= 2 && buf[0] == 'B' && buf[1] == 'M') {
        snprintf(out, cap, "BMP image data");
        return;
    }
    if (n >= 2 && buf[0] == 'P' && buf[1] == '6') {
        snprintf(out, cap, "PPM (P6) image data");
        return;
    }
    if (file_is_text(buf, n))
        snprintf(out, cap, "ASCII text");
    else
        snprintf(out, cap, "data");
}

int main(void) {
    char kind[64];
    const uint8_t elf[] = { 0x7F, 'E', 'L', 'F', 2, 1, 1 };
    file_describe(elf, sizeof(elf), kind, sizeof(kind));
    expect(!strcmp(kind, "ELF executable"), "ELF magic");

    const uint8_t zip[] = "PEAKZIP1\x01";
    file_describe((const uint8_t *)zip, 9, kind, sizeof(kind));
    expect(!strcmp(kind, "Peak PEAKZIP1 archive"), "PEAKZIP1");

    const uint8_t gz[] = "PEAKGZ1\x00";
    file_describe((const uint8_t *)gz, 8, kind, sizeof(kind));
    expect(!strcmp(kind, "Peak PEAKGZ1 compressed data"), "PEAKGZ1");

    const uint8_t bmp[] = "BM\x00\x00";
    file_describe(bmp, sizeof(bmp), kind, sizeof(kind));
    expect(!strcmp(kind, "BMP image data"), "BMP");

    const uint8_t ppm[] = "P6\n# comment";
    file_describe(ppm, sizeof(ppm), kind, sizeof(kind));
    expect(!strcmp(kind, "PPM (P6) image data"), "PPM");

    const uint8_t txt[] = "hello peak\n";
    file_describe(txt, sizeof(txt) - 1, kind, sizeof(kind));
    expect(!strcmp(kind, "ASCII text"), "text");

    const uint8_t bin[] = { 0x00, 0x01, 0x02 };
    file_describe(bin, sizeof(bin), kind, sizeof(kind));
    expect(!strcmp(kind, "data"), "binary");

    if (fails)
        return 1;
    printf("test_cli_filemagic: ok\n");
    return 0;
}
