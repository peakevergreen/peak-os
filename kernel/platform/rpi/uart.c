#include "rpi.h"

/* PL011 UART — Pi (0x3F../0xFE..) or QEMU virt (0x09000000). */
#define DR    0x00
#define FR    0x18
#define IBRD  0x24
#define FBRD  0x28
#define LCRH  0x2c
#define CR    0x30
#define ICR   0x44

static uint64_t g_uart_base = 0x3F201000ULL;

static volatile uint32_t *uart_regs(void) {
    return (volatile uint32_t *)(uintptr_t)g_uart_base;
}

static void uart_hw_init(volatile uint32_t *u) {
    u[CR / 4] = 0;
    u[ICR / 4] = 0x7FF;
    u[IBRD / 4] = 26;
    u[FBRD / 4] = 3;
    u[LCRH / 4] = (3 << 5) | (1 << 4);
    u[CR / 4] = (1 << 0) | (1 << 8) | (1 << 9);
}

/* Safe before MMU: only physical MMIO, no high-VMA globals beyond this file's
 * static (which lives in .data at a physical address once relocated — but C
 * still emits high VMA for statics!). So keep early path in phys-only asm/helpers. */

void platform_early_uart(void) {
    /* Direct phys PL011 — do not touch rpi_get() / high VMA. */
    volatile uint32_t *u = (volatile uint32_t *)(uintptr_t)0x3F201000ULL;
    uart_hw_init(u);
}

void platform_uart_init(void) {
    struct rpi_plat *p = rpi_get();
    if (p && p->uart_base)
        g_uart_base = p->uart_base;
    uart_hw_init(uart_regs());
}

void platform_uart_set_base(uint64_t base) {
    if (base)
        g_uart_base = base;
}

void platform_uart_putc(char c) {
    /* Before MMU, callers must use platform_early_uart_putc. After MMU, high
     * VMA access to g_uart_base is valid. */
    volatile uint32_t *u = uart_regs();
    int spins = 0;
    while ((u[FR / 4] & (1 << 5)) && spins++ < 100000)
        ;
    u[DR / 4] = (uint32_t)(uint8_t)c;
}

void platform_early_uart_putc(char c) {
    volatile uint32_t *u = (volatile uint32_t *)(uintptr_t)0x3F201000ULL;
    /* Do not spin on FR before MMU/IRQ — TXFF can look stuck on bring-up. */
    u[DR / 4] = (uint32_t)(uint8_t)c;
    for (volatile int i = 0; i < 1000; i++)
        ;
}
