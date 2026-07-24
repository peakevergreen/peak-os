#ifdef PEAK_HOST_TEST
#include "ctr_internal.h"
#include <stdio.h>
#include <string.h>
#else
#include "ctr_internal.h"
#include "util.h"
#endif

int ctr_str_has(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen)
        return 1;
    for (const char *p = hay; *p; p++) {
        if (!strncmp(p, needle, nlen))
            return 1;
    }
    return 0;
}

void ctr_log_append(char *log, size_t cap, const char *line) {
    if (!log || !cap)
        return;
    size_t n = strlen(log);
    size_t ln = strlen(line);
    if (n + ln + 2 >= cap)
        return;
    memcpy(log + n, line, ln);
    n += ln;
    log[n++] = '\n';
    log[n] = '\0';
}

void ctr_sanitize_tag(const char *tag, char *out, size_t out_cap) {
    size_t o = 0;
    for (size_t i = 0; tag[i] && o + 1 < out_cap; i++) {
        char c = tag[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            out[o++] = c;
        else
            out[o++] = '_';
    }
    if (o == 0 && out_cap > 1) {
        out[0] = 'i';
        o = 1;
    }
    out[o] = '\0';
}

void ctr_image_rootfs_path(const char *tag, char *out, size_t out_cap) {
    char safe[CTR_TAG_MAX];
    ctr_sanitize_tag(tag, safe, sizeof(safe));
    snprintf(out, out_cap, "%s/%s/rootfs", CTR_IMG_ROOT, safe);
}

void ctr_image_meta_path(const char *tag, char *out, size_t out_cap) {
    char safe[CTR_TAG_MAX];
    ctr_sanitize_tag(tag, safe, sizeof(safe));
    snprintf(out, out_cap, "%s/%s/tag", CTR_IMG_ROOT, safe);
}

void ctr_join_path(const char *base, const char *rel, char *out, size_t out_cap) {
    if (!out || !out_cap)
        return;
    out[0] = '\0';
    if (!base || !rel)
        return;
    if (rel[0] == '/') {
        snprintf(out, out_cap, "%s", rel);
        return;
    }
    size_t bl = strlen(base);
    if (bl > 0 && base[bl - 1] == '/')
        snprintf(out, out_cap, "%s%s", base, rel);
    else
        snprintf(out, out_cap, "%s/%s", base, rel);
}

/* Reject ".." components and require path to be rootfs or a child thereof. */
int ctr_path_under_rootfs(const char *rootfs, const char *path) {
    if (!rootfs || !path || rootfs[0] != '/' || path[0] != '/')
        return 0;
    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '.' && path[i + 1] == '.' &&
            (i == 0 || path[i - 1] == '/') &&
            (path[i + 2] == '/' || path[i + 2] == '\0'))
            return 0;
    }
    size_t rl = strlen(rootfs);
    while (rl > 1 && rootfs[rl - 1] == '/')
        rl--;
    if (strncmp(path, rootfs, rl) != 0)
        return 0;
    return path[rl] == '\0' || path[rl] == '/';
}

/*
 * Build rootfs-relative HTTP lookup candidates (same order as resolve_rootfs_file).
 * Returns candidate count, or -1 if the URL path / any candidate escapes the rootfs.
 */
int ctr_resolve_rootfs_candidates(const char *rootfs, const char *path,
                                  char out[][CTR_PATH_MAX], int max_out) {
    if (!rootfs || !path || !out || max_out <= 0)
        return -1;
    if (path[0] && path[0] != '/')
        return -1;
    /* URL paths must not carry ".." — callers normalize first; enforce here too. */
    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '.' && path[i + 1] == '.' &&
            (i == 0 || path[i - 1] == '/') &&
            (path[i + 2] == '/' || path[i + 2] == '\0'))
            return -1;
    }

    int nc = 0;
    if (!strcmp(path, "/") || path[0] == '\0') {
        if (nc >= max_out)
            return -1;
        snprintf(out[nc++], CTR_PATH_MAX, "%s/usr/share/nginx/html/index.html",
                 rootfs);
        if (nc >= max_out)
            return -1;
        snprintf(out[nc++], CTR_PATH_MAX, "%s/index.html", rootfs);
        if (nc >= max_out)
            return -1;
        snprintf(out[nc++], CTR_PATH_MAX, "%s/html/index.html", rootfs);
    } else {
        if (nc >= max_out)
            return -1;
        snprintf(out[nc++], CTR_PATH_MAX, "%s%s", rootfs, path);
        if (nc >= max_out)
            return -1;
        snprintf(out[nc++], CTR_PATH_MAX, "%s/usr/share/nginx/html%s", rootfs,
                 path);
        size_t pl = strlen(path);
        if (nc >= max_out)
            return -1;
        if (pl > 0 && path[pl - 1] == '/')
            snprintf(out[nc++], CTR_PATH_MAX, "%s%sindex.html", rootfs, path);
        else
            snprintf(out[nc++], CTR_PATH_MAX, "%s%s/index.html", rootfs, path);
    }

    for (int i = 0; i < nc; i++) {
        if (!ctr_path_under_rootfs(rootfs, out[i]))
            return -1;
    }
    return nc;
}
