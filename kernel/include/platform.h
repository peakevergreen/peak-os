#ifndef PEAK_PLATFORM_H
#define PEAK_PLATFORM_H

#include "types.h"

struct peak_bootinfo;

/* Board/firmware bring-up after arch_early_init / BootInfo validate. */
int  platform_init(struct peak_bootinfo *info);
void platform_late_init(struct peak_bootinfo *info);
void platform_poll(void);
const char *platform_name(void);

/* Power */
void platform_reboot(void);
void platform_shutdown(void);

/* Cache maintenance for non-coherent DMA (no-op on x86). */
void platform_dma_clean(void *addr, size_t len);
void platform_dma_invalidate(void *addr, size_t len);

/* VA/PA → DMA / VideoCore bus addresses (identity on PC). */
uint64_t platform_phys_to_bus(uint64_t phys);
uint64_t platform_virt_to_bus(void *virt);

/* Architecture timer interrupt routing/acknowledgement. */
void platform_timer_enable(void);
int  platform_timer_irq_pending(void);
/* Ack + dispatch one IRQ; returns 1 if a line was handled. */
int  platform_irq_ack_dispatch(void);
void platform_irq_eoi(void);

/* 1 when SoC MMIO required by drivers is covered by boot page tables. */
int  platform_mmio_mapped(void);

#endif
