#include "rpi.h"
#include "usb.h"
#include "serial.h"

extern int dwc2_init(void);
extern void dwc2_poll(void);
extern int xhci_init(uint64_t mmio_base);
extern void xhci_poll(void);

static int usb_host_ready;

void rpi_usb_init(void) {
    usb_init();
    usb_host_ready = 0;
    struct rpi_plat *p = rpi_get();
    if (p->soc == RPI_SOC_BCM2837) {
        /* DWC2 host + HID — critical path for Pi 3 keyboard/mouse. */
        if (dwc2_init() == 0)
            usb_host_ready = 1;
    } else if (p->soc == RPI_SOC_BCM2711 || p->soc == RPI_SOC_BCM2712) {
        extern int rpi_pcie_init(void);
        extern uint64_t rpi_pcie_xhci_bar(void);
        if (rpi_pcie_init() == 0 && xhci_init(rpi_pcie_xhci_bar()) == 0)
            usb_host_ready = 1;
        else
            serial_write_str("rpi: PCIe/xHCI deferred\n");
    }
}

void rpi_usb_poll(void) {
    if (!usb_host_ready)
        return;
    usb_poll();
    struct rpi_plat *p = rpi_get();
    if (p->soc == RPI_SOC_BCM2837)
        dwc2_poll();
    else
        xhci_poll();
}
