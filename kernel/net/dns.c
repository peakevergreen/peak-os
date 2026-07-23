#include "net_internal.h"
#include "timer.h"
#include "util.h"

#define DNS_CACHE_SLOTS 16

struct dns_cache_ent {
    char     host[128];
    uint32_t ip;
    uint64_t expires;
    uint8_t  in_use;
};

static struct dns_cache_ent dns_cache[DNS_CACHE_SLOTS];

static void dns_host_norm(const char *in, char *out, size_t out_len) {
    size_t i = 0;
    if (!in || out_len == 0) {
        if (out_len)
            out[0] = '\0';
        return;
    }
    for (; in[i] && i + 1 < out_len; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        out[i] = c;
    }
    out[i] = '\0';
}

static uint32_t dns_cache_lookup(const char *host) {
    char norm[128];
    dns_host_norm(host, norm, sizeof(norm));
    uint64_t now = timer_ticks();
    for (int i = 0; i < DNS_CACHE_SLOTS; i++) {
        if (!dns_cache[i].in_use)
            continue;
        if (now >= dns_cache[i].expires) {
            dns_cache[i].in_use = 0;
            continue;
        }
        if (!strcmp(dns_cache[i].host, norm))
            return dns_cache[i].ip;
    }
    return 0;
}

static void dns_cache_store(const char *host, uint32_t ip) {
    if (!host || !ip)
        return;
    char norm[128];
    dns_host_norm(host, norm, sizeof(norm));
    uint64_t now = timer_ticks();
    int slot = -1;
    for (int i = 0; i < DNS_CACHE_SLOTS; i++) {
        if (dns_cache[i].in_use && !strcmp(dns_cache[i].host, norm)) {
            slot = i;
            break;
        }
        if (!dns_cache[i].in_use && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        /* Evict soonest-expiring entry. */
        slot = 0;
        for (int i = 1; i < DNS_CACHE_SLOTS; i++) {
            if (dns_cache[i].expires < dns_cache[slot].expires)
                slot = i;
        }
    }
    size_t n = 0;
    for (; norm[n] && n + 1 < sizeof(dns_cache[slot].host); n++)
        dns_cache[slot].host[n] = norm[n];
    dns_cache[slot].host[n] = '\0';
    dns_cache[slot].ip = ip;
    dns_cache[slot].expires = now + NET_DNS_CACHE_TTL_TICKS;
    dns_cache[slot].in_use = 1;
}

void net_handle_dns_udp(const uint8_t *pkt, uint16_t ulen) {
    /* DNS response */
    if (ulen < 12 + 8)
        return;
    const uint8_t *dns = pkt + 8;
    uint16_t payload = (uint16_t)(ulen - 8);
    uint16_t id = ((uint16_t)dns[0] << 8) | dns[1];
    if (id != dns_txid)
        return;
    uint16_t ancount = ((uint16_t)dns[6] << 8) | dns[7];
    size_t i = 12;
    /* skip question */
    while (i < payload && dns[i])
        i += (size_t)dns[i] + 1;
    i += 5; /* 0 + type + class */
    for (uint16_t a = 0; a < ancount && i + 12 <= payload; a++) {
        if ((dns[i] & 0xC0) == 0xC0)
            i += 2;
        else {
            while (i < payload && dns[i])
                i += (size_t)dns[i] + 1;
            i++;
        }
        if (i + 10 > payload)
            break;
        uint16_t typ = ((uint16_t)dns[i] << 8) | dns[i + 1];
        uint16_t rdlen = ((uint16_t)dns[i + 8] << 8) | dns[i + 9];
        i += 10;
        if (typ == 1 && rdlen == 4 && i + 4 <= payload) {
            dns_answer_ip = ((uint32_t)dns[i] << 24) | ((uint32_t)dns[i + 1] << 16) |
                            ((uint32_t)dns[i + 2] << 8) | dns[i + 3];
            dns_done = 1;
            return;
        }
        i += rdlen;
    }
}

uint32_t net_dns_resolve(const char *hostname, uint32_t timeout_ticks) {
    attempt_stats.dns++;
    if (!net_ready() || !hostname || !hostname[0])
        return 0;
    /* dotted quad? */
    int dots = 0, ok = 1;
    uint32_t parts[4] = {0};
    int pi = 0;
    for (const char *p = hostname; *p; p++) {
        if (*p == '.') {
            dots++;
            pi++;
            if (pi > 3) {
                ok = 0;
                break;
            }
        } else if (*p >= '0' && *p <= '9') {
            parts[pi] = parts[pi] * 10 + (uint32_t)(*p - '0');
            if (parts[pi] > 255)
                ok = 0;
        } else {
            ok = 0;
            break;
        }
    }
    if (ok && dots == 3)
        return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];

    uint32_t cached = dns_cache_lookup(hostname);
    if (cached)
        return cached;

    uint8_t q[256];
    size_t o = 0;
    dns_txid++;
    q[o++] = (uint8_t)(dns_txid >> 8);
    q[o++] = (uint8_t)(dns_txid & 0xFF);
    q[o++] = 0x01;
    q[o++] = 0x00; /* recursion */
    q[o++] = 0x00;
    q[o++] = 0x01; /* QDCOUNT */
    q[o++] = 0x00;
    q[o++] = 0x00;
    q[o++] = 0x00;
    q[o++] = 0x00;
    q[o++] = 0x00;
    q[o++] = 0x00;
    /* QNAME */
    const char *h = hostname;
    while (*h) {
        const char *dot = h;
        while (*dot && *dot != '.')
            dot++;
        size_t lab = (size_t)(dot - h);
        if (lab == 0 || lab > 63 || o + lab + 1 >= sizeof(q))
            return 0;
        q[o++] = (uint8_t)lab;
        memcpy(q + o, h, lab);
        o += lab;
        if (*dot == '.')
            h = dot + 1;
        else {
            h = dot;
            break;
        }
    }
    q[o++] = 0;
    q[o++] = 0x00;
    q[o++] = 0x01; /* A */
    q[o++] = 0x00;
    q[o++] = 0x01; /* IN */

    dns_done = 0;
    dns_answer_ip = 0;
    dns_sport = ephem_port++;
    if (ephem_port < 40000)
        ephem_port = 40000;
    net_udp_send(local_dns, dns_sport, 53, q, (uint16_t)o);

    uint64_t start = timer_ticks();
    while (!net_timed_out(start, timeout_ticks)) {
        net_poll();
        if (dns_done) {
            dns_cache_store(hostname, dns_answer_ip);
            return dns_answer_ip;
        }
        hlt_if_enabled();
    }
    return 0;
}
