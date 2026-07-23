#ifndef PEAK_NET_INTERNAL_H
#define PEAK_NET_INTERNAL_H

#include "net.h"
#include "arp_util.h"
#include "dhcp_util.h"
#include "tcp_util.h"
#include "sync.h"
#include "peak_boot.h"

/* QEMU user-networking defaults */
#define NET_IP_DEFAULT   0x0A00020F  /* 10.0.2.15 */
#define NET_MASK_DEFAULT 0xFFFFFF00  /* 255.255.255.0 */
#define NET_GW_DEFAULT   0x0A000202  /* 10.0.2.2 */
#define NET_DNS_DEFAULT  0x0A000203  /* 10.0.2.3 */

/*
 * Networking timeouts. The kernel timer is 100 Hz (timer_init in boot.c),
 * so each tick is ~10 ms. Prefer these named budgets over bare literals.
 */
#define NET_TIMER_HZ                100u
#define NET_DHCP_TIMEOUT_DEFAULT    300u  /* 3s DISCOVER / REQUEST wait */
#define NET_DNS_CACHE_TTL_TICKS     600u  /* 6s positive A-cache */
#define NET_DNS_RESOLVE_TICKS       300u  /* 3s A-query (http / ping) */
#define NET_ARP_RESOLVE_TICKS       200u  /* 2s next-hop MAC resolve */
#define NET_ARP_GW_PRIME_ITERS       50u  /* post-DHCP gateway ARP prime */
#define NET_TCP_CONNECT_HTTP_TICKS  500u  /* 5s plaintext HTTP connect */
#define NET_TCP_RECV_SLICE_TICKS    100u  /* 1s per recv poll slice */
#define NET_HTTP_IDLE_TCP_TICKS     800u  /* 8s stall without TCP progress */
#define NET_HTTP_IDLE_TLS_TICKS    1200u  /* 12s stall without TLS progress */
#define NET_TLS_HANDSHAKE_TICKS    1200u  /* 12s TLS connect / handshake */

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

extern struct peak_net_config boot_net;
extern const char *addr_mode;

extern int net_up;
extern uint8_t local_mac[6];
extern uint32_t local_ip;
extern uint32_t local_mask;
extern uint32_t local_gw;
extern uint32_t local_dns;

extern struct net_attempt_stats attempt_stats;

extern struct arp_entry arp_cache[ARP_CACHE_MAX];
extern uint32_t arp_wait_ip;
extern int arp_resolved;
extern uint8_t arp_wait_mac[6];

extern struct tcp_conn tcps[NET_TCP_MAX];
extern struct tcp_listener listens[NET_LISTEN_MAX];
extern int tcp_cur;

extern struct spinlock net_lock;
extern int net_lock_ready;

extern int dhcp_active;
extern uint16_t dhcp_xid_hi;
extern uint32_t dhcp_xid;
extern struct dhcp_lease dhcp_pending;
extern int dhcp_have_offer;
extern int dhcp_have_ack;
extern int dhcp_have_nak;

extern uint16_t ephem_port;
extern uint16_t dns_txid;
extern uint32_t dns_answer_ip;
extern int dns_done;
extern uint16_t dns_sport;

extern int http_needs_tls_flag;

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

static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8) |
           ((x & 0xFF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}
static inline uint32_t htonl(uint32_t x) { return bswap32(x); }

void net_lock_acquire(void);
void net_lock_release(void);

/* True once start..now spans at least timeout_ticks (100 Hz). */
int net_timed_out(uint64_t start, uint32_t timeout_ticks);
/* Poll the NIC once, then HLT if IRQs are enabled (shared busy-wait quantum). */
void net_poll_idle(void);

int net_eth_send(uint16_t ethertype, const uint8_t dst[6], const void *payload, uint16_t plen);
int net_ip_send(uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t plen);
int net_udp_send(uint32_t dst_ip, uint16_t sport, uint16_t dport,
                 const void *data, uint16_t dlen);

void net_arp_cache_put(uint32_t ip, const uint8_t mac[6]);
int net_arp_cache_get(uint32_t ip, uint8_t mac[6]);
int net_arp_request(uint32_t tip);
int net_resolve_next_hop_mac(uint32_t dst_ip, uint8_t mac[6], uint32_t timeout_ticks);
void net_handle_arp(const uint8_t *pkt, uint16_t len);

void net_handle_dhcp_udp(const uint8_t *udp, uint16_t ulen);
void net_apply_static_fallback(const char *why);

void net_handle_dns_udp(const uint8_t *pkt, uint16_t ulen);

void net_handle_tcp(uint32_t src, const uint8_t *pkt, uint16_t len);
int net_tcp_send_seg_slot(int slot, uint8_t flags, const void *data, uint16_t dlen);
int net_tcp_send_seg(uint8_t flags, const void *data, uint16_t dlen);
int net_tcp_find_slot(uint32_t src, uint16_t sport, uint16_t dport);
int net_tcp_find_listener(uint16_t port);
int net_tcp_alloc_slot(void);

#endif
