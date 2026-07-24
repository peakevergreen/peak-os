/* /bin tar (ustar create/extract, size-capped) */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define TAR_BLOCK 512
#define TAR_MAX_BYTES (64 * 1024)
#define TAR_MAX_FILES 32

static void put_octal(char *dst, size_t field, unsigned long val) {
    /* ustar: octal digits, NUL or space padded; leave last as NUL if field>1 */
    char tmp[32];
    size_t i = 0;
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        char rev[32];
        size_t r = 0;
        while (val && r < sizeof(rev)) {
            rev[r++] = (char)('0' + (val & 7));
            val >>= 3;
        }
        while (r)
            tmp[i++] = rev[--r];
    }
    memset(dst, '0', field);
    if (i >= field)
        i = field - 1;
    memcpy(dst + (field - 1 - i), tmp, i);
    dst[field - 1] = '\0';
}

static unsigned long parse_octal(const char *s, size_t n) {
    unsigned long v = 0;
    for (size_t i = 0; i < n && s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '7')
            v = (v << 3) + (unsigned)(s[i] - '0');
        else if (s[i] == ' ' || s[i] == '\0')
            break;
    }
    return v;
}

static unsigned checksum(const char *hdr) {
    unsigned sum = 0;
    for (int i = 0; i < TAR_BLOCK; i++) {
        unsigned char c = (unsigned char)hdr[i];
        if (i >= 148 && i < 156)
            c = ' ';
        sum += c;
    }
    return sum;
}

static int write_file_hdr(char *archive, size_t *off, size_t cap,
                          const char *name, size_t size, char type) {
    if (*off + TAR_BLOCK > cap)
        return -1;
    char *h = archive + *off;
    memset(h, 0, TAR_BLOCK);
    size_t nl = strlen(name);
    if (nl > 99)
        nl = 99;
    memcpy(h, name, nl);
    put_octal(h + 100, 8, 0644);
    put_octal(h + 108, 8, 0);
    put_octal(h + 116, 8, 0);
    put_octal(h + 124, 12, (unsigned long)size);
    put_octal(h + 136, 12, 0);
    h[156] = type;
    memcpy(h + 257, "ustar", 5);
    h[262] = '0';
    h[263] = '0';
    put_octal(h + 148, 8, checksum(h));
    *off += TAR_BLOCK;
    return 0;
}

static int append_payload(char *archive, size_t *off, size_t cap,
                          const void *data, size_t size) {
    size_t padded = (size + TAR_BLOCK - 1) & ~(size_t)(TAR_BLOCK - 1);
    if (*off + padded > cap)
        return -1;
    if (size)
        memcpy(archive + *off, data, size);
    if (padded > size)
        memset(archive + *off + size, 0, padded - size);
    *off += padded;
    return 0;
}

int utar_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("tar", "-c <archive.tar> <file...> | -x <archive.tar> [dir]");
        return argc < 3 ? 1 : 0;
    }
    int mode = 0; /* 1=c 2=x */
    const char *archive_path = 0;
    const char *files[TAR_MAX_FILES];
    int nfiles = 0;
    const char *outdir = ".";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c")) {
            mode = 1;
            continue;
        }
        if (!strcmp(argv[i], "-x")) {
            mode = 2;
            continue;
        }
        if (!archive_path) {
            archive_path = argv[i];
            continue;
        }
        if (mode == 1) {
            if (nfiles >= TAR_MAX_FILES) {
                peak_perror("tar", "too many files");
                return 1;
            }
            files[nfiles++] = argv[i];
        } else {
            outdir = argv[i];
        }
    }
    if (!mode || !archive_path) {
        peak_usage("tar", "-c <archive.tar> <file...> | -x <archive.tar> [dir]");
        return 1;
    }

    if (mode == 1) {
        if (nfiles < 1) {
            peak_perror("tar", "no files");
            return 1;
        }
        char archive[TAR_MAX_BYTES];
        size_t off = 0;
        for (int i = 0; i < nfiles; i++) {
            char abs[VFS_PATH_MAX];
            if (shell_resolve_path(files[i], abs, sizeof(abs))) {
                peak_perror("tar", "missing file");
                return 1;
            }
            char data[8192];
            size_t len = 0;
            if (vfs_read_file(abs, data, sizeof(data), &len) != 0) {
                peak_perror("tar", "read failed");
                return 1;
            }
            /* Store basename only in archive for simplicity. */
            const char *base = files[i];
            for (const char *p = files[i]; *p; p++)
                if (*p == '/')
                    base = p + 1;
            if (write_file_hdr(archive, &off, sizeof(archive), base, len, '0') != 0 ||
                append_payload(archive, &off, sizeof(archive), data, len) != 0) {
                peak_perror("tar", "archive too large");
                return 1;
            }
        }
        /* Two zero blocks */
        if (off + 2 * TAR_BLOCK > sizeof(archive)) {
            peak_perror("tar", "archive too large");
            return 1;
        }
        memset(archive + off, 0, 2 * TAR_BLOCK);
        off += 2 * TAR_BLOCK;
        char abs[VFS_PATH_MAX];
        if (shell_resolve_path(archive_path, abs, sizeof(abs)))
            return 1;
        if (vfs_write_file(abs, archive, off) != 0) {
            peak_perror("tar", "write failed");
            return 1;
        }
        console_printf("tar: wrote %lu bytes (%d files)\n", (unsigned long)off, nfiles);
        return 0;
    }

    /* extract */
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(archive_path, abs, sizeof(abs)))
        return 1;
    char archive[TAR_MAX_BYTES];
    size_t alen = 0;
    if (vfs_read_file(abs, archive, sizeof(archive), &alen) != 0) {
        peak_perror("tar", "cannot read archive");
        return 1;
    }
    char outabs[VFS_PATH_MAX];
    if (shell_resolve_path(outdir, outabs, sizeof(outabs)))
        return 1;
    (void)vfs_mkdir(outabs);
    size_t off = 0;
    int extracted = 0;
    while (off + TAR_BLOCK <= alen) {
        char *h = archive + off;
        int empty = 1;
        for (int i = 0; i < TAR_BLOCK; i++)
            if (h[i]) {
                empty = 0;
                break;
            }
        if (empty)
            break;
        off += TAR_BLOCK;
        char name[100];
        memcpy(name, h, 99);
        name[99] = '\0';
        size_t size = (size_t)parse_octal(h + 124, 12);
        char type = h[156];
        size_t padded = (size + TAR_BLOCK - 1) & ~(size_t)(TAR_BLOCK - 1);
        if (off + padded > alen)
            break;
        if (type == '0' || type == '\0') {
            char path[VFS_PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", outabs, name);
            if (vfs_write_file(path, archive + off, size) != 0) {
                peak_perror("tar", "extract write failed");
                return 1;
            }
            console_printf("x %s\n", name);
            extracted++;
        }
        off += padded;
    }
    console_printf("tar: extracted %d files\n", extracted);
    return 0;
}
