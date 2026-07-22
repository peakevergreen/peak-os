#include "platform.h"
#include "gpio.h"
#include "blockdev.h"
#include "netdev.h"
#include "peak_boot.h"
#include "util.h"

void platform_gpio_set_output(unsigned pin) { (void)pin; }
void platform_gpio_write(unsigned pin, int val) { (void)pin; (void)val; }
int  platform_gpio_read(unsigned pin) { (void)pin; return 0; }

int platform_init(struct peak_bootinfo *info) {
    (void)info;
    blockdev_register_ata();
    netdev_register_e1000();
    return 0;
}

void platform_late_init(struct peak_bootinfo *info) {
    (void)info;
}

void platform_poll(void) {
    /* PS/2 and e1000 are IRQ-driven on PC. */
}

const char *platform_name(void) {
    return "pc";
}

void platform_reboot(void) {
    reboot();
}

void platform_shutdown(void) {
    /* Handled by power.c on PC/QEMU. */
}

void platform_dma_clean(void *addr, size_t len) {
    (void)addr;
    (void)len;
}

void platform_dma_invalidate(void *addr, size_t len) {
    (void)addr;
    (void)len;
}

uint64_t platform_phys_to_bus(uint64_t phys) {
    return phys;
}

uint64_t platform_virt_to_bus(void *virt) {
    (void)virt;
    return 0; /* PC DMA uses physical addresses via driver-specific paths */
}

int platform_mmio_mapped(void) {
    return 1;
}

void platform_timer_enable(void) {
}

int platform_timer_irq_pending(void) {
    return 1; /* PIC path acknowledges in the IRQ stub */
}

int platform_irq_ack_dispatch(void) {
    return 0; /* x86 uses IDT/PIC path in isr.S */
}

void platform_irq_eoi(void) {
}
