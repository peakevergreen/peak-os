/* /bin: sha256sum, md5sum, base64 */
#include "libpeak.h"
#include "crypto.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define HASH_MAX (64 * 1024)

static void console_write_n(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        console_putc(s[i]);
}

static int resolve_in_path(const char *path, char *abs, size_t abs_len) {
    if (!path || !strcmp(path, "-")) {
        const char *sin = shell_stdin_path();
        if (!sin)
            return -1;
        size_t i = 0;
        for (; sin[i] && i + 1 < abs_len; i++)
            abs[i] = sin[i];
        abs[i] = '\0';
        return 0;
    }
    return shell_resolve_path(path, abs, abs_len);
}

static int read_input(const char *path, uint8_t *buf, size_t cap, size_t *out,
                      const char *label) {
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs))) {
        peak_perror(label, "cannot open");
        return -1;
    }
    size_t n = 0;
    if (vfs_read_file(abs, (char *)buf, cap, &n) != 0) {
        peak_perror(label, "cannot read");
        return -1;
    }
    *out = n;
    return 0;
}

static void print_hex_digest(const uint8_t *d, size_t n) {
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        console_putc(hx[d[i] >> 4]);
        console_putc(hx[d[i] & 0xF]);
    }
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_hex_digest(const char *s, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int hi = hex_nibble(s[i * 2]);
        int lo = hex_nibble(s[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int digest_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int hash_check(const char *tool, void (*fn)(const uint8_t *, size_t, uint8_t *),
                      size_t out_len, int argc, char **argv) {
    const char *listp = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c"))
            continue;
        listp = argv[i];
    }
    char list[HASH_MAX];
    size_t ll = 0;
    if (read_input(listp, (uint8_t *)list, sizeof(list), &ll, tool) != 0)
        return 1;
    char *lines[256];
    int nl = 0;
    size_t start = 0;
    for (size_t i = 0; i <= ll && nl < 256; i++) {
        if (i == ll || list[i] == '\n') {
            lines[nl++] = list + start;
            if (i < ll)
                list[i] = '\0';
            start = i + 1;
        }
    }
    int failed = 0;
    for (int li = 0; li < nl; li++) {
        char *ln = lines[li];
        if (!ln[0] || ln[0] == '#')
            continue;
        char *sp = NULL;
        for (char *p = ln; *p; p++) {
            if (*p == ' ' || *p == '\t') {
                sp = p;
                break;
            }
        }
        if (!sp)
            continue;
        *sp = '\0';
        const char *path = sp + 1;
        while (*path == ' ' || *path == '\t')
            path++;
        if (*path == '*')
            path++;
        uint8_t expect[32];
        if (parse_hex_digest(ln, expect, out_len) != 0) {
            failed = 1;
            continue;
        }
        uint8_t data[HASH_MAX];
        size_t len = 0;
        if (read_input(path, data, sizeof(data), &len, tool) != 0) {
            console_printf("%s: FAILED open %s\n", tool, path);
            failed = 1;
            continue;
        }
        uint8_t got[32];
        fn(data, len, got);
        if (digest_equal(expect, got, out_len))
            console_printf("%s: OK\n", path);
        else {
            console_printf("%s: FAILED\n", path);
            failed = 1;
        }
    }
    return failed ? 1 : 0;
}

static int hash_file(const char *tool, void (*fn)(const uint8_t *, size_t, uint8_t *),
                     size_t out_len, int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage(tool, "[-c] [path|-]");
        return 0;
    }
    if (peak_has_flag(argc, argv, "-c"))
        return hash_check(tool, fn, out_len, argc, argv);
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-')
            path = argv[i];
    }
    uint8_t data[HASH_MAX];
    size_t len = 0;
    if (read_input(path, data, sizeof(data), &len, tool) != 0)
        return 1;
    uint8_t digest[32];
    fn(data, len, digest);
    print_hex_digest(digest, out_len);
    console_write("  ");
    console_write(!path || !strcmp(path, "-") ? "-" : path);
    console_write("\n");
    return 0;
}

int usha256sum_main(int argc, char **argv) {
    return hash_file("sha256sum", sha256, 32, argc, argv);
}

int umd5sum_main(int argc, char **argv) {
    return hash_file("md5sum", md5, 16, argc, argv);
}

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const uint8_t *in, size_t in_len, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < in_len)
            v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < in_len)
            v |= in[i + 2];
        if (o + 4 >= cap)
            break;
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i + 1 < in_len) ? B64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < in_len) ? B64[v & 63] : '=';
    }
    if (o < cap)
        out[o] = '\0';
    return o;
}

static int b64_val(int c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static size_t b64_decode(const char *in, size_t in_len, uint8_t *out, size_t cap) {
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ')
            continue;
        int v = b64_val(c);
        if (v < 0)
            continue;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap)
                break;
            out[o++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return o;
}

int ubase64_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("base64", "[-d] [path|-]");
        return 0;
    }
    int decode = peak_has_flag(argc, argv, "-d");
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d"))
            continue;
        path = argv[i];
        break;
    }
    uint8_t data[HASH_MAX];
    size_t len = 0;
    if (read_input(path, data, sizeof(data), &len, "base64") != 0)
        return 1;
    if (!decode) {
        char out[(HASH_MAX / 3 + 1) * 4 + 4];
        size_t n = b64_encode(data, len, out, sizeof(out));
        console_write_n(out, n);
        console_write("\n");
        return 0;
    }
    uint8_t out[HASH_MAX];
    size_t n = b64_decode((const char *)data, len, out, sizeof(out));
    for (size_t i = 0; i < n; i++)
        console_putc((char)out[i]);
    return 0;
}


int usha1sum_main(int argc, char **argv) {
    return hash_file("sha1sum", (void (*)(const uint8_t *, size_t, uint8_t *))sha1, 20, argc, argv);
}

static const char B32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static size_t b32_encode(const uint8_t *in, size_t in_len, char *out, size_t cap) {
    size_t o = 0;
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        buf = (buf << 8) | in[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            if (o + 1 >= cap)
                return o;
            out[o++] = B32[(buf >> bits) & 31];
        }
    }
    if (bits > 0 && o + 1 < cap)
        out[o++] = B32[(buf << (5 - bits)) & 31];
    if (o < cap)
        out[o] = '\0';
    return o;
}

static int b32_val(int c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= '2' && c <= '7')
        return c - '2' + 26;
    return -1;
}

static size_t b32_decode(const char *in, size_t in_len, uint8_t *out, size_t cap) {
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ')
            continue;
        int v = b32_val(c);
        if (v < 0)
            continue;
        acc = (acc << 5) | (uint32_t)v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap)
                break;
            out[o++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return o;
}

int ubasenc_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("basenc", "[--base32] [-d] [path|-]");
        return 0;
    }
    int decode = peak_has_flag(argc, argv, "-d") || peak_has_flag(argc, argv, "--decode");
    int b32 = peak_has_flag(argc, argv, "--base32");
    const char *path = "-";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--decode") || !strcmp(argv[i], "--base32"))
            continue;
        path = argv[i];
        break;
    }
    uint8_t data[HASH_MAX];
    size_t len = 0;
    if (read_input(path, data, sizeof(data), &len, "basenc") != 0)
        return 1;
    if (!b32) {
        peak_usage("basenc", "[--base32] [-d] [path|-]");
        return 1;
    }
    if (!decode) {
        char out[(HASH_MAX / 5 + 1) * 8 + 4];
        size_t n = b32_encode(data, len, out, sizeof(out));
        console_write_n(out, n);
        console_write("\n");
        return 0;
    }
    uint8_t out[HASH_MAX];
    size_t n = b32_decode((const char *)data, len, out, sizeof(out));
    for (size_t i = 0; i < n; i++)
        console_putc((char)out[i]);
    return 0;
}

