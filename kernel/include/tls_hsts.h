#ifndef PEAK_TLS_HSTS_H
#define PEAK_TLS_HSTS_H

#include "types.h"

#define HSTS_PATH "/etc/peak/tls-hsts"
#define HSTS_MAX  4096

/* Parse Strict-Transport-Security from response headers; store host. */
void hsts_process_header(const char *host, const char *raw_headers);

/* 1 if host should be upgraded to HTTPS. */
int hsts_should_upgrade(const char *host);

void hsts_clear(void);

#ifdef PEAK_HOST_TEST
void hsts_host_put(const char *host, uint32_t expiry_ticks);
int hsts_host_test_count(void);
#endif

#endif
