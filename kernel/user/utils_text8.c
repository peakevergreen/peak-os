/* /bin shuf, cksum (CRC32), xxd */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"
#include "timer.h"

#define READ_MAX 8192
#define MAX_LINES 256

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

static int read_in(const char *path, char *buf, size_t cap, size_t *out) {
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs)))
        return -1;
    size_t n = 0;
    if (vfs_read_file(abs, buf, cap - 1, &n) != 0)
        return -1;
    buf[n] = '\0';
    *out = n;
    return 0;
}

static int split_lines(char *data, size_t len, char **lines, int max) {
    int n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || data[i] == '\n') {
            if (n >= max)
                break;
            lines[n++] = data + start;
            if (i < len)
                data[i] = '\0';
            start = i + 1;
        }
    }
    return n;
}

static uint32_t shuf_rand(uint32_t *state) {
    *state = *state * 1103515245u + 12345u;
    return *state;
}

int ushuf_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("shuf", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char buf[READ_MAX];
    size_t len = 0;
    if (read_in(path, buf, sizeof(buf), &len) != 0) {
        peak_perror("shuf", "cannot read");
        return 1;
    }
    char *lines[MAX_LINES];
    int n = split_lines(buf, len, lines, MAX_LINES);
    if (n <= 1) {
        for (int i = 0; i < n; i++) {
            console_write(lines[i]);
            console_write("\n");
        }
        return 0;
    }
    uint32_t rng = (uint32_t)(timer_ticks() ^ (timer_ticks() >> 32) ^ 0x5A17u);
    for (int i = n - 1; i > 0; i--) {
        uint32_t r = shuf_rand(&rng);
        int j = (int)(r % (uint32_t)(i + 1));
        char *tmp = lines[i];
        lines[i] = lines[j];
        lines[j] = tmp;
    }
    for (int i = 0; i < n; i++) {
        console_write(lines[i]);
        console_write("\n");
    }
    return 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

int ucksum_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("cksum", "[path|-]");
        return 0;
    }
    const char *path = argc >= 2 ? argv[1] : "-";
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs))) {
        peak_perror("cksum", "cannot open");
        return 1;
    }
    uint8_t data[READ_MAX];
    size_t len = 0;
    if (vfs_read_file(abs, (char *)data, sizeof(data), &len) != 0) {
        peak_perror("cksum", "cannot read");
        return 1;
    }
    uint32_t crc = crc32_update(0, data, len);
    console_printf("%u %u", (unsigned)crc, (unsigned)len);
    if (path && strcmp(path, "-"))
        console_printf(" %s", path);
    else
        console_write(" -");
    console_write("\n");
    return 0;
}

static int is_print(unsigned char c) {
    return c >= 32 && c < 127;
}

int uxxd_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("xxd", "<path>");
        return argc < 2 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)))
        return 1;
    uint8_t data[READ_MAX];
    size_t len = 0;
    if (vfs_read_file(abs, (char *)data, sizeof(data), &len) != 0) {
        peak_perror("xxd", "cannot read");
        return 1;
    }
    for (size_t off = 0; off < len; off += 16) {
        console_printf("%08lx: ", (unsigned long)off);
        for (size_t i = 0; i < 16; i++) {
            if (i == 8)
                console_write(" ");
            if (off + i < len)
                console_printf("%02x", (unsigned)data[off + i]);
            else
                console_write("  ");
            if (i + 1 < 16)
                console_putc(' ');
        }
        console_write("  ");
        for (size_t i = 0; i < 16 && off + i < len; i++) {
            unsigned char c = data[off + i];
            console_putc(is_print(c) ? (char)c : '.');
        }
        console_write("\n");
    }
    return 0;
}
