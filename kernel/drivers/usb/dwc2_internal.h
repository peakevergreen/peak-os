#ifndef PEAK_DWC2_INTERNAL_H
#define PEAK_DWC2_INTERNAL_H

#include "types.h"

#define DWC2_GAHBCFG   0x008
#define DWC2_GUSBCFG   0x00c
#define DWC2_GRSTCTL   0x010
#define DWC2_GINTSTS   0x014
#define DWC2_HCFG      0x400
#define DWC2_HFIR      0x404
#define DWC2_HPRT0     0x440
#define DWC2_HCCHAR(ch)   (0x500 + (ch) * 0x20)
#define DWC2_HCSPLT(ch)   (0x504 + (ch) * 0x20)
#define DWC2_HCINT(ch)    (0x508 + (ch) * 0x20)
#define DWC2_HCINTMSK(ch) (0x50c + (ch) * 0x20)
#define DWC2_HCTSIZ(ch)   (0x510 + (ch) * 0x20)
#define DWC2_HCDMA(ch)    (0x514 + (ch) * 0x20)

#define HCINT_XFERCOMP  (1u << 0)
#define HCINT_CHHLTD    (1u << 1)
#define HCINT_NAK       (1u << 4)
#define HCINT_ACK       (1u << 5)
#define HCINT_NYET      (1u << 6)

#define PID_DATA0 0
#define PID_DATA1 2
#define PID_SETUP 3

#define EP_CTRL 0
#define EP_INTR 3

#define USB_SPEED_HS 0
#define USB_SPEED_FS 1
#define USB_SPEED_LS 2

extern volatile uint32_t *dwc2_mmio;
extern int dwc2_ready;
extern uint8_t dwc2_dma_buf[64];
extern uint8_t dwc2_setup_pkt[8];
extern uint64_t dwc2_last_hub_poll;
extern uint8_t dwc2_next_addr;

uint32_t dwc2_read(uint32_t off);
void dwc2_write(uint32_t off, uint32_t v);
void dwc2_delay_spin(unsigned n);

int dwc2_hc_xfer(int ch, uint8_t addr, uint8_t ep, int in, int eptype,
                 uint8_t pid, void *buf, uint32_t len, uint16_t mps,
                 uint8_t speed, uint8_t hub_addr, uint8_t hub_port);

int dwc2_ctrl_xfer(uint8_t addr, uint8_t bmRequestType, uint8_t bRequest,
                   uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                   void *data, uint8_t speed, uint8_t hub_addr,
                   uint8_t hub_port, uint16_t mps);

void dwc2_hub_reset(void);
void dwc2_hub_poll_ports(void);
int dwc2_hub_enum_device(uint8_t parent_hub, uint8_t parent_port, uint8_t speed,
                         uint8_t *out_addr);

void dwc2_hid_reset(void);
void dwc2_hid_parse_config(uint8_t addr, uint8_t *cfg, int len, uint8_t speed,
                           uint8_t hub_addr, uint8_t hub_port);
void dwc2_hid_clear_for_addr(uint8_t addr);
void dwc2_hid_poll(void);

/* SMSC USB LAN bind hooks (kernel/drivers/net/usb_lan.c) */
void usb_lan_try_bind(uint8_t addr, uint16_t vid, uint16_t pid, uint8_t *cfg, int len,
                      uint8_t speed, uint8_t hub_addr, uint8_t hub_port);
void usb_lan_clear_for_addr(uint8_t addr);

#endif
