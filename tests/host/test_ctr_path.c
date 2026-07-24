/*
 * Host tests for CTR path sandbox helpers (kernel/ctr_path.c):
 * join_path, path_under_rootfs, resolve_rootfs_candidates / escape prevention.
 */
#include "ctr_internal.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void test_join_path(void) {
    char out[CTR_PATH_MAX];

    ctr_join_path("/ctx", "a.html", out, sizeof(out));
    expect(!strcmp(out, "/ctx/a.html"), "join relative");

    ctr_join_path("/ctx/", "a.html", out, sizeof(out));
    expect(!strcmp(out, "/ctx/a.html"), "join with trailing slash base");

    ctr_join_path("/ctx", "/abs/file", out, sizeof(out));
    expect(!strcmp(out, "/abs/file"), "absolute rel ignores base");

    ctr_join_path("/ctx", "../escape", out, sizeof(out));
    expect(!strcmp(out, "/ctx/../escape"), "join keeps literal .. (caller must sandbox)");
    expect(!ctr_path_under_rootfs("/ctx", out), "joined .. escapes context");
}

static void test_path_under_rootfs(void) {
    const char *root = "/var/lib/peak-ctr/images/demo/rootfs";

    expect(ctr_path_under_rootfs(root, root), "root equals rootfs");
    expect(ctr_path_under_rootfs(root, "/var/lib/peak-ctr/images/demo/rootfs/index.html"),
           "child under rootfs");
    expect(ctr_path_under_rootfs(root,
                                 "/var/lib/peak-ctr/images/demo/rootfs/usr/share/nginx/html/x"),
           "deep child under rootfs");

    expect(!ctr_path_under_rootfs(root, "/var/lib/peak-ctr/images/demo/rootfsEvil"),
           "reject prefix bypass");
    expect(!ctr_path_under_rootfs(root, "/etc/passwd"), "reject outside tree");
    expect(!ctr_path_under_rootfs(root,
                                  "/var/lib/peak-ctr/images/demo/rootfs/../rootfsEvil"),
           "reject .. component");
    expect(!ctr_path_under_rootfs(root, "relative"), "reject relative");
    expect(!ctr_path_under_rootfs(root, NULL), "reject null path");
    expect(!ctr_path_under_rootfs(NULL, root), "reject null rootfs");
}

static void test_resolve_rootfs_candidates(void) {
    const char *root = "/var/lib/peak-ctr/images/demo/rootfs";
    char cands[CTR_ROOTFS_CANDS][CTR_PATH_MAX];
    int n;

    n = ctr_resolve_rootfs_candidates(root, "/", cands, CTR_ROOTFS_CANDS);
    expect(n == 3, "root path yields 3 candidates");
    if (n == 3) {
        expect(!strcmp(cands[0],
                       "/var/lib/peak-ctr/images/demo/rootfs/usr/share/nginx/html/index.html"),
               "root cand0 nginx index");
        expect(!strcmp(cands[1], "/var/lib/peak-ctr/images/demo/rootfs/index.html"),
               "root cand1 index");
        expect(!strcmp(cands[2], "/var/lib/peak-ctr/images/demo/rootfs/html/index.html"),
               "root cand2 html index");
        for (int i = 0; i < n; i++)
            expect(ctr_path_under_rootfs(root, cands[i]), "root cand under rootfs");
    }

    n = ctr_resolve_rootfs_candidates(root, "/style.css", cands, CTR_ROOTFS_CANDS);
    expect(n == 3, "file path yields 3 candidates");
    if (n == 3) {
        expect(!strcmp(cands[0], "/var/lib/peak-ctr/images/demo/rootfs/style.css"),
               "file cand0 direct");
        expect(!strcmp(cands[1],
                       "/var/lib/peak-ctr/images/demo/rootfs/usr/share/nginx/html/style.css"),
               "file cand1 nginx");
        expect(!strcmp(cands[2], "/var/lib/peak-ctr/images/demo/rootfs/style.css/index.html"),
               "file cand2 index suffix");
    }

    n = ctr_resolve_rootfs_candidates(root, "/dir/", cands, CTR_ROOTFS_CANDS);
    expect(n == 3, "dir path yields 3 candidates");
    if (n == 3)
        expect(!strcmp(cands[2], "/var/lib/peak-ctr/images/demo/rootfs/dir/index.html"),
               "dir trailing slash index");

    expect(ctr_resolve_rootfs_candidates(root, "/../etc/passwd", cands,
                                         CTR_ROOTFS_CANDS) < 0,
           "reject .. escape in URL path");
    expect(ctr_resolve_rootfs_candidates(root, "/foo/../../etc/passwd", cands,
                                         CTR_ROOTFS_CANDS) < 0,
           "reject nested .. escape");
    expect(ctr_resolve_rootfs_candidates(root, "relative", cands, CTR_ROOTFS_CANDS) < 0,
           "reject relative URL path");
    expect(ctr_resolve_rootfs_candidates(root, "/ok", cands, 0) < 0,
           "reject max_out 0");
}

static void test_copy_sandbox_join(void) {
    const char *ctx = "/home/dev/workspace/app";
    char src[CTR_PATH_MAX];

    ctr_join_path(ctx, "index.html", src, sizeof(src));
    expect(ctr_path_under_rootfs(ctx, src), "COPY relative src under context");

    ctr_join_path(ctx, "/etc/passwd", src, sizeof(src));
    expect(!ctr_path_under_rootfs(ctx, src), "COPY absolute src escapes context");

    ctr_join_path(ctx, "../../../etc/passwd", src, sizeof(src));
    expect(!ctr_path_under_rootfs(ctx, src), "COPY .. src escapes context");

    const char *root = "/var/lib/peak-ctr/images/x/rootfs";
    char dst[CTR_PATH_MAX];
    snprintf(dst, sizeof(dst), "%s%s", root, "/usr/share/nginx/html/index.html");
    expect(ctr_path_under_rootfs(root, dst), "COPY dest under rootfs");

    snprintf(dst, sizeof(dst), "%s%s", root, "/../../etc/passwd");
    expect(!ctr_path_under_rootfs(root, dst), "COPY dest with .. escapes rootfs");
}

int main(void) {
    test_join_path();
    test_path_under_rootfs();
    test_resolve_rootfs_candidates();
    test_copy_sandbox_join();

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("ok - ctr path sandbox host tests\n");
    return 0;
}
