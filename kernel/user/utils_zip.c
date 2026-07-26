/* /bin zip/unzip — PEAKZIP1 multi-file archive (store + RLE per entry) */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define ZIP_MAGIC "PEAKZIP1"
#define ZIP_HDR 10
#define ZIP_MAX_BYTES (64 * 1024)
#define ZIP_MAX_FILES 48
#define ZIP_FILE_MAX 8192
#define ZIP_NAME_MAX 255

#define ZIP_STORE 0
#define ZIP_RLE   1

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

static int zip_pack_entry(const uint8_t *data, size_t len, uint8_t method,
                          uint8_t *out, size_t cap, size_t *out_len, uint8_t *method_out) {
    if (method == ZIP_RLE || method == 0xFF) {
        uint8_t rle[ZIP_FILE_MAX * 2];
        size_t rlen = 0;
        if (rle_encode(data, len, rle, sizeof(rle), &rlen) != 0)
            return -1;
        if (method != ZIP_RLE && rlen >= len) {
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
    if (len > cap)
        return -1;
    memcpy(out, data, len);
    *out_len = len;
    *method_out = ZIP_STORE;
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

static int zip_write_u16(uint8_t *buf, size_t off, size_t cap, uint16_t v) {
    if (off + 2 > cap)
        return -1;
    buf[off] = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    return 0;
}

static uint16_t zip_read_u16(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static int zip_write_u32(uint8_t *buf, size_t off, size_t cap, uint32_t v) {
    if (off + 4 > cap)
        return -1;
    buf[off] = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    return 0;
}

static uint32_t zip_read_u32(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static const char *basename_only(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            base = p + 1;
    return base;
}

static int zip_list_archive(const char *archive_path) {
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(archive_path, abs, sizeof(abs)))
        return 1;
    uint8_t archive[ZIP_MAX_BYTES];
    size_t alen = 0;
    if (vfs_read_file(abs, (char *)archive, sizeof(archive), &alen) != 0) {
        peak_perror("zip", "cannot read archive");
        return 1;
    }
    if (alen < ZIP_HDR || memcmp(archive, ZIP_MAGIC, 8) != 0) {
        peak_perror("zip", "bad PEAKZIP1 archive");
        return 1;
    }
    uint16_t nentries = zip_read_u16(archive + 8);
    size_t off = ZIP_HDR;
    int listed = 0;
    console_printf("Archive:  %s\n", abs);
    console_printf("  Length   Method    Name\n");
    for (uint16_t e = 0; e < nentries; e++) {
        if (off >= alen) {
            peak_perror("zip", "truncated archive");
            return 1;
        }
        uint8_t nlen = archive[off++];
        if (nlen == 0 || off + nlen + 9 > alen) {
            peak_perror("zip", "bad entry header");
            return 1;
        }
        char name[ZIP_NAME_MAX + 1];
        memcpy(name, archive + off, nlen);
        name[nlen] = '\0';
        off += nlen;
        uint8_t method = archive[off++];
        uint32_t orig_size = zip_read_u32(archive + off);
        off += 4;
        uint32_t comp_size = zip_read_u32(archive + off);
        off += 4;
        if (off + comp_size > alen) {
            peak_perror("zip", "bad entry payload");
            return 1;
        }
        off += comp_size;
        console_printf("  %8u  %-8s  %s\n", (unsigned)orig_size,
                       method == ZIP_RLE ? "RLE" : "stored", name);
        listed++;
    }
    console_printf("zip: listed %d entries (cap %d files / %d KiB archive)\n",
                   listed, ZIP_MAX_FILES, ZIP_MAX_BYTES / 1024);
    return listed ? 0 : 1;
}

int uzip_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("zip", "[-l] <archive.zip> [file...]");
        return 0;
    }
    if (peak_has_flag(argc, argv, "-l")) {
        const char *archive_path = NULL;
        for (int i = 1; i < argc; i++)
            if (strcmp(argv[i], "-l"))
                archive_path = argv[i];
        if (!archive_path) {
            peak_usage("zip", "[-l] <archive.zip>");
            return 1;
        }
        return zip_list_archive(archive_path);
    }
    if (argc < 3) {
        peak_usage("zip", "<archive.zip> <file...>");
        return 1;
    }
    const char *archive_path = argv[1];
    const char *files[ZIP_MAX_FILES];
    int nfiles = 0;
    for (int i = 2; i < argc; i++) {
        if (nfiles >= ZIP_MAX_FILES) {
            peak_perror("zip", "too many files");
            return 1;
        }
        files[nfiles++] = argv[i];
    }
    uint8_t archive[ZIP_MAX_BYTES];
    memcpy(archive, ZIP_MAGIC, 8);
    if (zip_write_u16(archive, 8, sizeof(archive), (uint16_t)nfiles) != 0) {
        peak_perror("zip", "archive too large");
        return 1;
    }
    size_t off = ZIP_HDR;
    for (int i = 0; i < nfiles; i++) {
        char abs[VFS_PATH_MAX];
        if (shell_resolve_path(files[i], abs, sizeof(abs))) {
            peak_perror("zip", "missing file");
            return 1;
        }
        uint8_t data[ZIP_FILE_MAX];
        size_t len = 0;
        if (vfs_read_file(abs, (char *)data, sizeof(data), &len) != 0) {
            peak_perror("zip", "read failed");
            return 1;
        }
        const char *name = basename_only(files[i]);
        size_t nlen = strlen(name);
        if (nlen == 0 || nlen > ZIP_NAME_MAX) {
            peak_perror("zip", "bad name");
            return 1;
        }
        uint8_t packed[ZIP_FILE_MAX * 2];
        size_t plen = 0;
        uint8_t method = 0;
        if (zip_pack_entry(data, len, 0xFF, packed, sizeof(packed), &plen, &method) != 0) {
            peak_perror("zip", "pack failed");
            return 1;
        }
        size_t need = 1 + nlen + 1 + 4 + 4 + plen;
        if (off + need > sizeof(archive)) {
            peak_perror("zip", "archive too large");
            return 1;
        }
        archive[off++] = (uint8_t)nlen;
        memcpy(archive + off, name, nlen);
        off += nlen;
        archive[off++] = method;
        if (zip_write_u32(archive, off, sizeof(archive), (uint32_t)len) != 0 ||
            zip_write_u32(archive, off + 4, sizeof(archive), (uint32_t)plen) != 0) {
            peak_perror("zip", "archive too large");
            return 1;
        }
        off += 8;
        memcpy(archive + off, packed, plen);
        off += plen;
        console_printf("  adding: %s (%s %u -> %u)\n", name,
                       method == ZIP_RLE ? "RLE" : "stored",
                       (unsigned)len, (unsigned)plen);
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(archive_path, abs, sizeof(abs)))
        return 1;
    if (vfs_write_file(abs, (char *)archive, off) != 0) {
        peak_perror("zip", "write failed");
        return 1;
    }
    console_printf("zip: wrote %lu bytes (%d files)\n", (unsigned long)off, nfiles);
    return 0;
}

int uunzip_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("unzip", "<archive.zip> [dir]");
        return argc < 2 ? 1 : 0;
    }
    const char *archive_path = argv[1];
    const char *outdir = argc >= 3 ? argv[2] : ".";
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(archive_path, abs, sizeof(abs)))
        return 1;
    uint8_t archive[ZIP_MAX_BYTES];
    size_t alen = 0;
    if (vfs_read_file(abs, (char *)archive, sizeof(archive), &alen) != 0) {
        peak_perror("unzip", "cannot read archive");
        return 1;
    }
    if (alen < ZIP_HDR || memcmp(archive, ZIP_MAGIC, 8) != 0) {
        peak_perror("unzip", "bad PEAKZIP1 archive");
        return 1;
    }
    uint16_t nentries = zip_read_u16(archive + 8);
    char outabs[VFS_PATH_MAX];
    if (shell_resolve_path(outdir, outabs, sizeof(outabs)))
        return 1;
    (void)vfs_mkdir(outabs);
    size_t off = ZIP_HDR;
    int extracted = 0;
    for (uint16_t e = 0; e < nentries; e++) {
        if (off >= alen) {
            peak_perror("unzip", "truncated archive");
            return 1;
        }
        uint8_t nlen = archive[off++];
        if (nlen == 0 || off + nlen + 9 > alen) {
            peak_perror("unzip", "bad entry header");
            return 1;
        }
        char name[ZIP_NAME_MAX + 1];
        memcpy(name, archive + off, nlen);
        name[nlen] = '\0';
        off += nlen;
        uint8_t method = archive[off++];
        uint32_t orig_size = zip_read_u32(archive + off);
        off += 4;
        uint32_t comp_size = zip_read_u32(archive + off);
        off += 4;
        if (off + comp_size > alen || orig_size > ZIP_FILE_MAX) {
            peak_perror("unzip", "bad entry payload");
            return 1;
        }
        uint8_t out[ZIP_FILE_MAX];
        size_t olen = 0;
        if (zip_unpack_entry(method, archive + off, (size_t)comp_size,
                             (size_t)orig_size, out, sizeof(out), &olen) != 0) {
            peak_perror("unzip", "decode failed");
            return 1;
        }
        off += comp_size;
        char path[VFS_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", outabs, name);
        if (vfs_write_file(path, (char *)out, olen) != 0) {
            peak_perror("unzip", "extract write failed");
            return 1;
        }
        console_printf("  inflating: %s\n", name);
        extracted++;
    }
    console_printf("unzip: extracted %d files\n", extracted);
    return 0;
}
