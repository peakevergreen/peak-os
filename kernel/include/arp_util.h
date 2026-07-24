#ifndef PEAK_ARP_UTIL_H
#define PEAK_ARP_UTIL_H

#ifdef PEAK_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#else
#include "types.h"
#endif

/* Fixed-size open-addressed ARP cache (power of two). */
#ifndef ARP_CACHE_MAX
#define ARP_CACHE_MAX 32
#endif

struct arp_entry {
    int valid;
    uint32_t ip;
    uint8_t mac[6];
};

/* Home bucket for ip (0 .. ARP_CACHE_MAX-1). */
unsigned arp_cache_home(uint32_t ip);

/*
 * Lookup by IP with bounded linear probing (at most n probes).
 * Returns 0 on hit (copies MAC), -1 on miss.
 */
int arp_cache_lookup(const struct arp_entry *cache, unsigned n, uint32_t ip,
                     uint8_t mac[6]);

/*
 * Insert or update. Probes at most n slots from the home bucket.
 * If the table is full, replaces the home-bucket entry.
 */
void arp_cache_store(struct arp_entry *cache, unsigned n, uint32_t ip,
                     const uint8_t mac[6]);

#endif
