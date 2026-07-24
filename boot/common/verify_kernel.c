#include "boot_verify.h"
#include "boot_sha256.h"

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_hex32(const char *hex, uint8_t out[BOOT_SHA256_DIGEST_LEN]) {
    if (!hex)
        return -1;
    for (int i = 0; i < BOOT_SHA256_DIGEST_LEN; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (hex[BOOT_SHA256_DIGEST_LEN * 2] &&
        hex[BOOT_SHA256_DIGEST_LEN * 2] != ' ' &&
        hex[BOOT_SHA256_DIGEST_LEN * 2] != '\t' &&
        hex[BOOT_SHA256_DIGEST_LEN * 2] != '\r' &&
        hex[BOOT_SHA256_DIGEST_LEN * 2] != '\n')
        return -1;
    return 0;
}

static int path_suffix_eq(const char *line_path, const char *want) {
    size_t lw = boot_strlen(want);
    size_t lp = boot_strlen(line_path);
    if (lp < lw)
        return 0;
    return boot_strncasecmp(line_path + lp - lw, want, lw) == 0;
}

static int manifest_lookup(const char *manifest, size_t len, uint8_t out[BOOT_SHA256_DIGEST_LEN]) {
    if (!manifest || !len)
        return -1;
    static const char *paths[] = {
        "x86_64/kernel.elf",
        "kernel.elf",
        "KERNEL.ELF",
        "boot/kernel.elf",
        "BOOT/KERNEL.ELF",
        "EFI/PEAK/KERNEL.ELF",
        0,
    };
    size_t i = 0;
    while (i < len) {
        while (i < len && (manifest[i] == '\n' || manifest[i] == '\r'))
            i++;
        if (i >= len)
            break;
        size_t line = i;
        while (i < len && manifest[i] != '\n' && manifest[i] != '\r')
            i++;
        if (i - line < BOOT_SHA256_DIGEST_LEN * 2 + 3)
            continue;
        char hex[BOOT_SHA256_DIGEST_LEN * 2 + 1];
        boot_memcpy(hex, manifest + line, BOOT_SHA256_DIGEST_LEN * 2);
        hex[BOOT_SHA256_DIGEST_LEN * 2] = '\0';
        const char *path = manifest + line + BOOT_SHA256_DIGEST_LEN * 2;
        while (*path == ' ' || *path == '\t')
            path++;
        char pathbuf[64];
        size_t plen = 0;
        while (path[plen] && path[plen] != '\r' && path[plen] != '\n' &&
               plen + 1 < sizeof(pathbuf)) {
            pathbuf[plen] = path[plen];
            plen++;
        }
        pathbuf[plen] = '\0';
        for (int p = 0; paths[p]; p++) {
            if (path_suffix_eq(pathbuf, paths[p])) {
                if (parse_hex32(hex, out) == 0)
                    return 0;
            }
        }
    }
    return -1;
}

static void digest_to_hex(const uint8_t d[BOOT_SHA256_DIGEST_LEN], char *hex) {
    static const char k[] = "0123456789abcdef";
    for (int i = 0; i < BOOT_SHA256_DIGEST_LEN; i++) {
        hex[i * 2] = k[d[i] >> 4];
        hex[i * 2 + 1] = k[d[i] & 0xF];
    }
    hex[BOOT_SHA256_DIGEST_LEN * 2] = '\0';
}

int boot_verify_kernel(const struct peak_loader_conf *conf,
                       const uint8_t *img, size_t size,
                       const char *manifest, size_t manifest_len) {
    if (!conf || !img || !size)
        return conf && conf->verify_required ? -1 : 0;

    uint8_t expect[BOOT_SHA256_DIGEST_LEN];
    int have_expect = 0;
    if (conf->kernel_sha256[0]) {
        if (parse_hex32(conf->kernel_sha256, expect) != 0) {
            boot_serial_write_str("verify: bad kernel_sha256 in peak.conf\n");
            return -1;
        }
        have_expect = 1;
    } else if (manifest_lookup(manifest, manifest_len, expect) == 0) {
        have_expect = 1;
    }

    if (!have_expect) {
        if (conf->verify_required) {
            boot_serial_write_str("verify: required but no digest (manifest/conf)\n");
            return -1;
        }
        boot_serial_write_str("verify: skip (no expected digest)\n");
        return 0;
    }

    struct boot_sha256_ctx ctx;
    boot_sha256_init(&ctx);
    boot_sha256_update(&ctx, img, size);
    uint8_t got[BOOT_SHA256_DIGEST_LEN];
    boot_sha256_final(&ctx, got);

    if (boot_memcmp(got, expect, BOOT_SHA256_DIGEST_LEN) != 0) {
        boot_serial_write_str("verify: kernel digest mismatch\n");
        char hx[BOOT_SHA256_DIGEST_LEN * 2 + 1];
        digest_to_hex(got, hx);
        boot_serial_write_str("verify: got ");
        boot_serial_write_str(hx);
        boot_serial_write_str("\n");
        return -1;
    }

    boot_serial_write_str("verify: kernel digest ok\n");
    return 0;
}
