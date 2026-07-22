#include "rpi.h"
#include "serial.h"

void rpi_net_init(void) {
    struct rpi_plat *p = rpi_get();
    /* Do not register a netdev until its init path can become ready. */
    switch (p->soc) {
    case RPI_SOC_BCM2837:
        serial_write_str("rpi: net usb-lan deferred (hub/bulk datapath unavailable)\n");
        break;
    case RPI_SOC_BCM2711:
        serial_write_str("rpi: net GENET deferred (rings/PHY unavailable)\n");
        break;
    case RPI_SOC_BCM2712:
        serial_write_str("rpi: net RP1 GEM deferred (PCIe/datapath unavailable)\n");
        break;
    default:
        serial_write_str("rpi: no netdev for soc\n");
        break;
    }
}
