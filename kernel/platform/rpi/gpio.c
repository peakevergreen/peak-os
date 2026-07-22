#include "rpi.h"
#include "serial.h"

/* BCM283x / 2711 GPIO — minimal pinctrl */
#define GPFSEL0 0x00
#define GPSET0  0x1c
#define GPCLR0  0x28
#define GPLEV0  0x34

static volatile uint32_t *gpio(void) {
    return (volatile uint32_t *)(uintptr_t)rpi_get()->gpio_base;
}

void rpi_gpio_init(void) {
    if (!rpi_get()->gpio_base)
        return;
    serial_write_str("rpi: gpio ready\n");
}

void rpi_gpio_set_func(unsigned pin, unsigned fn) {
    volatile uint32_t *g = gpio();
    if (!g || pin > 53 || fn > 7)
        return;
    unsigned reg = pin / 10;
    unsigned shift = (pin % 10) * 3;
    uint32_t v = g[GPFSEL0 / 4 + reg];
    v &= ~(7u << shift);
    v |= (fn & 7u) << shift;
    g[GPFSEL0 / 4 + reg] = v;
}

void platform_gpio_set_output(unsigned pin) {
    rpi_gpio_set_func(pin, 1);
}

void rpi_gpio_set_output(unsigned pin) {
    platform_gpio_set_output(pin);
}

void platform_gpio_write(unsigned pin, int val) {
    volatile uint32_t *g = gpio();
    if (!g || pin > 53)
        return;
    if (val)
        g[(pin < 32 ? GPSET0 : GPSET0 + 4) / 4] = 1u << (pin % 32);
    else
        g[(pin < 32 ? GPCLR0 : GPCLR0 + 4) / 4] = 1u << (pin % 32);
}

void rpi_gpio_write(unsigned pin, int val) {
    platform_gpio_write(pin, val);
}

int platform_gpio_read(unsigned pin) {
    volatile uint32_t *g = gpio();
    if (!g || pin > 53)
        return 0;
    uint32_t lev = g[(pin < 32 ? GPLEV0 : GPLEV0 + 4) / 4];
    return (lev >> (pin % 32)) & 1;
}

int rpi_gpio_read(unsigned pin) {
    return platform_gpio_read(pin);
}
