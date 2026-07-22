#include "net_internal.h"
#include "peak_errno.h"
#include "dhcp_util.h"
#include "serial.h"
#include "timer.h"
#include "util.h"

void net_handle_dhcp_udp(const uint8_t *udp, uint16_t ulen) {
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
    return net_udp_send(0xFFFFFFFFu, 68, 67, dhcp, (uint16_t)o);
}

void net_apply_static_fallback(const char *why) {
    local_ip = boot_net.ip ? boot_net.ip : NET_IP_DEFAULT;
    local_mask = boot_net.mask ? boot_net.mask : NET_MASK_DEFAULT;
    local_gw = boot_net.gw ? boot_net.gw : NET_GW_DEFAULT;
    local_dns = boot_net.dns ? boot_net.dns : NET_DNS_DEFAULT;
    addr_mode = why;
}

int net_dhcp_try(uint32_t timeout_ticks) {
    if (boot_net.mode == PEAK_NET_STATIC) {
        net_apply_static_fallback("static");
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
            return PEAK_EDHCP;
        net_apply_static_fallback("fallback");
        serial_log_once(SERIAL_LOG_INFO, "dhcp.discover_fail",
                          "net: DHCP discover send failed; using fallback\n");
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
            return PEAK_EDHCP;
        net_apply_static_fallback("fallback");
        serial_log_once(SERIAL_LOG_INFO, "dhcp.timeout",
                          "net: DHCP timeout; using fallback\n");
        return 0;
    }

    uint32_t yi = dhcp_pending.yiaddr;
    uint32_t sid = dhcp_pending.server_id;
    dhcp_have_ack = 0;
    dhcp_have_nak = 0;
    if (dhcp_send_msg(3 /* REQUEST */, yi, sid) != 0) {
        dhcp_active = 0;
        if (boot_net.mode == PEAK_NET_DHCP_ONLY)
            return PEAK_EDHCP;
        net_apply_static_fallback("fallback");
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
            return PEAK_EDHCP;
        net_apply_static_fallback("fallback");
        serial_log_once(SERIAL_LOG_INFO, "dhcp.no_ack",
                          "net: DHCP ACK missing; using fallback\n");
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
