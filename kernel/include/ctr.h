#ifndef PEAK_CTR_H
#define PEAK_CTR_H

#include "types.h"

/* In-guest Peak Container Runtime — VFS images + HTTP ports.
 * Not OCI/Docker; no host bridge required.
 */

void ctr_init(void);

/* Build Dockerfile in context_dir into image tag. log may be NULL. */
int ctr_build(const char *context_dir, const char *tag, char *log, size_t log_cap);

/* Run image as named container on TCP/HTTP port (e.g. "18080"). */
int ctr_run(const char *image, const char *name, const char *port,
            char *log, size_t log_cap);

int ctr_stop(const char *name);
int ctr_ps(char *out, size_t out_cap);
int ctr_logs(const char *name, char *out, size_t out_cap);

/* Most recently built image this boot (tag + its EXPOSE port). Returns 0 if any. */
int ctr_last_built(char *tag, size_t tag_cap, char *port, size_t port_cap);

/* Read an image's EXPOSE port from its build metadata. Returns 0 on success. */
int ctr_image_expose(const char *tag, char *port, size_t port_cap);

/* Poll listeners / accepted connections (call from net worker). */
void ctr_poll(void);

/* Serve http://127.0.0.1:<port>/... from a running container rootfs. */
int ctr_http_get(const char *url, char *body, size_t body_cap, int *status_out);

/* Always succeeds for in-guest runtime. */
int ctr_ping(char *out, size_t out_cap);

#endif
