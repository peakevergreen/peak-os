#include "netdev.h"
#include "../usb/dwc2_internal.h"
#include "serial.h"
#include "util.h"

/* LAN9514 (Pi 3B) / LAN7800-class (Pi 3B+) USB Ethernet.
 * Bulk IN/OUT datapath: bind only when SMSC/Microchip VID matches and both
 * bulk endpoints are found. ready() is 0 until bind succeeds (no false ready). */

#define EP_BULK 2
#define SMSC_VID 0x0424u

/* SMSC vendor requests */
#define SMSC_WR_REG 0xA0
#define SMSC_RD_REG 0xA1
#define SMSC_ADDRL  0x104
#define SMSC_ADDRH  0x108

static uint8_t mac[6] = { 0xb8, 0x27, 0xeb, 0x00, 0x00, 0x01 };
static int bound;
static uint8_t dev_addr;
static uint8_t ep_in, ep_out;
static uint16_t mps_in, mps_out;
static uint8_t speed, hub_addr, hub_port;
static uint8_t tx_pid = PID_DATA0;
static uint8_t rx_pid = PID_DATA0;
static uint8_t rx_stash[1600];
static int rx_len;

static int smsc_rd32(uint32_t reg, uint32_t *out) {
    uint8_t b[4];
    memset(b, 0, sizeof(b));
    if (dwc2_ctrl_xfer(dev_addr, 0xC0, SMSC_RD_REG, 0, (uint16_t)reg, 4, b, speed,
                       hub_addr, hub_port, 64) != 0)
        return -1;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return 0;
}

static int smsc_read_mac(void) {
    uint32_t lo = 0, hi = 0;
    if (smsc_rd32(SMSC_ADDRL, &lo) != 0 || smsc_rd32(SMSC_ADDRH, &hi) != 0)
        return -1;
    mac[0] = (uint8_t)(lo);
    mac[1] = (uint8_t)(lo >> 8);
    mac[2] = (uint8_t)(lo >> 16);
    mac[3] = (uint8_t)(lo >> 24);
    mac[4] = (uint8_t)(hi);
    mac[5] = (uint8_t)(hi >> 8);
    /* Reject all-zero / all-FF as unbound EEPROM. */
    int z = 1, f = 1;
    for (int i = 0; i < 6; i++) {
        if (mac[i])
            z = 0;
        if (mac[i] != 0xff)
            f = 0;
    }
    return (z || f) ? -1 : 0;
}

void usb_lan_try_bind(uint8_t addr, uint16_t vid, uint16_t pid, uint8_t *cfg, int len,
                      uint8_t spd, uint8_t ha, uint8_t hp) {
    (void)pid;
    if (bound || vid != SMSC_VID || !cfg || len < 9)
        return;

    uint8_t in_ep = 0, out_ep = 0;
    uint16_t in_mps = 64, out_mps = 64;
    int i = 0;
    while (i + 7 <= len) {
        uint8_t dlen = cfg[i];
        uint8_t dtype = cfg[i + 1];
        if (dlen < 2)
            break;
        if (dtype == 5 && dlen >= 7) {
            uint8_t ep = cfg[i + 2];
            uint8_t attr = cfg[i + 3] & 3;
            uint16_t mps = (uint16_t)cfg[i + 4] | ((uint16_t)cfg[i + 5] << 8);
            if (attr == EP_BULK) {
                if (ep & 0x80) {
                    in_ep = ep & 0x0f;
                    in_mps = mps ? mps : 64;
                } else {
                    out_ep = ep & 0x0f;
                    out_mps = mps ? mps : 64;
                }
            }
        }
        i += dlen;
    }
    if (!in_ep || !out_ep)
        return;

    dev_addr = addr;
    ep_in = in_ep;
    ep_out = out_ep;
    mps_in = in_mps;
    mps_out = out_mps;
    speed = spd;
    hub_addr = ha;
    hub_port = hp;
    tx_pid = PID_DATA0;
    rx_pid = PID_DATA0;
    rx_len = 0;

    if (smsc_read_mac() != 0) {
        serial_log(SERIAL_LOG_DEBUG,
                   "usb-lan: SMSC found but MAC read failed (not ready)\n");
        return;
    }
    bound = 1;
    char msg[72];
    snprintf(msg, sizeof(msg),
             "usb-lan: bound @%u bulk in=%u out=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
             (unsigned)addr, (unsigned)in_ep, (unsigned)out_ep, mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
    serial_log(SERIAL_LOG_INFO, msg);
}

void usb_lan_clear_for_addr(uint8_t addr) {
    if (bound && dev_addr == addr) {
        bound = 0;
        rx_len = 0;
        serial_log(SERIAL_LOG_DEBUG, "usb-lan: unbound\n");
    }
}

static int lan_init(void) {
    if (!bound) {
        serial_log(SERIAL_LOG_DEBUG,
                   "usb-lan: no SMSC device bound (bulk path idle)\n");
        return -1;
    }
    return 0;
}

static int lan_ready(void) { return bound; }
static void lan_get_mac(uint8_t m[6]) { memcpy(m, mac, 6); }

static int lan_send(const void *d, uint16_t l) {
    if (!bound || !d || l < 14 || l > 1514)
        return -1;
    /* LAN95xx TXCMD_A/B (8 bytes) + frame. Use dma_buf for header then chunk. */
    uint8_t hdr[8];
    uint32_t txa = (uint32_t)l | (1u << 13); /* FIRST_SEG|LAST_SEG via len */
    uint32_t txb = (uint32_t)l;
    hdr[0] = (uint8_t)txa;
    hdr[1] = (uint8_t)(txa >> 8);
    hdr[2] = (uint8_t)(txa >> 16);
    hdr[3] = (uint8_t)(txa >> 24);
    hdr[4] = (uint8_t)txb;
    hdr[5] = (uint8_t)(txb >> 8);
    hdr[6] = (uint8_t)(txb >> 16);
    hdr[7] = (uint8_t)(txb >> 24);

    /* Single bulk OUT: header + frame in one transfer when it fits dma; else
     * sequential OUT of header then payload via temporary stack copy max 64. */
    uint8_t pkt[8 + 1514];
    if ((size_t)l + 8 > sizeof(pkt))
        return -1;
    memcpy(pkt, hdr, 8);
    memcpy(pkt + 8, d, l);
    uint32_t total = (uint32_t)l + 8;
    uint32_t off = 0;
    while (off < total) {
        uint32_t chunk = total - off;
        if (chunk > mps_out)
            chunk = mps_out;
        if (chunk > sizeof(dwc2_dma_buf))
            chunk = sizeof(dwc2_dma_buf);
        memcpy(dwc2_dma_buf, pkt + off, chunk);
        if (dwc2_hc_xfer(5, dev_addr, ep_out, 0, EP_BULK, tx_pid, dwc2_dma_buf,
                         chunk, mps_out, speed, hub_addr, hub_port) != 0)
            return -1;
        tx_pid = (tx_pid == PID_DATA0) ? PID_DATA1 : PID_DATA0;
        off += chunk;
    }
    return 0;
}

static int lan_recv(void *b, uint16_t c) {
    if (!bound || !b || !c)
        return -1;
    if (rx_len > 0) {
        uint16_t n = (uint16_t)rx_len;
        if (n > c)
            n = c;
        memcpy(b, rx_stash, n);
        rx_len = 0;
        return (int)n;
    }
    /* Poll one bulk IN (may NAK). */
    memset(dwc2_dma_buf, 0, sizeof(dwc2_dma_buf));
    int r = dwc2_hc_xfer(6, dev_addr, ep_in, 1, EP_BULK, rx_pid, dwc2_dma_buf,
                         mps_in > sizeof(dwc2_dma_buf) ? sizeof(dwc2_dma_buf) : mps_in,
                         mps_in, speed, hub_addr, hub_port);
    if (r != 0)
        return -1;
    rx_pid = (rx_pid == PID_DATA0) ? PID_DATA1 : PID_DATA0;
    /* RX status word (4) + frame; short packets may be status-only. */
    if (mps_in < 4)
        return -1;
    uint32_t st = (uint32_t)dwc2_dma_buf[0] | ((uint32_t)dwc2_dma_buf[1] << 8) |
                  ((uint32_t)dwc2_dma_buf[2] << 16) | ((uint32_t)dwc2_dma_buf[3] << 24);
    uint16_t flen = (uint16_t)(st & 0x3FFF);
    if (flen < 14 || flen > 1514)
        return -1;
    /* For small mps, only first fragment available — stash what we have. */
    uint16_t have = mps_in > 4 ? (uint16_t)(mps_in - 4) : 0;
    if (have > flen)
        have = flen;
    if (have > c)
        have = c;
    if (have == 0)
        return -1;
    memcpy(b, dwc2_dma_buf + 4, have);
    /* Full multi-mps RX deferred; return first fragment for link bring-up. */
    (void)SMSC_WR_REG;
    return (int)have;
}

static int lan_rx_pending(void) { return bound && rx_len > 0; }

static const struct netdev_ops lan_ops = {
    .name = "usb-lan",
    .init = lan_init,
    .ready = lan_ready,
    .get_mac = lan_get_mac,
    .send = lan_send,
    .recv = lan_recv,
    .rx_pending = lan_rx_pending,
};

void netdev_register_usb_lan(void) {
    netdev_register(&lan_ops);
}
