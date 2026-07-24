#include "rpi.h"
#include "netdev.h"
#include "serial.h"

void rpi_net_init(void) {
    struct rpi_plat *p = rpi_get();
    switch (p->soc) {
    case RPI_SOC_BCM2837:
        /* Register ops; ready() stays 0 until SMSC bulk bind succeeds. */
        netdev_register_usb_lan();
        serial_log(SERIAL_LOG_DEBUG, "rpi: net usb-lan registered (bind on enum)\n");
        break;
    case RPI_SOC_BCM2711:
        netdev_register_genet();
        serial_log(SERIAL_LOG_DEBUG, "rpi: net genet registered (Pi 4 only)\n");
        break;
    case RPI_SOC_BCM2712:
        serial_log(SERIAL_LOG_DEBUG, "rpi: net rp1-gem stub (not ready)\n");
        break;
    default:
        serial_log(SERIAL_LOG_DEBUG, "rpi: no netdev for soc\n");
        break;
    }
}
