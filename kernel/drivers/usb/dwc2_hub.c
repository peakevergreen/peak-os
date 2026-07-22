#include "usb.h"
#include "dwc2_internal.h"
#include "serial.h"
#include "util.h"

/* DWC2 hub enumeration, split transactions, and hotplug polling. */

#define HUB_PORT_CONNECTION   0
#define HUB_PORT_ENABLE       1
#define HUB_PORT_RESET        4
#define HUB_PORT_POWER        8
#define HUB_C_PORT_CONNECTION 16
#define HUB_C_PORT_RESET      20

struct hub_state {
    int used;
    uint8_t addr;
    uint8_t nports;
    uint8_t speed;
    uint8_t status_ep;
    uint16_t status_mps;
    uint8_t port_child[8];
    uint8_t port_seen[8];
};

#define MAX_HUB 2
static struct hub_state hubs[MAX_HUB];
static int nhub;

static int hub_set_port_feature(struct hub_state *h, uint8_t port,
                                uint16_t feature) {
    return dwc2_ctrl_xfer(h->addr, 0x23, 0x03, feature, port, 0, 0, h->speed, 0,
                          0, 64);
}

static int hub_clear_port_feature(struct hub_state *h, uint8_t port,
                                  uint16_t feature) {
    return dwc2_ctrl_xfer(h->addr, 0x23, 0x01, feature, port, 0, 0, h->speed, 0,
                          0, 64);
}

static int hub_get_port_status(struct hub_state *h, uint8_t port,
                               uint32_t *status) {
    uint8_t st[4];
    memset(st, 0, sizeof(st));
    if (dwc2_ctrl_xfer(h->addr, 0xA3, 0x00, 0, port, 4, st, h->speed, 0, 0, 64) !=
        0)
        return -1;
    *status = (uint32_t)st[0] | ((uint32_t)st[1] << 8) | ((uint32_t)st[2] << 16) |
              ((uint32_t)st[3] << 24);
    return 0;
}

static int register_hub(uint8_t addr, uint8_t speed, uint8_t *cfg, int cfg_len) {
    if (nhub >= MAX_HUB)
        return -1;
    uint8_t hubdesc[16];
    memset(hubdesc, 0, sizeof(hubdesc));
    if (dwc2_ctrl_xfer(addr, 0xA0, 0x06, 0x2900, 0, 9, hubdesc, speed, 0, 0, 64) !=
        0)
        return -1;
    uint8_t nports = hubdesc[2];
    if (nports == 0 || nports > 8)
        nports = hubdesc[2] ? hubdesc[2] : 4;
    if (nports > 8)
        nports = 8;

    struct hub_state *h = &hubs[nhub++];
    memset(h, 0, sizeof(*h));
    h->used = 1;
    h->addr = addr;
    h->nports = nports;
    h->speed = speed;

    int i = 0;
    while (i + 2 <= cfg_len) {
        uint8_t dlen = cfg[i];
        uint8_t dtype = cfg[i + 1];
        if (dlen < 2 || i + dlen > cfg_len)
            break;
        if (dtype == 5 && dlen >= 7) {
            uint8_t ep = cfg[i + 2];
            uint8_t attr = cfg[i + 3];
            if ((ep & 0x80) && (attr & 3) == 3) {
                h->status_ep = ep & 0x0f;
                h->status_mps =
                    (uint16_t)cfg[i + 4] | ((uint16_t)cfg[i + 5] << 8);
                break;
            }
        }
        i += dlen;
    }

    char hub_msg[48];
    snprintf(hub_msg, sizeof(hub_msg), "usb: hub @%u ports=%u\n",
             (unsigned)addr, (unsigned)nports);
    serial_log(SERIAL_LOG_DEBUG, hub_msg);

    for (uint8_t p = 1; p <= nports; p++) {
        (void)hub_set_port_feature(h, p, HUB_PORT_POWER);
        dwc2_delay_spin(100000);
        uint32_t st = 0;
        if (hub_get_port_status(h, p, &st) != 0)
            continue;
        if (st & (1u << HUB_PORT_CONNECTION)) {
            (void)hub_clear_port_feature(h, p, HUB_C_PORT_CONNECTION);
            (void)hub_set_port_feature(h, p, HUB_PORT_RESET);
            dwc2_delay_spin(200000);
            (void)hub_clear_port_feature(h, p, HUB_C_PORT_RESET);
            dwc2_delay_spin(50000);
            if (hub_get_port_status(h, p, &st) != 0)
                continue;
            uint8_t child_speed = USB_SPEED_FS;
            if (st & (1u << 10))
                child_speed = USB_SPEED_LS;
            else if (st & (1u << 9))
                child_speed = USB_SPEED_HS;
            uint8_t child = 0;
            if (dwc2_hub_enum_device(addr, p, child_speed, &child) == 0) {
                h->port_child[p - 1] = child;
                h->port_seen[p - 1] = 1;
            }
        }
    }
    return 0;
}

int dwc2_hub_enum_device(uint8_t parent_hub, uint8_t parent_port, uint8_t speed,
                         uint8_t *out_addr) {
    uint8_t desc[18];
    memset(desc, 0, sizeof(desc));
    uint16_t mps0 = 8;
    if (dwc2_ctrl_xfer(0, 0x80, 0x06, 0x0100, 0, 8, desc, speed, parent_hub,
                       parent_port, mps0) != 0)
        return -1;
    if (desc[7])
        mps0 = desc[7];
    uint8_t addr = dwc2_next_addr++;
    if (dwc2_next_addr == 0)
        dwc2_next_addr = 1;
    if (dwc2_ctrl_xfer(0, 0x00, 0x05, addr, 0, 0, 0, speed, parent_hub, parent_port,
                       mps0) != 0)
        return -1;
    dwc2_delay_spin(100000);
    if (dwc2_ctrl_xfer(addr, 0x80, 0x06, 0x0100, 0, 18, desc, speed, parent_hub,
                       parent_port, mps0) != 0)
        return -1;
    uint8_t cfg[256];
    memset(cfg, 0, sizeof(cfg));
    if (dwc2_ctrl_xfer(addr, 0x80, 0x06, 0x0200, 0, 9, cfg, speed, parent_hub,
                       parent_port, mps0) != 0)
        return -1;
    uint16_t total = (uint16_t)cfg[2] | ((uint16_t)cfg[3] << 8);
    if (total > sizeof(cfg))
        total = sizeof(cfg);
    if (dwc2_ctrl_xfer(addr, 0x80, 0x06, 0x0200, 0, total, cfg, speed, parent_hub,
                       parent_port, mps0) != 0)
        return -1;
    if (dwc2_ctrl_xfer(addr, 0x00, 0x09, cfg[5] ? cfg[5] : 1, 0, 0, 0, speed,
                       parent_hub, parent_port, mps0) != 0)
        return -1;

    int is_hub = (desc[4] == 0x09);
    if (!is_hub) {
        int i = 0;
        while (i + 9 <= (int)total) {
            if (cfg[i + 1] == 4 && cfg[i + 5] == 0x09) {
                is_hub = 1;
                break;
            }
            if (cfg[i] < 2)
                break;
            i += cfg[i];
        }
    }

    if (is_hub && parent_hub == 0) {
        (void)register_hub(addr, speed, cfg, (int)total);
    } else {
        dwc2_hid_parse_config(addr, cfg, (int)total, speed, parent_hub, parent_port);
    }
    if (out_addr)
        *out_addr = addr;
    return 0;
}

void dwc2_hub_poll_ports(void) {
    for (int hi = 0; hi < nhub; hi++) {
        struct hub_state *h = &hubs[hi];
        if (!h->used)
            continue;
        for (uint8_t p = 1; p <= h->nports; p++) {
            uint32_t st = 0;
            if (hub_get_port_status(h, p, &st) != 0)
                continue;
            int connected = (st & (1u << HUB_PORT_CONNECTION)) != 0;
            uint8_t child = h->port_child[p - 1];
            if (!connected && child) {
                dwc2_hid_clear_for_addr(child);
                h->port_child[p - 1] = 0;
                h->port_seen[p - 1] = 0;
                serial_log_rl(SERIAL_LOG_DEBUG, 100,
                              "usb: hub port disconnect\n");
                (void)hub_clear_port_feature(h, p, HUB_C_PORT_CONNECTION);
            } else if (connected && !child) {
                (void)hub_clear_port_feature(h, p, HUB_C_PORT_CONNECTION);
                (void)hub_set_port_feature(h, p, HUB_PORT_RESET);
                dwc2_delay_spin(200000);
                (void)hub_clear_port_feature(h, p, HUB_C_PORT_RESET);
                dwc2_delay_spin(50000);
                if (hub_get_port_status(h, p, &st) != 0)
                    continue;
                uint8_t child_speed = USB_SPEED_FS;
                if (st & (1u << 10))
                    child_speed = USB_SPEED_LS;
                else if (st & (1u << 9))
                    child_speed = USB_SPEED_HS;
                uint8_t naddr = 0;
                if (dwc2_hub_enum_device(h->addr, p, child_speed, &naddr) == 0) {
                    h->port_child[p - 1] = naddr;
                    h->port_seen[p - 1] = 1;
                    serial_log_rl(SERIAL_LOG_DEBUG, 100,
                                  "usb: hub port hotplug\n");
                }
            }
        }
    }
}

void dwc2_hub_reset(void) {
    nhub = 0;
    memset(hubs, 0, sizeof(hubs));
}
