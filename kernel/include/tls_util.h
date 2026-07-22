#ifndef PEAK_TLS_UTIL_H
#define PEAK_TLS_UTIL_H

#ifdef PEAK_HOST_TEST
#include <stddef.h>
#include <stdint.h>
#else
#include "types.h"
#endif

/* Lowercase hex, no separators; out must hold at least 2*n + 1 bytes. */
void tls_hex_encode(const uint8_t *in, size_t n, char *out);

/* One leading wildcard label: *.example.com matches foo.example.com */
int tls_hostname_matches_sni(const char *pattern, const char *host);

/*
 * Parse a TOFU store buffer ("host:hex64" lines).
 * Returns 1 = match, 0 = unknown host, -1 = digest mismatch (possible MITM).
 */
int tls_tofu_check_store(const char *store, const char *host, const char *hexdigest);

#endif
