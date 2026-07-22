#include "net_internal.h"
#include "peak_errno.h"
#include "netdev.h"
#include "timer.h"
#include "util.h"

void net_arp_cache_put(uint32_t ip, const uint8_t mac[6]) {
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

int net_arp_cache_get(uint32_t ip, uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

int net_arp_request(uint32_t tip) {
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
    return net_eth_send(ETH_ARP, bcast, pkt, 28);
}

int net_resolve_next_hop_mac(uint32_t dst_ip, uint8_t mac[6], uint32_t timeout_ticks) {
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (dst_ip == 0xFFFFFFFFu || dst_ip == (local_ip | ~local_mask)) {
        memcpy(mac, bcast, 6);
        return 0;
    }
    uint32_t tip = dst_ip;
    if (local_mask && (dst_ip & local_mask) != (local_ip & local_mask))
        tip = local_gw;
    if (net_arp_cache_get(tip, mac) == 0)
        return 0;
    net_arp_request(tip);
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        net_poll();
        if (arp_resolved && arp_wait_ip == tip) {
            memcpy(mac, arp_wait_mac, 6);
            net_arp_cache_put(tip, mac);
            return 0;
        }
        if (net_arp_cache_get(tip, mac) == 0)
            return 0;
        hlt();
    }
    return PEAK_ETIMEOUT;
}

void net_handle_arp(const uint8_t *pkt, uint16_t len) {
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
        net_eth_send(ETH_ARP, pkt + 8, reply, 28);
        net_arp_cache_put(sip, pkt + 8);
    } else if (op == ARP_REPLY) {
        net_arp_cache_put(sip, pkt + 8);
        if (sip == arp_wait_ip) {
            for (int i = 0; i < 6; i++)
                arp_wait_mac[i] = pkt[8 + i];
            arp_resolved = 1;
        }
    }
}
