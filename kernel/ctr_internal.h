#ifndef PEAK_CTR_INTERNAL_H
#define PEAK_CTR_INTERNAL_H

#include "types.h"
#include "vfs.h"

#define CTR_MAX_CONTAINERS 8
#define CTR_TAG_MAX        64
#define CTR_NAME_MAX       48
#define CTR_PATH_MAX       VFS_PATH_MAX
#define CTR_LOG_MAX        1024
#define CTR_IMG_ROOT       "/var/lib/peak-ctr/images"
#define CTR_HTTP_BODY_MAX  24576
#define CTR_HTTP_REQ_MAX   2048
#define CTR_ROOTFS_CANDS   5

struct ctr_container {
    int used;
    int running;
    int listen_id;
    uint16_t port_num;
    char name[CTR_NAME_MAX];
    char image[CTR_TAG_MAX];
    char port[16];
    char rootfs[CTR_PATH_MAX];
    char log[CTR_LOG_MAX];
};

extern struct ctr_container containers[CTR_MAX_CONTAINERS];
extern int ctr_ready;
extern char last_image[CTR_TAG_MAX];
extern char last_port[16];

/* Shared helpers (ctr.c / ctr_path.c). */
int ctr_str_has(const char *hay, const char *needle);
void ctr_log_append(char *log, size_t cap, const char *line);
uint16_t ctr_parse_port(const char *port);

struct ctr_container *ctr_find_by_name(const char *name);
struct ctr_container *ctr_find_by_port(const char *port);
struct ctr_container *ctr_alloc_slot(void);

/* Path / sandbox helpers (ctr_path.c) — host-testable without TCP. */
void ctr_sanitize_tag(const char *tag, char *out, size_t out_cap);
void ctr_image_rootfs_path(const char *tag, char *out, size_t out_cap);
void ctr_image_meta_path(const char *tag, char *out, size_t out_cap);
void ctr_join_path(const char *base, const char *rel, char *out, size_t out_cap);
int ctr_path_under_rootfs(const char *rootfs, const char *path);
int ctr_resolve_rootfs_candidates(const char *rootfs, const char *path,
                                  char out[][CTR_PATH_MAX], int max_out);

#endif
