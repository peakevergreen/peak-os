#include "rpi.h"
#include "serial.h"

/* BCM2711 PCIe root + VL805; BCM2712 PCIe → RP1.
 * Staged stub: RC bring-up / BAR enumeration not implemented. Never claims ready. */

static uint64_t xhci_bar;
static int pcie_ok;

int rpi_pcie_init(void) {
    struct rpi_plat *p = rpi_get();
    xhci_bar = 0;
    pcie_ok = 0;
    if (p->soc == RPI_SOC_BCM2711) {
        serial_write_str("pcie: stub (not ready; VL805/RC deferred)\n");
        return -1;
    }
    if (p->soc == RPI_SOC_BCM2712) {
        serial_write_str("pcie: stub (not ready; RP1 deferred)\n");
        return -1;
    }
    return -1;
}

uint64_t rpi_pcie_xhci_bar(void) {
    return xhci_bar;
}

int rpi_pcie_ok(void) {
    return pcie_ok;
}
