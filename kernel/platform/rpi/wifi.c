#include "rpi.h"
#include "serial.h"

/* SDIO Wi-Fi (CYW4343x / 43455). Staged stub: SDIO enum + firmware load
 * (cyfmac*.bin on bootfs) + MLME are not implemented. Never claims ready. */

static int wifi_present;

void rpi_wifi_init(void) {
    struct rpi_plat *p = rpi_get();
    wifi_present = 0;
    switch (p->soc) {
    case RPI_SOC_BCM2837:
    case RPI_SOC_BCM2711:
    case RPI_SOC_BCM2712:
        serial_log(SERIAL_LOG_DEBUG,
                   "rpi: wifi stub (not ready; SDIO/firmware deferred)\n");
        break;
    default:
        break;
    }
}

int rpi_wifi_ready(void) {
    return 0;
}

int rpi_wifi_present(void) {
    return wifi_present;
}
