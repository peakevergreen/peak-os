#include "rpi.h"
#include "serial.h"
#include "util.h"

/* SDIO Wi-Fi (CYW4343x / 43455). Runtime firmware is a documented binary
 * exception — loaded from the FAT boot partition when present
 * (e.g. cyfmac43455-sdio.bin / .txt next to the DTBs).
 * Association/security reuse the shared net stack above L2 once the
 * SDIO function is up. */

static int wifi_present;
void rpi_wifi_init(void) {
    struct rpi_plat *p = rpi_get();
    wifi_present = 0;
    switch (p->soc) {
    case RPI_SOC_BCM2837:
    case RPI_SOC_BCM2711:
    case RPI_SOC_BCM2712:
        serial_write_str("rpi: wifi unavailable (SDIO/firmware load not implemented)\n");
        serial_write_str("rpi: wifi firmware expected on bootfs as cyfmac*.bin\n");
        break;
    default:
        break;
    }
}

int rpi_wifi_ready(void) {
    /* Ready after SDIO enum + firmware download + MLME join. */
    return 0;
}

int rpi_wifi_present(void) {
    /* Set only after hardware discovery and successful firmware loading. */
    return wifi_present;
}
