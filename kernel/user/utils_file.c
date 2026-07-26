/* /bin file utilities: ls, mkdir, touch, rm, cp, mv, ln, stat, du, df, truncate. */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"
#include "pmm.h"
#include "peakdisk.h"
#include "blobstore.h"

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
            if (human) {
                char sz[16];
                peak_hsize_fmt((uint64_t)st.size, sz, sizeof(sz));
                console_printf("%c %7s %s\n", 'f', sz, abs);
            } else
                console_printf("%c %6lu %s\n", 'f', (uint64_t)st.size, abs);
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
            if (human) {
                char sz[16];
                peak_hsize_fmt((uint64_t)st.size, sz, sizeof(sz));
                console_printf("%c %7s %s\n",
                               ents[i].type == VFS_DIR ? 'd' : 'f', sz, ents[i].name);
            } else
                console_printf("%c %6lu %s\n",
                               ents[i].type == VFS_DIR ? 'd' : 'f',
                               (uint64_t)st.size, ents[i].name);
        } else {
            console_write(ents[i].name);
            if (ents[i].type == VFS_DIR)
                console_write("/");
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
        peak_usage("cp", "[-r] <src> <dst>");
        return argc < 3 ? 1 : 0;
    }
    int rec = peak_has_flag(argc, argv, "-r");
    const char *src = NULL, *dst = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        if (!src)
            src = argv[i];
        else if (!dst)
            dst = argv[i];
    }
    if (!src || !dst) {
        peak_usage("cp", "[-r] <src> <dst>");
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
        return vfs_copy_tree(as, ad) == 0 ? 0 : 1;
    }
    return vfs_copy_file(as, ad) == 0 ? 0 : 1;
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
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("ln", "<target> <linkname>");
        return argc < 3 ? 1 : 0;
    }
    char as[VFS_PATH_MAX], ad[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], as, sizeof(as)) || shell_resolve_path(argv[2], ad, sizeof(ad)))
        return 1;
    return vfs_link(as, ad) == 0 ? 0 : 1;
}

int ustat_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("stat", "<path>");
        return argc < 2 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)))
        return 1;
    struct vfs_stat st;
    if (vfs_stat(abs, &st) != 0) {
        console_printf("stat: cannot read '%s': no such file or directory\n", abs);
        return 1;
    }
    console_printf("path: %s\n", abs);
    const char *type_str = st.type == VFS_DIR ? "directory" :
                           st.type == VFS_SYMLINK ? "symlink" : "file";
    console_printf("type: %s\n", type_str);
    console_printf("size: %lu\n", (uint64_t)st.size);
    console_printf("children: %u\n", st.nchildren);
    console_printf("refs: %u\n", st.refs);
    if (st.type == VFS_SYMLINK && st.link_target[0])
        console_printf("target: %s\n", st.link_target);
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

int udu_main(int argc, char **argv) {
    int human = peak_has_flag(argc, argv, "-h");
    const char *path = ".";
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-') {
            path = argv[i];
            break;
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
        console_printf("%s\t%s\n", sz, abs);
    } else
        console_printf("%lu\t%s\n", bytes, abs);
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
        console_printf("RAM:         %s free / %s total\n", free_s, total_s);
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
            console_printf("Blobstore:   %u objects, %s / %s, cache %u / %u",
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
