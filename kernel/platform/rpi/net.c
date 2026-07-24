#include "rpi.h"
#include "serial.h"

void rpi_net_init(void) {
    struct rpi_plat *p = rpi_get();
    /* Staged stubs must not be installed into netdev until they can become ready. */
    switch (p->soc) {
    case RPI_SOC_BCM2837:
        serial_log(SERIAL_LOG_DEBUG, "rpi: net usb-lan stub (not ready)\n");
        break;
    case RPI_SOC_BCM2711:
        serial_log(SERIAL_LOG_DEBUG, "rpi: net genet stub (not ready)\n");
        break;
    case RPI_SOC_BCM2712:
        serial_log(SERIAL_LOG_DEBUG, "rpi: net rp1-gem stub (not ready)\n");
        break;
    default:
        serial_log(SERIAL_LOG_DEBUG, "rpi: no netdev for soc\n");
        break;
    }
}
