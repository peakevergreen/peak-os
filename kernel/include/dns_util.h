#ifndef PEAK_DNS_UTIL_H
#define PEAK_DNS_UTIL_H

#ifdef PEAK_HOST_TEST
#include <stddef.h>
#else
#include "types.h"
#endif

/* Lowercase copy for DNS cache keys (lite). */
void dns_host_normalize(char *out, size_t out_len, const char *in);

/* 1 = plausible hostname for A lookup; 0 = reject. */
int dns_host_valid(const char *host);

#endif
