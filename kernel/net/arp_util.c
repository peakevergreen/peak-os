#ifdef PEAK_HOST_TEST
#include "../include/arp_util.h"
#include <string.h>
#else
#include "arp_util.h"
#include "util.h"
#endif

unsigned arp_cache_home(uint32_t ip) {
    /* Multiplicative hash; ARP_CACHE_MAX is a power of two. */
    return (unsigned)(ip * 2654435761u) & (ARP_CACHE_MAX - 1);
}

int arp_cache_lookup(const struct arp_entry *cache, unsigned n, uint32_t ip,
                     uint8_t mac[6]) {
    if (!cache || !mac || n == 0)
        return -1;
    unsigned home = arp_cache_home(ip) % n;
    /* Bounded probe of all slots from home so replacement cannot hide hits. */
    for (unsigned probe = 0; probe < n; probe++) {
        unsigned i = (home + probe) % n;
        if (cache[i].valid && cache[i].ip == ip) {
            memcpy(mac, cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

void arp_cache_store(struct arp_entry *cache, unsigned n, uint32_t ip,
                     const uint8_t mac[6]) {
    if (!cache || !mac || n == 0)
        return;
    unsigned home = arp_cache_home(ip) % n;
    int empty = -1;
    for (unsigned probe = 0; probe < n; probe++) {
        unsigned i = (home + probe) % n;
        if (cache[i].valid && cache[i].ip == ip) {
            memcpy(cache[i].mac, mac, 6);
            return;
        }
        if (!cache[i].valid && empty < 0)
            empty = (int)i;
    }
    unsigned slot = empty >= 0 ? (unsigned)empty : home;
    cache[slot].valid = 1;
    cache[slot].ip = ip;
    memcpy(cache[slot].mac, mac, 6);
}
