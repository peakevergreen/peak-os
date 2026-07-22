#include "net.h"
#include "net_internal.h"
#include "peak_errno.h"
#include "netdev.h"
#include "timer.h"
#include "util.h"
#include "serial.h"
#include "sync.h"

struct peak_net_config boot_net = {
    .mode = PEAK_NET_DHCP_FALLBACK,
    .ip = NET_IP_DEFAULT,
    .mask = NET_MASK_DEFAULT,
    .gw = NET_GW_DEFAULT,
    .dns = NET_DNS_DEFAULT,
    .dhcp_timeout_ticks = 300,
};
const char *addr_mode = "static";

int net_up;
uint8_t local_mac[6];
uint32_t local_ip = NET_IP_DEFAULT;
uint32_t local_mask = NET_MASK_DEFAULT;
uint32_t local_gw = NET_GW_DEFAULT;
uint32_t local_dns = NET_DNS_DEFAULT;

struct net_attempt_stats attempt_stats;

struct arp_entry arp_cache[ARP_CACHE_MAX];
uint32_t arp_wait_ip;
int arp_resolved;
uint8_t arp_wait_mac[6];

struct tcp_conn tcps[NET_TCP_MAX];
struct tcp_listener listens[NET_LISTEN_MAX];
int tcp_cur;

struct spinlock net_lock;
int net_lock_ready;

int dhcp_active;
uint16_t dhcp_xid_hi;
uint32_t dhcp_xid;
struct dhcp_lease dhcp_pending;
int dhcp_have_offer;
int dhcp_have_ack;
int dhcp_have_nak;

uint16_t ephem_port = 40000;
uint16_t dns_txid;
uint32_t dns_answer_ip;
int dns_done;
uint16_t dns_sport;

int http_needs_tls_flag;

void net_set_boot_config(const struct peak_net_config *cfg) {
    if (!cfg)
        return;
    boot_net = *cfg;
    if (!boot_net.ip)
        boot_net.ip = NET_IP_DEFAULT;
    if (!boot_net.mask)
        boot_net.mask = NET_MASK_DEFAULT;
    if (!boot_net.gw)
        boot_net.gw = NET_GW_DEFAULT;
    if (!boot_net.dns)
        boot_net.dns = NET_DNS_DEFAULT;
    if (!boot_net.dhcp_timeout_ticks)
        boot_net.dhcp_timeout_ticks = 300;
}

void net_attempt_stats_get(struct net_attempt_stats *out) {
    if (out)
        *out = attempt_stats;
}

void net_attempt_stats_reset(void) {
    memset(&attempt_stats, 0, sizeof(attempt_stats));
}

void net_attempt_stats_note_tls(void) {
    attempt_stats.tls++;
}

void net_lock_acquire(void) {
    if (net_lock_ready)
        spin_lock(&net_lock);
}

void net_lock_release(void) {
    if (net_lock_ready)
        spin_unlock(&net_lock);
}

void net_format_ip(uint32_t ip, char *buf, size_t cap) {
    snprintf(buf, cap, "%u.%u.%u.%u",
             (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
             (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
}

uint16_t net_checksum(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t net_tcp_checksum(uint32_t src, uint32_t dst, const void *tcp, size_t tcp_len) {
    uint32_t sum = 0;
    sum += (src >> 16) & 0xFFFF;
    sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF;
    sum += dst & 0xFFFF;
    sum += IP_TCP;
    sum += (uint32_t)tcp_len;
    const uint8_t *p = tcp;
    size_t len = tcp_len;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

int net_eth_send(uint16_t ethertype, const uint8_t dst[6], const void *payload, uint16_t plen) {
    uint8_t frame[1518];
    if (plen + 14 > sizeof(frame))
        return PEAK_ENOBUFS;
    for (int i = 0; i < 6; i++) {
        frame[i] = dst[i];
        frame[6 + i] = local_mac[i];
    }
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
    memcpy(frame + 14, payload, plen);
    return netdev_send(frame, (uint16_t)(plen + 14));
}

int net_ip_send(uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t plen) {
    uint8_t dmac[6];
    if (net_resolve_next_hop_mac(dst_ip, dmac, 200) != 0)
        return PEAK_ENETUNREACH;
    uint8_t pkt[1500];
    uint16_t total = (uint16_t)(20 + plen);
    if (total > sizeof(pkt))
        return PEAK_ENOBUFS;
    memset(pkt, 0, 20);
    pkt[0] = 0x45;
    pkt[2] = (uint8_t)(total >> 8);
    pkt[3] = (uint8_t)(total & 0xFF);
    static uint16_t ip_id;
    ip_id++;
    pkt[4] = (uint8_t)(ip_id >> 8);
    pkt[5] = (uint8_t)(ip_id & 0xFF);
    pkt[8] = 64;
    pkt[9] = proto;
    pkt[12] = (uint8_t)((local_ip >> 24) & 0xFF);
    pkt[13] = (uint8_t)((local_ip >> 16) & 0xFF);
    pkt[14] = (uint8_t)((local_ip >> 8) & 0xFF);
    pkt[15] = (uint8_t)(local_ip & 0xFF);
    pkt[16] = (uint8_t)((dst_ip >> 24) & 0xFF);
    pkt[17] = (uint8_t)((dst_ip >> 16) & 0xFF);
    pkt[18] = (uint8_t)((dst_ip >> 8) & 0xFF);
    pkt[19] = (uint8_t)(dst_ip & 0xFF);
    uint16_t csum = net_checksum(pkt, 20);
    pkt[10] = (uint8_t)(csum >> 8);
    pkt[11] = (uint8_t)(csum & 0xFF);
    memcpy(pkt + 20, payload, plen);
    return net_eth_send(ETH_IP, dmac, pkt, total);
}

int net_udp_send(uint32_t dst_ip, uint16_t sport, uint16_t dport,
                 const void *data, uint16_t dlen) {
    uint8_t pkt[512];
    uint16_t total = (uint16_t)(8 + dlen);
    if (total > sizeof(pkt))
        return PEAK_ENOBUFS;
    pkt[0] = (uint8_t)(sport >> 8);
    pkt[1] = (uint8_t)(sport & 0xFF);
    pkt[2] = (uint8_t)(dport >> 8);
    pkt[3] = (uint8_t)(dport & 0xFF);
    pkt[4] = (uint8_t)(total >> 8);
    pkt[5] = (uint8_t)(total & 0xFF);
    pkt[6] = 0;
    pkt[7] = 0;
    memcpy(pkt + 8, data, dlen);
    return net_ip_send(dst_ip, IP_UDP, pkt, total);
}

static void handle_icmp(uint32_t src, const uint8_t *pkt, uint16_t len) {
    if (len < 8 || pkt[0] != 8) /* echo request */
        return;
    uint8_t reply[1500];
    if (len > sizeof(reply))
        return;
    memcpy(reply, pkt, len);
    reply[0] = 0; /* echo reply */
    reply[2] = 0;
    reply[3] = 0;
    uint16_t c = net_checksum(reply, len);
    reply[2] = (uint8_t)(c >> 8);
    reply[3] = (uint8_t)(c & 0xFF);
    net_ip_send(src, IP_ICMP, reply, len);
}

static void handle_udp(uint32_t src, const uint8_t *pkt, uint16_t len) {
    (void)src;
    if (len < 8)
        return;
    uint16_t dport = ((uint16_t)pkt[2] << 8) | pkt[3];
    uint16_t ulen = ((uint16_t)pkt[4] << 8) | pkt[5];
    if (ulen > len)
        ulen = len;
    if (dport == 68 && dhcp_active) {
        net_handle_dhcp_udp(pkt, ulen);
        return;
    }
    if (dport != dns_sport)
        return;
    net_handle_dns_udp(pkt, ulen);
}

static void handle_ip(const uint8_t *pkt, uint16_t len) {
    if (len < 20)
        return;
    uint8_t ihl = (pkt[0] & 0x0F) * 4;
    if (ihl < 20 || ihl > len)
        return;
    uint16_t total = ((uint16_t)pkt[2] << 8) | pkt[3];
    if (total > len)
        total = len;
    uint8_t proto = pkt[9];
    uint32_t src = ((uint32_t)pkt[12] << 24) | ((uint32_t)pkt[13] << 16) |
                   ((uint32_t)pkt[14] << 8) | pkt[15];
    uint32_t dst = ((uint32_t)pkt[16] << 24) | ((uint32_t)pkt[17] << 16) |
                   ((uint32_t)pkt[18] << 8) | pkt[19];
    if (dst != local_ip && dst != 0xFFFFFFFFu && !dhcp_active)
        return;
    const uint8_t *payload = pkt + ihl;
    uint16_t plen = (uint16_t)(total - ihl);
    if (proto == IP_ICMP)
        handle_icmp(src, payload, plen);
    else if (proto == IP_UDP)
        handle_udp(src, payload, plen);
    else if (proto == IP_TCP)
        net_handle_tcp(src, payload, plen);
}

void net_poll(void) {
    uint8_t buf[2048];
    net_lock_acquire();
    for (;;) {
        int n = netdev_recv(buf, sizeof(buf));
        if (n < 14)
            break;
        uint16_t et = ((uint16_t)buf[12] << 8) | buf[13];
        if (et == ETH_ARP)
            net_handle_arp(buf + 14, (uint16_t)(n - 14));
        else if (et == ETH_IP)
            handle_ip(buf + 14, (uint16_t)(n - 14));
    }
    net_lock_release();
}

int net_init(void) {
    spin_init(&net_lock, "net");
    net_lock_ready = 1;
    if (netdev_init() != 0) {
        net_up = 0;
        return PEAK_EIO;
    }
    netdev_get_mac(local_mac);
    net_up = 1;
    memset(tcps, 0, sizeof(tcps));
    memset(listens, 0, sizeof(listens));
    memset(arp_cache, 0, sizeof(arp_cache));
    tcp_cur = 0;
    local_ip = 0;
    local_mask = boot_net.mask;
    local_gw = boot_net.gw;
    local_dns = boot_net.dns;
    if (net_dhcp_try(boot_net.dhcp_timeout_ticks) != 0) {
        serial_write_str("net: address configuration failed\n");
        net_up = 0;
        return PEAK_EDHCP;
    }
    /* Prime ARP for gateway when known */
    if (local_gw) {
        net_arp_request(local_gw);
        for (int i = 0; i < 50; i++) {
            net_poll();
            uint8_t mac[6];
            if (net_arp_cache_get(local_gw, mac) == 0)
                break;
            for (volatile int j = 0; j < 10000; j++)
                ;
        }
    }
    return 0;
}

int net_ready(void) {
    return net_up && netdev_ready();
}

void net_get_info(struct net_info *out) {
    memset(out, 0, sizeof(*out));
    out->up = net_ready();
    for (int i = 0; i < 6; i++)
        out->mac[i] = local_mac[i];
    out->ip = local_ip;
    out->mask = local_mask;
    out->gw = local_gw;
    out->dns = local_dns;
    out->driver = "e1000";
    out->addr_mode = addr_mode;
}
