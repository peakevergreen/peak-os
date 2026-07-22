#ifndef PEAK_RPI_H
#define PEAK_RPI_H

#include "types.h"
#include "peak_boot.h"

enum rpi_soc {
    RPI_SOC_BCM2837 = 3,
    RPI_SOC_BCM2711 = 4,
    RPI_SOC_BCM2712 = 5,
    RPI_SOC_UNKNOWN = 0,
};

struct rpi_plat {
    enum rpi_soc soc;
    uint64_t peri_base;
    uint64_t local_base;
    uint64_t uart_base;
    uint64_t gpio_base;
    uint64_t mbox_base;
    uint64_t sdhci_base;
    uint64_t usb_base;
    const void *fdt;
};

struct rpi_plat *rpi_get(void);
enum rpi_soc rpi_detect_soc(const void *fdt);
uint64_t platform_peri_base(void);

void platform_early_uart(void);
void platform_early_uart_putc(char c);
void platform_uart_init(void);
void platform_uart_putc(char c);
void platform_uart_set_base(uint64_t base);

int  platform_mailbox_fb(struct peak_framebuffer_info *fb);
int  platform_mailbox_fb_set_virt(uint32_t w, uint32_t h);
int  platform_mailbox_fb_set_offset(uint32_t x, uint32_t y);
int  platform_mailbox_fb_get_pitch(uint32_t *pitch);
void platform_timer_enable(void);
int  platform_timer_irq_pending(void);
void platform_irq_eoi(void);

void rpi_gpio_init(void);
void rpi_gpio_set_func(unsigned pin, unsigned fn);
void rpi_gpio_set_output(unsigned pin);
void rpi_gpio_write(unsigned pin, int val);
int  rpi_gpio_read(unsigned pin);

void rpi_sdhci_init(void);
void rpi_usb_init(void);
void rpi_usb_poll(void);
void rpi_net_init(void);
void rpi_wifi_init(void);
int  rpi_wifi_ready(void);
int  rpi_wifi_present(void);
void rpi_sound_init(void);
void rpi_sound_beep(uint32_t freq_hz, uint32_t ms);
void rpi_gpu_init(void);
int  rpi_gpu_soft_fb(void);
int  rpi_gpu_accel_ready(void);

#endif
