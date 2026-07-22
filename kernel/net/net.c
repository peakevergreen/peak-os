#include "net.h"
#include "dhcp_util.h"
#include "tls.h"
#include "http_util.h"
#include "netdev.h"
#include "timer.h"
#include "util.h"
#include "serial.h"
#include "sync.h"
#include "cap.h"

/* QEMU user-networking defaults */
#define NET_IP_DEFAULT   0x0A00020F  /* 10.0.2.15 */
#define NET_MASK_DEFAULT 0xFFFFFF00  /* 255.255.255.0 */
#define NET_GW_DEFAULT   0x0A000202  /* 10.0.2.2 */
#define NET_DNS_DEFAULT  0x0A000203  /* 10.0.2.3 */

static struct peak_net_config boot_net = {
    .mode = PEAK_NET_DHCP_FALLBACK,
    .ip = NET_IP_DEFAULT,
    .mask = NET_MASK_DEFAULT,
    .gw = NET_GW_DEFAULT,
    .dns = NET_DNS_DEFAULT,
    .dhcp_timeout_ticks = 300,
};
static const char *addr_mode = "static";

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

#define ETH_IP   0x0800
#define ETH_ARP  0x0806
#define IP_ICMP  1
#define IP_TCP   6
#define IP_UDP   17

#define ARP_REQUEST 1
#define ARP_REPLY   2

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_MSS 1400
#define RX_RING 16

static int net_up;
static uint8_t local_mac[6];
static uint32_t local_ip = NET_IP_DEFAULT;
static uint32_t local_mask = NET_MASK_DEFAULT;
static uint32_t local_gw = NET_GW_DEFAULT;
static uint32_t local_dns = NET_DNS_DEFAULT;

static struct net_attempt_stats attempt_stats;

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

#define ARP_CACHE_MAX 16
struct arp_entry {
    int valid;
    uint32_t ip;
    uint8_t mac[6];
};
static struct arp_entry arp_cache[ARP_CACHE_MAX];
static uint32_t arp_wait_ip;
static int arp_resolved;
static uint8_t arp_wait_mac[6];

/* TCP multi-connection */
enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_CLOSE_WAIT,
    TCP_FIN_WAIT
};

struct tcp_conn {
    int state;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint8_t rx[32768];
    size_t rx_len;
    int got_fin;
    int is_server;
    int accepted;
};

struct tcp_listener {
    int used;
    uint16_t port;
};

static struct tcp_conn tcps[NET_TCP_MAX];
static struct tcp_listener listens[NET_LISTEN_MAX];
static int tcp_cur; /* active slot for legacy API */
static struct spinlock net_lock;
static int net_lock_ready;
static int dhcp_active;
static uint16_t dhcp_xid_hi;
static uint32_t dhcp_xid;
static struct dhcp_lease dhcp_pending;
static int dhcp_have_offer;
static int dhcp_have_ack;
static int dhcp_have_nak;

/* Compatibility shims for legacy slot API (never use across error returns in RX). */
#define tcp_state       (tcps[tcp_cur].state)
#define tcp_remote_ip   (tcps[tcp_cur].remote_ip)
#define tcp_remote_port (tcps[tcp_cur].remote_port)
#define tcp_local_port  (tcps[tcp_cur].local_port)
#define tcp_snd_nxt     (tcps[tcp_cur].snd_nxt)
#define tcp_rcv_nxt     (tcps[tcp_cur].rcv_nxt)
#define tcp_rx          (tcps[tcp_cur].rx)
#define tcp_rx_len      (tcps[tcp_cur].rx_len)
#define tcp_got_fin     (tcps[tcp_cur].got_fin)

static void net_lock_acquire(void) {
    if (net_lock_ready)
        spin_lock(&net_lock);
}

static void net_lock_release(void) {
    if (net_lock_ready)
        spin_unlock(&net_lock);
}

static uint16_t ephem_port = 40000;
static uint16_t dns_txid;

static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8) |
           ((x & 0xFF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}
static inline uint32_t htonl(uint32_t x) { return bswap32(x); }

void net_format_ip(uint32_t ip, char *buf, size_t cap) {
    snprintf(buf, cap, "%u.%u.%u.%u",
             (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
             (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
}

static uint16_t checksum(const void *data, size_t len) {
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

static uint16_t tcp_checksum(uint32_t src, uint32_t dst, const void *tcp, size_t tcp_len) {
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

static int eth_send(uint16_t ethertype, const uint8_t dst[6], const void *payload, uint16_t plen) {
    uint8_t frame[1518];
    if (plen + 14 > sizeof(frame))
        return -1;
    for (int i = 0; i < 6; i++) {
        frame[i] = dst[i];
        frame[6 + i] = local_mac[i];
    }
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
    memcpy(frame + 14, payload, plen);
    return netdev_send(frame, (uint16_t)(plen + 14));
}

static void arp_cache_put(uint32_t ip, const uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].valid = 1;
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    arp_cache[0].valid = 1;
    arp_cache[0].ip = ip;
    memcpy(arp_cache[0].mac, mac, 6);
}

static int arp_cache_get(uint32_t ip, uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

static int arp_request(uint32_t tip) {
    uint8_t pkt[28];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x00;
    pkt[1] = 0x01; /* HTYPE Ethernet */
    pkt[2] = 0x08;
    pkt[3] = 0x00; /* PTYPE IP */
    pkt[4] = 6;
    pkt[5] = 4;
    pkt[6] = 0x00;
    pkt[7] = ARP_REQUEST;
    for (int i = 0; i < 6; i++)
        pkt[8 + i] = local_mac[i];
    pkt[14] = (uint8_t)((local_ip >> 24) & 0xFF);
    pkt[15] = (uint8_t)((local_ip >> 16) & 0xFF);
    pkt[16] = (uint8_t)((local_ip >> 8) & 0xFF);
    pkt[17] = (uint8_t)(local_ip & 0xFF);
    /* THA zeros */
    pkt[24] = (uint8_t)((tip >> 24) & 0xFF);
    pkt[25] = (uint8_t)((tip >> 16) & 0xFF);
    pkt[26] = (uint8_t)((tip >> 8) & 0xFF);
    pkt[27] = (uint8_t)(tip & 0xFF);
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    arp_wait_ip = tip;
    arp_resolved = 0;
    return eth_send(ETH_ARP, bcast, pkt, 28);
}

static int resolve_next_hop_mac(uint32_t dst_ip, uint8_t mac[6], uint32_t timeout_ticks) {
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (dst_ip == 0xFFFFFFFFu || dst_ip == (local_ip | ~local_mask)) {
        memcpy(mac, bcast, 6);
        return 0;
    }
    uint32_t tip = dst_ip;
    if (local_mask && (dst_ip & local_mask) != (local_ip & local_mask))
        tip = local_gw;
    if (arp_cache_get(tip, mac) == 0)
        return 0;
    arp_request(tip);
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (arp_resolved && arp_wait_ip == tip) {
            memcpy(mac, arp_wait_mac, 6);
            arp_cache_put(tip, mac);
            return 0;
        }
        if (arp_cache_get(tip, mac) == 0)
            return 0;
        hlt();
    }
    return -1;
}

static int ip_send(uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t plen) {
    uint8_t dmac[6];
    if (resolve_next_hop_mac(dst_ip, dmac, 200) != 0)
        return -1;
    uint8_t pkt[1500];
    uint16_t total = (uint16_t)(20 + plen);
    if (total > sizeof(pkt))
        return -1;
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
    uint16_t csum = checksum(pkt, 20);
    pkt[10] = (uint8_t)(csum >> 8);
    pkt[11] = (uint8_t)(csum & 0xFF);
    memcpy(pkt + 20, payload, plen);
    return eth_send(ETH_IP, dmac, pkt, total);
}

static int tcp_send_seg_slot(int slot, uint8_t flags, const void *data, uint16_t dlen) {
    if (slot < 0 || slot >= NET_TCP_MAX)
        return -1;
    struct tcp_conn *c = &tcps[slot];
    uint8_t seg[1500];
    uint16_t hdr = 20;
    uint16_t total = (uint16_t)(hdr + dlen);
    if (total > sizeof(seg))
        return -1;
    memset(seg, 0, hdr);
    seg[0] = (uint8_t)(c->local_port >> 8);
    seg[1] = (uint8_t)(c->local_port & 0xFF);
    seg[2] = (uint8_t)(c->remote_port >> 8);
    seg[3] = (uint8_t)(c->remote_port & 0xFF);
    uint32_t seq = htonl(c->snd_nxt);
    memcpy(seg + 4, &seq, 4);
    uint32_t ack = htonl(c->rcv_nxt);
    memcpy(seg + 8, &ack, 4);
    seg[12] = (uint8_t)((hdr / 4) << 4);
    seg[13] = flags;
    /* Advertise actual receive space so senders are not throttled to a
     * stale 8 KiB window (and stop sending into a full buffer). */
    size_t space = sizeof(c->rx) - c->rx_len;
    uint16_t win = space > 0xFFFF ? 0xFFFF : (uint16_t)space;
    seg[14] = (uint8_t)(win >> 8);
    seg[15] = (uint8_t)(win & 0xFF);
    if (dlen)
        memcpy(seg + hdr, data, dlen);
    uint16_t csum = tcp_checksum(local_ip, c->remote_ip, seg, total);
    seg[16] = (uint8_t)(csum >> 8);
    seg[17] = (uint8_t)(csum & 0xFF);
    int r = ip_send(c->remote_ip, IP_TCP, seg, total);
    if (r == 0) {
        if (flags & TCP_SYN)
            c->snd_nxt++;
        if (flags & TCP_FIN)
            c->snd_nxt++;
        c->snd_nxt += dlen;
    }
    return r;
}

static int tcp_send_seg(uint8_t flags, const void *data, uint16_t dlen) {
    return tcp_send_seg_slot(tcp_cur, flags, data, dlen);
}

static void handle_arp(const uint8_t *pkt, uint16_t len) {
    if (len < 28)
        return;
    uint16_t op = ((uint16_t)pkt[6] << 8) | pkt[7];
    uint32_t tip = ((uint32_t)pkt[24] << 24) | ((uint32_t)pkt[25] << 16) |
                   ((uint32_t)pkt[26] << 8) | pkt[27];
    uint32_t sip = ((uint32_t)pkt[14] << 24) | ((uint32_t)pkt[15] << 16) |
                   ((uint32_t)pkt[16] << 8) | pkt[17];
    if (op == ARP_REQUEST && tip == local_ip) {
        uint8_t reply[28];
        memset(reply, 0, sizeof(reply));
        reply[0] = 0x00;
        reply[1] = 0x01;
        reply[2] = 0x08;
        reply[3] = 0x00;
        reply[4] = 6;
        reply[5] = 4;
        reply[6] = 0x00;
        reply[7] = ARP_REPLY;
        for (int i = 0; i < 6; i++) {
            reply[8 + i] = local_mac[i];
            reply[18 + i] = pkt[8 + i];
        }
        reply[14] = (uint8_t)((local_ip >> 24) & 0xFF);
        reply[15] = (uint8_t)((local_ip >> 16) & 0xFF);
        reply[16] = (uint8_t)((local_ip >> 8) & 0xFF);
        reply[17] = (uint8_t)(local_ip & 0xFF);
        reply[24] = pkt[14];
        reply[25] = pkt[15];
        reply[26] = pkt[16];
        reply[27] = pkt[17];
        eth_send(ETH_ARP, pkt + 8, reply, 28);
        arp_cache_put(sip, pkt + 8);
    } else if (op == ARP_REPLY) {
        arp_cache_put(sip, pkt + 8);
        if (sip == arp_wait_ip) {
            for (int i = 0; i < 6; i++)
                arp_wait_mac[i] = pkt[8 + i];
            arp_resolved = 1;
        }
    }
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
    uint16_t c = checksum(reply, len);
    reply[2] = (uint8_t)(c >> 8);
    reply[3] = (uint8_t)(c & 0xFF);
    ip_send(src, IP_ICMP, reply, len);
}

static uint32_t dns_answer_ip;
static int dns_done;

static uint16_t dns_sport;

static void handle_dhcp_udp(const uint8_t *udp, uint16_t ulen) {
    if (ulen < 8 + 240)
        return;
    const uint8_t *dhcp = udp + 8;
    uint16_t dlen = (uint16_t)(ulen - 8);
    if (dhcp[0] != 2) /* BOOTREPLY */
        return;
    uint32_t xid = ((uint32_t)dhcp[4] << 24) | ((uint32_t)dhcp[5] << 16) |
                   ((uint32_t)dhcp[6] << 8) | dhcp[7];
    if (xid != dhcp_xid)
        return;
    uint32_t yi = ((uint32_t)dhcp[16] << 24) | ((uint32_t)dhcp[17] << 16) |
                  ((uint32_t)dhcp[18] << 8) | dhcp[19];
    struct dhcp_lease lease;
    if (dhcp_parse_options(dhcp + 236, (size_t)(dlen - 236), &lease) != 0)
        return;
    lease.yiaddr = yi;
    if (lease.msg_type == 2) {
        dhcp_pending = lease;
        dhcp_have_offer = 1;
    } else if (lease.msg_type == 5) {
        dhcp_pending = lease;
        dhcp_have_ack = 1;
    } else if (lease.msg_type == 6) {
        dhcp_have_nak = 1;
    }
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
        handle_dhcp_udp(pkt, ulen);
        return;
    }
    if (dport != dns_sport)
        return;
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

static int tcp_find_slot(uint32_t src, uint16_t sport, uint16_t dport) {
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].state == TCP_CLOSED || tcps[i].state == TCP_LISTEN)
            continue;
        if (tcps[i].remote_ip == src && tcps[i].remote_port == sport &&
            tcps[i].local_port == dport)
            return i;
    }
    return -1;
}

static int tcp_find_listener(uint16_t port) {
    for (int i = 0; i < NET_LISTEN_MAX; i++) {
        if (listens[i].used && listens[i].port == port)
            return i;
    }
    return -1;
}

static int tcp_alloc_slot(void) {
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].state == TCP_CLOSED)
            return i;
    }
    return -1;
}

static void handle_tcp(uint32_t src, const uint8_t *pkt, uint16_t len) {
    if (len < 20)
        return;
    uint16_t sport = ((uint16_t)pkt[0] << 8) | pkt[1];
    uint16_t dport = ((uint16_t)pkt[2] << 8) | pkt[3];
    uint32_t seq = ((uint32_t)pkt[4] << 24) | ((uint32_t)pkt[5] << 16) |
                   ((uint32_t)pkt[6] << 8) | pkt[7];
    uint8_t data_off = (pkt[12] >> 4) * 4;
    uint8_t flags = pkt[13];
    if (data_off > len)
        return;
    uint16_t dlen = (uint16_t)(len - data_off);
    const uint8_t *data = pkt + data_off;

    int slot = tcp_find_slot(src, sport, dport);
    if (slot < 0) {
        /* Passive open: SYN to a listening port */
        if ((flags & TCP_SYN) && !(flags & TCP_ACK) && tcp_find_listener(dport) >= 0) {
            slot = tcp_alloc_slot();
            if (slot < 0)
                return;
            struct tcp_conn *nc = &tcps[slot];
            memset(nc, 0, sizeof(*nc));
            nc->remote_ip = src;
            nc->remote_port = sport;
            nc->local_port = dport;
            nc->rcv_nxt = seq + 1;
            nc->snd_nxt = 0x2000u + (uint32_t)(timer_ticks() & 0xFFFF);
            nc->is_server = 1;
            nc->accepted = 0;
            nc->state = TCP_ESTABLISHED;
            tcp_send_seg_slot(slot, TCP_SYN | TCP_ACK, NULL, 0);
        }
        return;
    }

    struct tcp_conn *c = &tcps[slot];

    if (flags & TCP_RST) {
        c->state = TCP_CLOSED;
        return;
    }

    if (c->state == TCP_SYN_SENT && (flags & TCP_SYN) && (flags & TCP_ACK)) {
        c->rcv_nxt = seq + 1;
        uint32_t peer_ack = ((uint32_t)pkt[8] << 24) | ((uint32_t)pkt[9] << 16) |
                            ((uint32_t)pkt[10] << 8) | pkt[11];
        c->snd_nxt = peer_ack;
        c->state = TCP_ESTABLISHED;
        tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
        return;
    }

    if (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT ||
        c->state == TCP_CLOSE_WAIT) {
        if (dlen > 0 && seq == c->rcv_nxt) {
            size_t space = sizeof(c->rx) - c->rx_len;
            if ((size_t)dlen > space) {
                tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
            } else {
                memcpy(c->rx + c->rx_len, data, dlen);
                c->rx_len += dlen;
                c->rcv_nxt += dlen;
                tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
            }
        } else if (dlen > 0 && seq + dlen > c->rcv_nxt) {
            tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
        }
        if ((flags & TCP_FIN) && c->state != TCP_CLOSE_WAIT) {
            c->rcv_nxt++;
            c->got_fin = 1;
            tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
            c->state = TCP_CLOSE_WAIT;
        }
    }
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
        handle_tcp(src, payload, plen);
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
            handle_arp(buf + 14, (uint16_t)(n - 14));
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
        return -1;
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
        return -1;
    }
    /* Prime ARP for gateway when known */
    if (local_gw) {
        arp_request(local_gw);
        for (int i = 0; i < 50; i++) {
            net_poll();
            uint8_t mac[6];
            if (arp_cache_get(local_gw, mac) == 0)
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

static int udp_send(uint32_t dst_ip, uint16_t sport, uint16_t dport,
                    const void *data, uint16_t dlen) {
    uint8_t pkt[512];
    uint16_t total = (uint16_t)(8 + dlen);
    if (total > sizeof(pkt))
        return -1;
    pkt[0] = (uint8_t)(sport >> 8);
    pkt[1] = (uint8_t)(sport & 0xFF);
    pkt[2] = (uint8_t)(dport >> 8);
    pkt[3] = (uint8_t)(dport & 0xFF);
    pkt[4] = (uint8_t)(total >> 8);
    pkt[5] = (uint8_t)(total & 0xFF);
    pkt[6] = 0;
    pkt[7] = 0;
    memcpy(pkt + 8, data, dlen);
    return ip_send(dst_ip, IP_UDP, pkt, total);
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
    udp_send(local_dns, dns_sport, 53, q, (uint16_t)o);

    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (dns_done)
            return dns_answer_ip;
        hlt_if_enabled();
    }
    return 0;
}

int net_tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ticks) {
    attempt_stats.tcp++;
    if (!net_ready())
        return -1;
    if (!privacy_net_client_allowed())
        return -1;
    int slot = -1;
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].state == TCP_CLOSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* Reuse current slot */
        net_tcp_close();
        slot = tcp_cur;
    }
    tcp_cur = slot;
    memset(&tcps[slot], 0, sizeof(tcps[slot]));
    tcp_remote_ip = ip;
    tcp_remote_port = port;
    tcp_local_port = ephem_port++;
    if (ephem_port < 40000)
        ephem_port = 40000;
    tcp_snd_nxt = 0x1000u + (uint32_t)(timer_ticks() & 0xFFFF);
    tcp_rcv_nxt = 0;
    tcp_rx_len = 0;
    tcp_got_fin = 0;
    tcp_state = TCP_SYN_SENT;
    if (tcp_send_seg(TCP_SYN, NULL, 0) != 0)
        return -1;
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (tcp_state == TCP_ESTABLISHED)
            return 0;
        if (tcp_state == TCP_CLOSED)
            return -1;
        hlt_if_enabled();
    }
    tcp_state = TCP_CLOSED;
    return -1;
}

int net_tcp_active_count(void) {
    int n = 0;
    for (int i = 0; i < NET_TCP_MAX; i++)
        if (tcps[i].state != TCP_CLOSED)
            n++;
    return n;
}

int net_tcp_listen(uint16_t port) {
    if (!port)
        return -1;
    /* LAN expose requires CAP_NET_LAN; localhost-only is the default. */
    int want_lan = !privacy_listeners_localhost_only();
    if (!privacy_net_listen_allowed(want_lan))
        return -1;
    int existing = tcp_find_listener(port);
    if (existing >= 0)
        return existing;
    for (int i = 0; i < NET_LISTEN_MAX; i++) {
        if (!listens[i].used) {
            listens[i].used = 1;
            listens[i].port = port;
            return i;
        }
    }
    return -1;
}

void net_tcp_unlisten(uint16_t port) {
    for (int i = 0; i < NET_LISTEN_MAX; i++) {
        if (listens[i].used && listens[i].port == port)
            listens[i].used = 0;
    }
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].is_server && tcps[i].local_port == port &&
            tcps[i].state != TCP_CLOSED) {
            tcps[i].state = TCP_CLOSED;
            tcps[i].rx_len = 0;
        }
    }
}

int net_tcp_listening(uint16_t port) {
    return tcp_find_listener(port) >= 0;
}

int net_tcp_accept(int listen_id) {
    if (listen_id < 0 || listen_id >= NET_LISTEN_MAX || !listens[listen_id].used)
        return -1;
    uint16_t port = listens[listen_id].port;
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].is_server && !tcps[i].accepted &&
            tcps[i].local_port == port &&
            (tcps[i].state == TCP_ESTABLISHED || tcps[i].state == TCP_CLOSE_WAIT)) {
            tcps[i].accepted = 1;
            return i;
        }
    }
    return -1;
}

int net_tcp_fd_send(int fd, const void *data, size_t len) {
    if (fd < 0 || fd >= NET_TCP_MAX)
        return -1;
    if (tcps[fd].state != TCP_ESTABLISHED)
        return -1;
    const uint8_t *p = data;
    while (len) {
        uint16_t chunk = len > TCP_MSS ? TCP_MSS : (uint16_t)len;
        if (tcp_send_seg_slot(fd, TCP_PSH | TCP_ACK, p, chunk) != 0)
            return -1;
        p += chunk;
        len -= chunk;
        net_poll();
    }
    return 0;
}

int net_tcp_fd_recv(int fd, void *buf, size_t cap, size_t *out_len,
                    uint32_t timeout_ticks) {
    if (out_len)
        *out_len = 0;
    if (fd < 0 || fd >= NET_TCP_MAX)
        return -1;
    uint64_t start = timer_ticks();
    while (tcps[fd].rx_len == 0) {
        net_poll();
        if (tcps[fd].got_fin && tcps[fd].rx_len == 0)
            return -1;
        if (tcps[fd].state == TCP_CLOSED)
            return -1;
        if (timer_ticks() - start > timeout_ticks)
            return -1;
        hlt_if_enabled();
    }
    size_t n = tcps[fd].rx_len < cap ? tcps[fd].rx_len : cap;
    memcpy(buf, tcps[fd].rx, n);
    memmove(tcps[fd].rx, tcps[fd].rx + n, tcps[fd].rx_len - n);
    tcps[fd].rx_len -= n;
    if (out_len)
        *out_len = n;
    return 0;
}

void net_tcp_fd_close(int fd) {
    if (fd < 0 || fd >= NET_TCP_MAX)
        return;
    if (tcps[fd].state == TCP_ESTABLISHED || tcps[fd].state == TCP_CLOSE_WAIT) {
        tcp_send_seg_slot(fd, TCP_FIN | TCP_ACK, NULL, 0);
        for (int i = 0; i < 10; i++) {
            net_poll();
            for (volatile int j = 0; j < 2000; j++)
                ;
        }
    }
    tcps[fd].state = TCP_CLOSED;
    tcps[fd].rx_len = 0;
    tcps[fd].is_server = 0;
    tcps[fd].accepted = 0;
}

static int udp_send(uint32_t dst_ip, uint16_t sport, uint16_t dport,
                    const void *data, uint16_t dlen);

static int dhcp_send_msg(uint8_t msg_type, uint32_t req_ip, uint32_t server_id) {
    uint8_t dhcp[300];
    memset(dhcp, 0, sizeof(dhcp));
    dhcp[0] = 1; /* BOOTREQUEST */
    dhcp[1] = 1; /* Ethernet */
    dhcp[2] = 6;
    dhcp[4] = (uint8_t)(dhcp_xid >> 24);
    dhcp[5] = (uint8_t)(dhcp_xid >> 16);
    dhcp[6] = (uint8_t)(dhcp_xid >> 8);
    dhcp[7] = (uint8_t)(dhcp_xid);
    if (msg_type == 3) /* REQUEST may set ciaddr if renewing; leave 0 for init */
        ;
    for (int i = 0; i < 6; i++)
        dhcp[28 + i] = local_mac[i];
    dhcp[236] = 99;
    dhcp[237] = 130;
    dhcp[238] = 83;
    dhcp[239] = 99;
    size_t o = 240;
    dhcp[o++] = 53;
    dhcp[o++] = 1;
    dhcp[o++] = msg_type;
    if (msg_type == 3 && req_ip) {
        dhcp[o++] = 50;
        dhcp[o++] = 4;
        dhcp[o++] = (uint8_t)(req_ip >> 24);
        dhcp[o++] = (uint8_t)(req_ip >> 16);
        dhcp[o++] = (uint8_t)(req_ip >> 8);
        dhcp[o++] = (uint8_t)(req_ip);
    }
    if (server_id) {
        dhcp[o++] = 54;
        dhcp[o++] = 4;
        dhcp[o++] = (uint8_t)(server_id >> 24);
        dhcp[o++] = (uint8_t)(server_id >> 16);
        dhcp[o++] = (uint8_t)(server_id >> 8);
        dhcp[o++] = (uint8_t)(server_id);
    }
    /* Parameter request list */
    dhcp[o++] = 55;
    dhcp[o++] = 4;
    dhcp[o++] = 1;
    dhcp[o++] = 3;
    dhcp[o++] = 6;
    dhcp[o++] = 15;
    dhcp[o++] = 255;
    return udp_send(0xFFFFFFFFu, 68, 67, dhcp, (uint16_t)o);
}

static void apply_static_fallback(const char *why) {
    local_ip = boot_net.ip ? boot_net.ip : NET_IP_DEFAULT;
    local_mask = boot_net.mask ? boot_net.mask : NET_MASK_DEFAULT;
    local_gw = boot_net.gw ? boot_net.gw : NET_GW_DEFAULT;
    local_dns = boot_net.dns ? boot_net.dns : NET_DNS_DEFAULT;
    addr_mode = why;
}

int net_dhcp_try(uint32_t timeout_ticks) {
    if (boot_net.mode == PEAK_NET_STATIC) {
        apply_static_fallback("static");
        return 0;
    }
    if (!timeout_ticks)
        timeout_ticks = boot_net.dhcp_timeout_ticks
                            ? boot_net.dhcp_timeout_ticks
                            : 300;

    dhcp_active = 1;
    dhcp_have_offer = 0;
    dhcp_have_ack = 0;
    dhcp_have_nak = 0;
    memset(&dhcp_pending, 0, sizeof(dhcp_pending));
    dhcp_xid_hi++;
    dhcp_xid = ((uint32_t)dhcp_xid_hi << 16) | (uint32_t)(timer_ticks() & 0xFFFF);
    local_ip = 0;

    if (dhcp_send_msg(1 /* DISCOVER */, 0, 0) != 0) {
        dhcp_active = 0;
        if (boot_net.mode == PEAK_NET_DHCP_ONLY)
            return -1;
        apply_static_fallback("fallback");
        serial_write_str("net: DHCP discover send failed; using fallback\n");
        return 0;
    }

    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (dhcp_have_offer)
            break;
        hlt_if_enabled();
    }
    if (!dhcp_have_offer) {
        dhcp_active = 0;
        if (boot_net.mode == PEAK_NET_DHCP_ONLY)
            return -1;
        apply_static_fallback("fallback");
        serial_write_str("net: DHCP timeout; using fallback\n");
        return 0;
    }

    uint32_t yi = dhcp_pending.yiaddr;
    uint32_t sid = dhcp_pending.server_id;
    dhcp_have_ack = 0;
    dhcp_have_nak = 0;
    if (dhcp_send_msg(3 /* REQUEST */, yi, sid) != 0) {
        dhcp_active = 0;
        if (boot_net.mode == PEAK_NET_DHCP_ONLY)
            return -1;
        apply_static_fallback("fallback");
        return 0;
    }
    start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (dhcp_have_ack || dhcp_have_nak)
            break;
        hlt_if_enabled();
    }
    dhcp_active = 0;
    if (!dhcp_have_ack) {
        if (boot_net.mode == PEAK_NET_DHCP_ONLY)
            return -1;
        apply_static_fallback("fallback");
        serial_write_str("net: DHCP ACK missing; using fallback\n");
        return 0;
    }

    local_ip = dhcp_pending.yiaddr ? dhcp_pending.yiaddr : yi;
    if (dhcp_pending.mask)
        local_mask = dhcp_pending.mask;
    else if (boot_net.mask)
        local_mask = boot_net.mask;
    else
        local_mask = NET_MASK_DEFAULT;
    if (dhcp_pending.gw)
        local_gw = dhcp_pending.gw;
    else if (boot_net.gw)
        local_gw = boot_net.gw;
    if (dhcp_pending.dns)
        local_dns = dhcp_pending.dns;
    else if (boot_net.dns)
        local_dns = boot_net.dns;
    addr_mode = "dhcp";
    return 0;
}

int net_tcp_send(const void *data, size_t len) {
    if (tcp_state != TCP_ESTABLISHED)
        return -1;
    const uint8_t *p = data;
    while (len) {
        uint16_t chunk = len > TCP_MSS ? TCP_MSS : (uint16_t)len;
        if (tcp_send_seg(TCP_PSH | TCP_ACK, p, chunk) != 0)
            return -1;
        p += chunk;
        len -= chunk;
        net_poll();
    }
    return 0;
}

int net_tcp_recv(void *buf, size_t cap, size_t *out_len, uint32_t timeout_ticks) {
    if (out_len)
        *out_len = 0;
    uint64_t start = timer_ticks();
    while (tcp_rx_len == 0) {
        net_poll();
        if (tcp_got_fin && tcp_rx_len == 0)
            return -1;
        if (tcp_state == TCP_CLOSED)
            return -1;
        if (timer_ticks() - start > timeout_ticks)
            return -1;
        hlt_if_enabled();
    }
    size_t n = tcp_rx_len < cap ? tcp_rx_len : cap;
    memcpy(buf, tcp_rx, n);
    memmove(tcp_rx, tcp_rx + n, tcp_rx_len - n);
    tcp_rx_len -= n;
    if (out_len)
        *out_len = n;
    return 0;
}

void net_tcp_close(void) {
    if (tcp_state == TCP_ESTABLISHED || tcp_state == TCP_CLOSE_WAIT) {
        tcp_send_seg(TCP_FIN | TCP_ACK, NULL, 0);
        for (int i = 0; i < 20; i++) {
            net_poll();
            for (volatile int j = 0; j < 5000; j++)
                ;
        }
    }
    tcp_state = TCP_CLOSED;
    tcp_rx_len = 0;
}

static int parse_url(const char *url, int *https, char *host, size_t host_cap,
                     uint16_t *port, char *path, size_t path_cap) {
    const char *p = url;
    *https = 0;
    *port = 80;
    if (!strncmp(p, "https://", 8)) {
        *https = 1;
        *port = 443;
        p += 8;
    } else if (!strncmp(p, "http://", 7)) {
        p += 7;
    }
    size_t hi = 0;
    while (*p && *p != '/' && *p != ':' && hi + 1 < host_cap)
        host[hi++] = *p++;
    host[hi] = '\0';
    if (*p == ':') {
        p++;
        uint16_t po = 0;
        while (*p >= '0' && *p <= '9') {
            po = (uint16_t)(po * 10 + (*p - '0'));
            p++;
        }
        *port = po;
    }
    if (*p == '/')
        snprintf(path, path_cap, "%s", p);
    else
        snprintf(path, path_cap, "/");
    return host[0] ? 0 : -1;
}

static int header_value(const char *raw, const char *key, char *out, size_t out_cap) {
    size_t klen = strlen(key);
    const char *p = raw;
    while (*p && !(*p == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n')) {
        /* start of line */
        int match = 1;
        for (size_t i = 0; i < klen; i++) {
            char a = p[i], b = key[i];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b) {
                match = 0;
                break;
            }
        }
        if (match && p[klen] == ':') {
            p += klen + 1;
            while (*p == ' ' || *p == '\t')
                p++;
            size_t o = 0;
            while (*p && *p != '\r' && *p != '\n' && o + 1 < out_cap)
                out[o++] = *p++;
            out[o] = '\0';
            return 0;
        }
        while (*p && !(*p == '\r' && p[1] == '\n'))
            p++;
        if (*p == '\r' && p[1] == '\n')
            p += 2;
        else
            break;
    }
    return -1;
}

static void copy_response_headers(char *buf, char *hdr_out, size_t hdr_cap) {
    if (!hdr_out || hdr_cap < 2)
        return;
    hdr_out[0] = '\0';
    if (!buf)
        return;
    size_t total = strlen(buf);
    for (size_t i = 0; i + 3 < total; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
            buf[i + 3] == '\n') {
            size_t hlen = i + 4;
            if (hlen >= hdr_cap)
                hlen = hdr_cap - 1;
            memcpy(hdr_out, buf, hlen);
            hdr_out[hlen] = '\0';
            return;
        }
    }
}

static void strip_http_headers(char *body) {
    size_t total = strlen(body);
    for (size_t i = 0; i + 3 < total; i++) {
        if (body[i] == '\r' && body[i + 1] == '\n' && body[i + 2] == '\r' &&
            body[i + 3] == '\n') {
            char *hdr_end = body + i + 4;
            size_t blen = strlen(hdr_end);
            memmove(body, hdr_end, blen + 1);
            return;
        }
    }
}

static int http_needs_tls_flag;

int net_http_needs_tls(void) {
    return http_needs_tls_flag;
}

static void tls_fail_page(char *body, size_t body_cap, const char *host, const char *why) {
    snprintf(body, body_cap,
             "<html><head><title>TLS failed</title>"
             "<style>body{background:#0B1A12;color:#E8F0EA}h1{color:#C45C5C}"
             "code{color:#9AC4AE}</style></head><body>"
             "<h1>TLS handshake failed</h1>"
             "<p>Host: <code>%s</code></p>"
             "<p>%s</p>"
             "<p>Peak TLS 1.2: ECDHE + AES-128-GCM or ChaCha20-Poly1305 (X25519).</p>"
             "</body></html>",
             host, why ? why : "unknown error");
}

static int build_http_request(char *req, size_t req_cap, const char *method,
                              const char *path, const char *host,
                              const char *extra_headers, const char *body,
                              size_t body_len, int tls) {
    size_t off = 0;
    const char *m = method && method[0] ? method : "GET";
    off = (size_t)snprintf(req, req_cap, "%s %s HTTP/1.0\r\nHost: %s\r\n", m, path, host);
    if (tls)
        off += (size_t)snprintf(req + off, req_cap - off,
                                "User-Agent: PeakBrowser/1\r\n");
    else
        off += (size_t)snprintf(req + off, req_cap - off,
                                "User-Agent: PeakBrowser/1\r\n");
    off += (size_t)snprintf(req + off, req_cap - off,
                            "Accept: text/html,application/xhtml+xml,*/*;q=0.8\r\n"
                            "Accept-Encoding: identity\r\n"
                            "Connection: close\r\n");
    if (extra_headers && extra_headers[0]) {
        off += (size_t)snprintf(req + off, req_cap - off, "%s", extra_headers);
        if (off >= 2 && req[off - 1] != '\n')
            off += (size_t)snprintf(req + off, req_cap - off, "\r\n");
    }
    if (body && body_len > 0) {
        off += (size_t)snprintf(req + off, req_cap - off, "Content-Length: %zu\r\n",
                                body_len);
    }
    if (off + 4 >= req_cap)
        return -1;
    memcpy(req + off, "\r\n", 2);
    off += 2;
    if (body && body_len > 0) {
        if (off + body_len >= req_cap)
            return -1;
        memcpy(req + off, body, body_len);
        off += body_len;
    }
    req[off] = '\0';
    return 0;
}

static int parse_http_status(const char *buf, int *status_out) {
    int st = 0;
    if (!strncmp(buf, "HTTP/", 5)) {
        const char *s = buf;
        while (*s && *s != ' ')
            s++;
        while (*s == ' ')
            s++;
        while (*s >= '0' && *s <= '9') {
            st = st * 10 + (*s - '0');
            s++;
        }
    }
    if (status_out)
        *status_out = st;
    return st;
}

static int recv_http_response_tls(char *buf, size_t buf_cap) {
    size_t total = 0;
    uint64_t last_progress = timer_ticks();
    while (total + 1 < buf_cap) {
        size_t n = 0;
        if (tls_recv(buf + total, buf_cap - 1 - total, &n, 100) != 0) {
            /* Stream done (close_notify/alert or TCP torn down) — not a stall. */
            if (!tls_ready())
                break;
            if (tcp_got_fin || tcp_state == TCP_CLOSED || tcp_state == TCP_CLOSE_WAIT)
                break;
            /* Mid-transfer stall (retransmits, slow origin): keep waiting up
             * to 12 s without progress instead of truncating the page. */
            if (timer_ticks() - last_progress > 1200)
                break;
            continue;
        }
        total += n;
        last_progress = timer_ticks();
    }
    buf[total] = '\0';
    return total > 0 ? 0 : -4;
}

static int recv_http_response_tcp(char *buf, size_t buf_cap) {
    size_t total = 0;
    uint64_t start = timer_ticks();
    while (total + 1 < buf_cap && timer_ticks() - start < 800) {
        size_t n = 0;
        if (net_tcp_recv(buf + total, buf_cap - 1 - total, &n, 100) != 0) {
            if (tcp_got_fin || tcp_state == TCP_CLOSED || tcp_state == TCP_CLOSE_WAIT)
                break;
            continue;
        }
        total += n;
        start = timer_ticks();
    }
    buf[total] = '\0';
    return total > 0 ? 0 : -1;
}

/* HTTP over TLS; leaves full response in buf. */
static int https_exchange_raw(uint32_t ip, const char *host, const char *path,
                              const char *method, const char *extra_headers,
                              const char *body, size_t body_len, char *buf, size_t buf_cap,
                              int *status_out) {
    if (tls_connect(ip, 443, host, 1200) != 0)
        return -2;

    char req[2048];
    if (build_http_request(req, sizeof(req), method, path, host, extra_headers, body,
                           body_len, 1) != 0) {
        tls_close();
        return -3;
    }
    if (tls_send(req, strlen(req)) != 0) {
        tls_close();
        return -3;
    }

    int ex = recv_http_response_tls(buf, buf_cap);
    tls_close();
    if (ex != 0)
        return ex;
    parse_http_status(buf, status_out);
    return 0;
}

/* Exchange; leaves full response (headers+body) in buf. */
static int http_exchange_raw(uint32_t ip, uint16_t port, const char *host, const char *path,
                             const char *method, const char *extra_headers, const char *body,
                             size_t body_len, char *buf, size_t buf_cap, int *status_out) {
    if (net_tcp_connect(ip, port, 500) != 0)
        return -1;

    char req[2048];
    if (build_http_request(req, sizeof(req), method, path, host, extra_headers, body,
                           body_len, 0) != 0) {
        net_tcp_close();
        return -1;
    }
    if (net_tcp_send(req, strlen(req)) != 0) {
        net_tcp_close();
        return -1;
    }

    int ex = recv_http_response_tcp(buf, buf_cap);
    net_tcp_close();
    if (ex != 0)
        return ex;
    parse_http_status(buf, status_out);
    return 0;
}

static int is_redirect(int st) {
    return st == 301 || st == 302 || st == 303 || st == 307 || st == 308;
}

static void join_redirect(const char *scheme_host_prefix, const char *host, const char *cur_path,
                          const char *loc, char *out, size_t out_cap) {
    if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8)) {
        snprintf(out, out_cap, "%s", loc);
        return;
    }
    if (loc[0] == '/') {
        snprintf(out, out_cap, "%s%s%s", scheme_host_prefix, host, loc);
        return;
    }
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", cur_path);
    char *slash = NULL;
    for (char *p = dir; *p; p++)
        if (*p == '/')
            slash = p;
    if (slash)
        slash[1] = '\0';
    else
        snprintf(dir, sizeof(dir), "/");
    snprintf(out, out_cap, "%s%s%s%s", scheme_host_prefix, host, dir, loc);
}

int net_http_request(const struct net_http_request *req, char *body, size_t body_cap,
                     int *status_out, char *hdr_out, size_t hdr_cap) {
    attempt_stats.http++;
    if (!net_ready() || !req || !req->url || !body || !body_cap)
        return -1;
    if (!privacy_net_client_allowed())
        return -1;

    const char *method = req->method[0] ? req->method : "GET";
    if (strcmp(method, "GET") && strcmp(method, "POST"))
        return -1;

    http_needs_tls_flag = 0;
    char cur[320];
    snprintf(cur, sizeof(cur), "%s", req->url);

    for (int hop = 0; hop < 5; hop++) {
        int https = 0;
        char host[128], path[256];
        uint16_t port = 80;
        if (parse_url(cur, &https, host, sizeof(host), &port, path, sizeof(path)) != 0)
            return -1;

        uint32_t ip = net_dns_resolve(host, 300);
        if (!ip) {
            if (status_out)
                *status_out = 0;
            snprintf(body, body_cap,
                     "<html><body><h1>DNS failed</h1><p>Could not resolve %s</p></body></html>",
                     host);
            return -1;
        }

        int st = 0;
        int ex;
        const char *send_body = req->body;
        size_t send_len = req->body_len;
        if (hop > 0) {
            send_body = NULL;
            send_len = 0;
        }
        if (https || port == 443) {
            http_needs_tls_flag = 1;
            ex = https_exchange_raw(ip, host, path, method, req->headers, send_body,
                                    send_len, body, body_cap, &st);
            if (ex != 0) {
                if (status_out)
                    *status_out = 0;
                const char *why = "Handshake or empty response";
                if (ex == -2)
                    why = tls_last_error();
                else if (ex == -3)
                    why = "TLS connected but request send failed";
                else if (ex == -4)
                    why = "TLS connected but empty HTTP response";
                tls_fail_page(body, body_cap, host, why);
                return -1;
            }
        } else {
            ex = http_exchange_raw(ip, port, host, path, method, req->headers, send_body,
                                   send_len, body, body_cap, &st);
            if (ex != 0) {
                if (status_out)
                    *status_out = 0;
                return -1;
            }
        }
        if (status_out)
            *status_out = st;

        if (is_redirect(st)) {
            char loc[280];
            if (header_value(body, "location", loc, sizeof(loc)) != 0) {
                copy_response_headers(body, hdr_out, hdr_cap);
                strip_http_headers(body);
                return -1;
            }
            const char *pref = (https || port == 443) ? "https://" : "http://";
            char next[320];
            join_redirect(pref, host, path, loc, next, sizeof(next));
            snprintf(cur, sizeof(cur), "%s", next);
            continue;
        }

        copy_response_headers(body, hdr_out, hdr_cap);

        if (st >= 200 && st < 300) {
            strip_http_headers(body);
            http_needs_tls_flag = 0;
            return 0;
        }

        strip_http_headers(body);
        if (!body[0]) {
            snprintf(body, body_cap,
                     "<html><body><h1>HTTP %d</h1><p>Empty response from %s</p></body></html>",
                     st, host);
        }
        return -1;
    }

    if (status_out)
        *status_out = 0;
    snprintf(body, body_cap, "<html><body><h1>Too many redirects</h1></body></html>");
    return -1;
}

int net_http_get(const char *url, char *body, size_t body_cap, int *status_out) {
    struct net_http_request req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    req.url = url;
    return net_http_request(&req, body, body_cap, status_out, NULL, 0);
}
