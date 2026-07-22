#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"
#include "pmm.h"

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
        peak_perror("stat", "not found");
        return 1;
    }
    console_printf("path: %s\n", abs);
    console_printf("type: %s\n", st.type == VFS_DIR ? "directory" : "file");
    console_printf("size: %lu\n", (uint64_t)st.size);
    console_printf("children: %u\n", st.nchildren);
    console_printf("refs: %u\n", st.refs);
    return 0;
}

int udu_main(int argc, char **argv) {
    const char *path = ".";
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-') {
            path = argv[i];
            break;
        }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)))
        return 1;
    console_printf("%lu\t%s\n", vfs_tree_bytes(abs), abs);
    return 0;
}

int udf_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_printf("vfs nodes: %d / %d\n", vfs_node_count(), VFS_MAX_NODES);
    console_printf("mem pages free: %lu / %lu\n", pmm_free_pages(), pmm_total_pages());
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
