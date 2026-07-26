/* /bin file utilities: ls, mkdir, touch, rm, cp, mv, ln, stat, chmod, du, df, truncate. */
#include "libpeak.h"
#include "peak_io.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"
#include "pmm.h"
#include "peakdisk.h"
#include "blockdev.h"
#include "blobstore.h"
#include "timer.h"

#define PEAK_PAGE_BYTES 4096ull

/* Human-readable binary size (B / KiB / MiB). */
static void peak_hsize_fmt(uint64_t n, char *buf, size_t cap) {
    if (!buf || !cap)
        return;
    if (n < 1024)
        snprintf(buf, cap, "%luB", (unsigned long)n);
    else if (n < 1024ull * 1024)
        snprintf(buf, cap, "%luKiB", (unsigned long)(n / 1024));
    else
        snprintf(buf, cap, "%luMiB", (unsigned long)(n / (1024ull * 1024)));
}

static char ls_type_char(enum vfs_type t) { return t==VFS_DIR?'d':t==VFS_SYMLINK?'l':'f'; }

int uls_main(int argc, char **argv) {
    int longf = peak_has_flag(argc, argv, "-l");
    int human = peak_has_flag(argc, argv, "-h");
    const char *path = ".";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            path = argv[i];
            break;
        }
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)))
        return 1;
    if (!vfs_is_dir(abs)) {
        struct vfs_stat st;
        if (vfs_stat(abs, &st) != 0) {
            peak_perror("ls", "not found");
            return 1;
        }
        if (longf) {
            char modebuf[12];
            vfs_mode_string(st.type, st.mode, modebuf, sizeof(modebuf));
            if (human) {
                char sz[16];
                peak_hsize_fmt((uint64_t)st.size, sz, sizeof(sz));
                console_printf("%c %7s %s\n", ls_type_char(st.type), sz, abs);
            } else
                console_printf("%c %6lu %s\n", ls_type_char(st.type), (uint64_t)st.size, abs);
        } else {
            console_write(abs);
            console_write("\n");
        }
        return 0;
    }
    struct vfs_dirent ents[64];
    int n = vfs_readdir(abs, ents, 64);
    if (n < 0)
        return 1;
    for (int i = 0; i < n; i++) {
        if (longf) {
            char child[VFS_PATH_MAX];
            if (!strcmp(abs, "/"))
                snprintf(child, sizeof(child), "/%s", ents[i].name);
            else
                snprintf(child, sizeof(child), "%s/%s", abs, ents[i].name);
            struct vfs_stat st;
            vfs_stat(child, &st);
            char modebuf[12];
            vfs_mode_string(st.type, st.mode, modebuf, sizeof(modebuf));
            if (human) {
                char sz[16];
                peak_hsize_fmt((uint64_t)st.size, sz, sizeof(sz));
                if (ents[i].type == VFS_SYMLINK && st.link_target[0])
                    console_printf("%c %7s %s -> %s\n", ls_type_char(ents[i].type), sz, ents[i].name, st.link_target);
                else
                    console_printf("%c %7s %s\n", ls_type_char(ents[i].type), sz, ents[i].name);
            } else if (ents[i].type == VFS_SYMLINK && st.link_target[0])
                console_printf("%c %6lu %s -> %s\n", ls_type_char(ents[i].type), (uint64_t)st.size, ents[i].name, st.link_target);
            else
                console_printf("%c %6lu %s\n", ls_type_char(ents[i].type), (uint64_t)st.size, ents[i].name);
        } else {
            console_write(ents[i].name);
            if (ents[i].type == VFS_DIR)
                console_write("/");
            else if (ents[i].type == VFS_SYMLINK)
                console_write("@");
            console_write("\n");
        }
    }
    return 0;
}

int umkdir_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("mkdir", "[-p] <path>");
        return 0;
    }
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        path = argv[i];
        break;
    }
    if (!path) {
        peak_usage("mkdir", "[-p] <path>");
        return 1;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)) != 0) {
        peak_perror("mkdir", "bad path");
        return 1;
    }
    if (!vfs_mkdir(abs)) {
        peak_perror("mkdir", "failed");
        return 1;
    }
    return 0;
}

int utouch_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("touch", "<path>");
        return argc < 2 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)) != 0)
        return 1;
    if (vfs_exists(abs))
        return 0;
    if (!vfs_create_file(abs)) {
        peak_perror("touch", "failed");
        return 1;
    }
    return 0;
}

int urm_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("rm", "[-rf] <path>");
        return 0;
    }
    int rec = peak_has_flag(argc, argv, "-r") || peak_has_flag(argc, argv, "-rf") ||
              peak_has_flag(argc, argv, "-fr");
    int force = peak_has_flag(argc, argv, "-f") || peak_has_flag(argc, argv, "-rf") ||
                peak_has_flag(argc, argv, "-fr");
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        path = argv[i];
        break;
    }
    if (!path) {
        peak_usage("rm", "[-rf] <path>");
        return 1;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)) != 0)
        return 1;
    if (!vfs_exists(abs)) {
        if (force)
            return 0;
        peak_perror("rm", "no such file");
        return 1;
    }
    int rc;
    if (vfs_is_dir(abs))
        rc = rec ? vfs_remove_tree(abs) : vfs_rmdir(abs);
    else
        rc = vfs_unlink(abs);
    if (rc != 0 && !force) {
        peak_perror("rm", "failed");
        return 1;
    }
    return 0;
}

int ucp_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("cp", "[-r] [--promote-blob] <src> <dst>");
        return argc < 3 ? 1 : 0;
    }
    int rec = peak_has_flag(argc, argv, "-r");
    int promote = peak_has_flag(argc, argv, "--promote-blob");
    const char *src = NULL, *dst = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--promote-blob"))
            continue;
        if (argv[i][0] == '-')
            continue;
        if (!src)
            src = argv[i];
        else if (!dst)
            dst = argv[i];
    }
    if (!src || !dst) {
        peak_usage("cp", "[-r] [--promote-blob] <src> <dst>");
        return 1;
    }
    char as[VFS_PATH_MAX], ad[VFS_PATH_MAX];
    if (shell_resolve_path(src, as, sizeof(as)) || shell_resolve_path(dst, ad, sizeof(ad)))
        return 1;
    if (vfs_is_dir(as)) {
        if (!rec) {
            peak_perror("cp", "omitting directory (use -r)");
            return 1;
        }
        return vfs_copy_tree_ex(as, ad, promote) == 0 ? 0 : 1;
    }
    return vfs_copy_file_ex(as, ad, promote) == 0 ? 0 : 1;
}

int umv_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("mv", "<src> <dst>");
        return argc < 3 ? 1 : 0;
    }
    char as[VFS_PATH_MAX], ad[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], as, sizeof(as)) || shell_resolve_path(argv[2], ad, sizeof(ad)))
        return 1;
    return vfs_rename(as, ad) == 0 ? 0 : 1;
}

int uln_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("ln", "[-sf] [-s] <target> <linkname>");
        return 0;
    }
    int sym = peak_has_flag(argc, argv, "-s");
    int force = peak_has_flag(argc, argv, "-f");
    const char *target = NULL, *linkname = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) {
            for (const char *f = argv[i] + 1; *f; f++)
                if (*f != 's' && *f != 'f')
                    ;
            continue;
        }
        if (!target)
            target = argv[i];
        else if (!linkname)
            linkname = argv[i];
    }
    if (!target || !linkname) {
        peak_usage("ln", "[-sf] [-s] <target> <linkname>");
        return 1;
    }
    char ad[VFS_PATH_MAX];
    if (shell_resolve_path(linkname, ad, sizeof(ad)))
        return 1;
    if (force && vfs_exists(ad))
        (void)vfs_unlink(ad);
    if (sym)
        return vfs_symlink(target, ad) == 0 ? 0 : 1;
    char as[VFS_PATH_MAX];
    if (shell_resolve_path(target, as, sizeof(as)))
        return 1;
    return vfs_link(as, ad) == 0 ? 0 : 1;
}

int ureadlink_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) { peak_usage("readlink", "<path>"); return argc < 2 ? 1 : 0; }
    char abs[VFS_PATH_MAX]; if (shell_resolve_path(argv[1], abs, sizeof(abs))) return 1;
    char target[VFS_PATH_MAX]; size_t n = 0;
    if (vfs_readlink(abs, target, sizeof(target), &n) != 0) { peak_perror("readlink", "failed"); return 1; }
    console_write(target); console_write("\n"); return 0;
}

int ustat_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("stat", "[-c format] <path>");
        return 0;
    }
    const char *format = NULL;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc)
            format = argv[++i];
        else if (argv[i][0] != '-')
            path = argv[i];
    }
    if (!path) {
        peak_usage("stat", "[-c format] <path>");
        return 1;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)))
        return 1;
    struct vfs_stat st;
    if (vfs_stat(abs, &st) != 0) {
        console_printf("stat: cannot read '%s': no such file or directory\n", abs);
        return 1;
    }
    if (format) {
        for (const char *p = format; *p; p++) {
            if (*p == '%' && p[1]) {
                p++;
                if (*p == 's')
                    console_printf("%lu", (uint64_t)st.size);
                else if (*p == 'n')
                    console_write(abs);
                else {
                    console_putc('%');
                    console_putc(*p);
                }
            } else
                console_putc(*p);
        }
        console_write("\n");
        return 0;
    }
    console_printf("path: %s\n", abs);
    const char *type_str = st.type == VFS_DIR ? "directory" : "file";
    char modebuf[12];
    vfs_mode_string(st.type, st.mode, modebuf, sizeof(modebuf));
    console_printf("type: %s\n", type_str);
    console_printf("mode: %04o (%s)\n", (unsigned)st.mode, modebuf);
    console_printf("size: %lu\n", (uint64_t)st.size);
    console_printf("children: %u\n", st.nchildren);
    console_printf("refs: %u\n", st.refs);
    if (st.type == VFS_SYMLINK && st.link_target[0]) console_printf("target: %s\n", st.link_target);
    if (st.type == VFS_FILE) {
        struct vfs_node *node = vfs_lookup(abs);
        if (node) {
            if (node->blob_id) {
                console_printf("backing: blob (id %u, %lu bytes)\n",
                               node->blob_id, (uint64_t)blobstore_size(node->blob_id));
            } else console_printf("backing: heap\n");
        }
    }
    return 0;
}

static int parse_octal_mode(const char *s, uint16_t *out) {
    if (!s || !*s)
        return -1;
    uint16_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '7')
            return -1;
        v = (uint16_t)((v << 3) | (uint16_t)(*s - '0'));
    }
    if (v > 0777u)
        return -1;
    *out = v;
    return 0;
}

static int apply_symbolic(uint16_t *mode, const char *spec) {
    const char *p = spec;
    while (*p) {
        int who = 0;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            if (*p == 'u')
                who |= 4;
            else if (*p == 'g')
                who |= 2;
            else if (*p == 'o')
                who |= 1;
            else
                who |= 7;
            p++;
        }
        if (!who)
            who = 7;
        char op = *p++;
        if (op != '+' && op != '-' && op != '=')
            return -1;
        int perm = 0;
        while (*p == 'r' || *p == 'w' || *p == 'x') {
            if (*p == 'r')
                perm |= 4;
            else if (*p == 'w')
                perm |= 2;
            else
                perm |= 1;
            p++;
        }
        if (!perm && op != '=')
            return -1;
        for (int slot = 0; slot < 3; slot++) {
            int bit = 4 >> slot;
            if (!(who & bit))
                continue;
            int shift = (2 - slot) * 3;
            uint16_t mask = (uint16_t)(7u << shift);
            if (op == '+')
                *mode = (uint16_t)(*mode | ((uint16_t)(perm & 7) << shift));
            else if (op == '-')
                *mode = (uint16_t)(*mode & ~((uint16_t)(perm & 7) << shift));
            else
                *mode = (uint16_t)((*mode & ~mask) | ((uint16_t)(perm & 7) << shift));
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p)
            return -1;
    }
    return 0;
}

static int parse_mode_arg(const char *s, uint16_t *out) {
    if (!s || !*s)
        return -1;
    int all_digits = 1;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '7') {
            all_digits = 0;
            break;
        }
    }
    if (all_digits)
        return parse_octal_mode(s, out);
    uint16_t mode = *out;
    return apply_symbolic(&mode, s) == 0 ? (*out = mode, 0) : -1;
}

int uchmod_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("chmod", "<mode> <path>...  (mode: octal or ugo[+-=]rwx)");
        return 0;
    }
    if (argc < 3) {
        peak_usage("chmod", "<mode> <path>...");
        return 1;
    }
    uint16_t mode = 0;
    if (parse_mode_arg(argv[1], &mode) != 0) {
        peak_perror("chmod", "invalid mode");
        return 1;
    }
    int rc = 0;
    for (int i = 2; i < argc; i++) {
        char abs[VFS_PATH_MAX];
        if (shell_resolve_path(argv[i], abs, sizeof(abs)) != 0) {
            rc = 1;
            continue;
        }
        if (vfs_chmod(abs, mode) != 0) {
            console_printf("chmod: cannot access '%s': no such file or directory\n", abs);
            rc = 1;
        }
    }
    return rc;
}


int udu_main(int argc, char **argv) {
    int human = peak_has_flag(argc, argv, "-h");
    int summary = peak_has_flag(argc, argv, "-s");
    const char *path = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-s"))
            continue;
        if (argv[i][0] != '-') {
            path = argv[i];
            break;
        }
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)))
        return 1;
    if (!vfs_exists(abs)) {
        console_printf("du: cannot access '%s': no such file or directory\n", abs);
        return 1;
    }
    uint64_t bytes = vfs_tree_bytes(abs);
    if (human) {
        char sz[16];
        peak_hsize_fmt(bytes, sz, sizeof(sz));
        console_printf("%s", sz);
    } else
        console_printf("%lu", bytes);
    if (!summary || human)
        console_printf("\t%s\n", abs);
    else
        console_write("\n");
    return 0;
}

int udf_main(int argc, char **argv) {
    int human = peak_has_flag(argc, argv, "-h");
    int nodes = vfs_node_count();
    int pct = (nodes * 100) / VFS_MAX_NODES;
    console_printf("VFS inodes:  %d used / %d max (%d%%)\n", nodes, VFS_MAX_NODES, pct);
    uint64_t free_p = pmm_free_pages();
    uint64_t total_p = pmm_total_pages();
    if (human) {
        char free_s[16], total_s[16];
        peak_hsize_fmt(free_p * PEAK_PAGE_BYTES, free_s, sizeof(free_s));
        peak_hsize_fmt(total_p * PEAK_PAGE_BYTES, total_s, sizeof(total_s));
        console_printf("RAM:         %s free / %s total (guest-visible, approximate)\n", free_s, total_s);
    } else
        console_printf("RAM pages:   %lu free / %lu total\n", free_p, total_p);
    if (peakdisk_available()) {
        if (peakdisk_busy())
            console_write("PeakDisk:    block device present (saving)\n");
        else
            console_write("PeakDisk:    block device present\n");
    } else {
        console_write("PeakDisk:    no block device\n");
    }
    if (blobstore_available()) {
        struct blobstore_stats bs;
        blobstore_stats(&bs);
        if (human) {
            char used_s[16], total_s[16];
            peak_hsize_fmt(bs.bytes_used, used_s, sizeof(used_s));
            peak_hsize_fmt((uint64_t)bs.pages_total * PEAK_PAGE_BYTES, total_s, sizeof(total_s));
            console_printf("Blobstore:   %u objects, %s / %s, cache %u / %u (capacity honest: guest pages only)",
                           (unsigned)bs.objects, used_s, total_s,
                           (unsigned)bs.cache_pages, (unsigned)BLOBSTORE_CACHE_PAGES);
        } else
            console_printf("Blobstore:   %u objects, %u / %u pages (%lu KiB), cache %u / %u",
                           (unsigned)bs.objects, (unsigned)bs.pages_used,
                           (unsigned)bs.pages_total,
                           (unsigned long)(bs.bytes_used / 1024u),
                           (unsigned)bs.cache_pages, (unsigned)BLOBSTORE_CACHE_PAGES);
        console_write(blobstore_check() == 0 ? " ok\n" : " (integrity check failed)\n");
    }
    if (nodes >= VFS_MAX_NODES - 2)
        console_write("df: warning — VFS inode table nearly full\n");
    if (human)
        console_write("df: -h sizes are KiB/MiB approximations of guest counters, not host disk quotas\n");
    return 0;
}

int utruncate_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("truncate", "<path> <size>");
        return argc < 3 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)))
        return 1;
    int sz = peak_atoi(argv[2]);
    if (sz < 0)
        sz = 0;
    if (sz > 4096) {
        peak_perror("truncate", "size too large (max 4096)");
        return 1;
    }
    static char zbuf[4096];
    memset(zbuf, 0, sizeof(zbuf));
    return vfs_write_file(abs, zbuf, (size_t)sz) == 0 ? 0 : 1;
}

#define DD_IO_MAX   PEAK_IO_CAP
#define DD_BS_DEF   512

static int dd_parse_eq(const char *arg, const char *key, const char **val) {
    size_t k = strlen(key);
    if (strncmp(arg, key, k) != 0 || arg[k] != '=')
        return 0;
    *val = arg + k + 1;
    return 1;
}

int udd_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("dd", "if=<path> of=<path> [bs=N] [count=N]");
        console_write("  lite copy (default bs=512, max 32 KiB total)\n");
        return 0;
    }
    const char *if_path = NULL;
    const char *of_path = NULL;
    int bs = DD_BS_DEF;
    int count = -1;
    for (int i = 1; i < argc; i++) {
        const char *v = NULL;
        if (dd_parse_eq(argv[i], "if", &v))
            if_path = v;
        else if (dd_parse_eq(argv[i], "of", &v))
            of_path = v;
        else if (dd_parse_eq(argv[i], "bs", &v))
            bs = peak_atoi(v);
        else if (dd_parse_eq(argv[i], "count", &v))
            count = peak_atoi(v);
    }
    if (!if_path || !of_path) {
        peak_usage("dd", "if=<path> of=<path> [bs=N] [count=N]");
        return 1;
    }
    if (bs <= 0 || bs > DD_IO_MAX) {
        peak_perror("dd", "bs out of range (1..65536)");
        return 1;
    }
    char if_abs[VFS_PATH_MAX];
    char of_abs[VFS_PATH_MAX];
    if (shell_resolve_path(if_path, if_abs, sizeof(if_abs)) != 0) {
        peak_perror("dd", "bad if= path");
        return 1;
    }
    if (shell_resolve_path(of_path, of_abs, sizeof(of_abs)) != 0) {
        peak_perror("dd", "bad of= path");
        return 1;
    }
    size_t max_total = DD_IO_MAX;
    if (count >= 0) {
        if (count == 0) {
            console_printf("%u+0 records in\n0+0 records out\n", (unsigned)bs);
            return 0;
        }
        uint64_t want = (uint64_t)bs * (uint64_t)count;
        if (want > DD_IO_MAX)
            want = DD_IO_MAX;
        max_total = (size_t)want;
    }
    static char buf[DD_IO_MAX];
    size_t in_n = 0;
    if (vfs_read_file(if_abs, buf, sizeof(buf), &in_n) != 0) {
        peak_perror("dd", "cannot read if=");
        return 1;
    }
    if (in_n > max_total)
        in_n = max_total;
    size_t off = 0;
    size_t rec_in = 0;
    size_t rec_out = 0;
    static char outbuf[DD_IO_MAX];
    size_t out_n = 0;
    while (off < in_n && out_n < max_total) {
        size_t chunk = (size_t)bs;
        if (chunk > in_n - off)
            chunk = in_n - off;
        if (chunk > max_total - out_n)
            chunk = max_total - out_n;
        memcpy(outbuf + out_n, buf + off, chunk);
        off += chunk;
        out_n += chunk;
        rec_in++;
        rec_out++;
    }
    if (vfs_write_file(of_abs, outbuf, out_n) != 0) {
        peak_perror("dd", "cannot write of=");
        return 1;
    }
    console_printf("%u+%u records in\n%u+%u records out\n",
                   (unsigned)bs, (unsigned)rec_in, (unsigned)bs, (unsigned)rec_out);
    console_printf("%lu bytes copied (dd lite cap %u)\n",
                   (unsigned long)out_n, (unsigned)DD_IO_MAX);
    return 0;
}

int usync_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (peak_wants_help(argc, argv)) {
        peak_usage("sync", "");
        console_write("  flush block device caches when ATA/SD present\n");
        return 0;
    }
    int acted = 0;
    if (blockdev_present()) {
        if (blockdev_flush() != 0) {
            peak_perror("sync", "blockdev flush failed");
            return 1;
        }
        console_write("sync: block device flushed\n");
        acted = 1;
    }
    if (peakdisk_available()) {
        console_write("sync: peakdisk volume ready (use disksave to persist workspace)\n");
        acted = 1;
    }
    if (!acted)
        console_write("sync: no block device (nothing to flush)\n");
    return 0;
}

#define FILE_SNIFF_MAX 512

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
    if (!out || !cap) {
        return;
    }
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
    if (n >= 3 && buf[0] == 'P' && buf[1] == '6' && (buf[2] == '\n' || buf[2] == '\r' || buf[2] == ' ')) {
        snprintf(out, cap, "PPM (P6) image data");
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

int ufile_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("file", "<path>...");
        return argc < 2 ? 1 : 0;
    }
    int err = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        char abs[VFS_PATH_MAX];
        if (shell_resolve_path(argv[i], abs, sizeof(abs)) != 0) {
            peak_perror("file", "bad path");
            err = 1;
            continue;
        }
        static uint8_t sniff[FILE_SNIFF_MAX];
        size_t n = 0;
        if (vfs_read_file(abs, sniff, sizeof(sniff), &n) != 0) {
            console_printf("%s: cannot open\n", abs);
            err = 1;
            continue;
        }
        char kind[64];
        file_describe(sniff, n, kind, sizeof(kind));
        console_printf("%s: %s\n", abs, kind);
    }
    return err;
}


static int mkdir_parents_of(const char *filepath) {
    char path[VFS_PATH_MAX];
    size_t i = 0;
    for (; filepath[i] && i + 1 < sizeof(path); i++)
        path[i] = filepath[i];
    path[i] = '\0';
    for (int j = (int)strlen(path) - 1; j >= 0; j--) {
        if (path[j] == '/') {
            path[j] = '\0';
            break;
        }
    }
    if (!path[0])
        return 0;
    char acc[VFS_PATH_MAX];
    acc[0] = '\0';
    const char *p = path;
    if (*p == '/') {
        acc[0] = '/';
        acc[1] = '\0';
        p++;
    }
    while (*p) {
        const char *slash = p;
        while (*slash && *slash != '/')
            slash++;
        size_t n = (size_t)(slash - p);
        if (n) {
            size_t al2 = strlen(acc);
            if (al2 + 1 + n + 1 >= sizeof(acc))
                return -1;
            if (al2 > 0 && acc[al2 - 1] != '/')
                acc[al2++] = '/';
            memcpy(acc + al2, p, n);
            acc[al2 + n] = '\0';
            vfs_mkdir(acc);
        }
        if (*slash == '/')
            p = slash + 1;
        else
            break;
    }
    return 0;
}

int umktemp_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("mktemp", "[-p dir] [template]");
        return 0;
    }
    const char *dir = "/tmp";
    const char *tpl = "peak.XXXXXX";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            dir = argv[++i];
            continue;
        }
        if (argv[i][0] != '-')
            tpl = argv[i];
    }
    char path[VFS_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, tpl);
    char *xs = strstr(path, "XXXXXX");
    if (!xs) {
        peak_perror("mktemp", "template must contain XXXXXX");
        return 1;
    }
    vfs_mkdir(dir);
    uint64_t t = timer_ticks();
    for (int n = 0; n < 6; n++) {
        xs[n] = 'A' + (char)((t >> (n * 4)) & 0xF);
    }
    if (!vfs_create_file(path)) {
        peak_perror("mktemp", "cannot create");
        return 1;
    }
    console_write(path);
    console_write("\n");
    return 0;
}

int uinstall_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("install", "[-D] [-m mode] <src> <dest>");
        return argc < 3 ? 1 : 0;
    }
    int mkparents = peak_has_flag(argc, argv, "-D");
    uint16_t mode = 0;
    int have_mode = 0;
    const char *src = NULL, *dest = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-D"))
            continue;
        if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            if (parse_mode_arg(argv[++i], &mode) != 0) {
                peak_perror("install", "invalid mode");
                return 1;
            }
            have_mode = 1;
            continue;
        }
        if (!src)
            src = argv[i];
        else
            dest = argv[i];
    }
    if (!src || !dest) {
        peak_usage("install", "[-D] [-m mode] <src> <dest>");
        return 1;
    }
    char src_abs[VFS_PATH_MAX], dst_abs[VFS_PATH_MAX];
    if (shell_resolve_path(src, src_abs, sizeof(src_abs)) ||
        shell_resolve_path(dest, dst_abs, sizeof(dst_abs))) {
        peak_perror("install", "bad path");
        return 1;
    }
    if (mkparents)
        mkdir_parents_of(dst_abs);
    static char buf[65536];
    size_t n = 0;
    if (vfs_read_file(src_abs, buf, sizeof(buf), &n) != 0) {
        peak_perror("install", "cannot read src");
        return 1;
    }
    if (vfs_write_file(dst_abs, buf, n) != 0) {
        peak_perror("install", "cannot write dest");
        return 1;
    }
    if (have_mode && vfs_chmod(dst_abs, mode) != 0) {
        peak_perror("install", "chmod failed");
        return 1;
    }
    return 0;
}

