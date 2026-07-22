#include "platform.h"
#include "rpi.h"
#include "blockdev.h"
#include "netdev.h"
#include "fdt.h"
#include "irq.h"
#include "arch.h"
#include "serial.h"
#include "peak_boot.h"
#include "util.h"

static struct rpi_plat g_rpi;
static int g_mmio_mapped = 1;

/* BCM2711 GICv2 (Pi 4); PPI 30 = CNTPNSIRQ. */
#define RPI4_GICD 0xFF841000ULL
#define RPI4_GICC 0xFF842000ULL
#define GICD_ISENABLER0 0x100
#define GICC_IAR  0x00C
#define GICC_EOIR 0x010
#define GIC_PPI_CNTPNS 30

/* VideoCore ARM→VC bus alias for DRAM below 1 GiB (all BCM SoCs). */
#define VC_BUS_ALIAS 0xC0000000ULL

struct rpi_plat *rpi_get(void) {
    return &g_rpi;
}

enum rpi_soc rpi_detect_soc(const void *fdt) {
    if (fdt) {
        uint32_t len = 0;
        const char *compat = (const char *)fdt_getprop(fdt, "", "compatible", &len);
        if (compat && len) {
            const char *p = compat;
            const char *end = compat + len;
            while (p < end && *p) {
                if (strstr(p, "bcm2712") || strstr(p, "rp1"))
                    return RPI_SOC_BCM2712;
                if (strstr(p, "bcm2711"))
                    return RPI_SOC_BCM2711;
                if (strstr(p, "bcm2837") || strstr(p, "bcm2836"))
                    return RPI_SOC_BCM2837;
                p += strlen(p) + 1;
            }
        }
    }
    uint64_t midr = 0;
    __asm__ volatile ("mrs %0, midr_el1" : "=r"(midr));
    uint32_t part = (uint32_t)((midr >> 4) & 0xFFF);
    if (part == 0xD03) return RPI_SOC_BCM2837;
    if (part == 0xD08) return RPI_SOC_BCM2711;
    if (part == 0xD0B) return RPI_SOC_BCM2712;
    return RPI_SOC_BCM2837;
}

uint64_t platform_peri_base(void) {
    if (g_rpi.peri_base)
        return g_rpi.peri_base;
    return 0x3F000000ULL;
}

static void rpi_fill_bases(enum rpi_soc soc) {
    g_rpi.soc = soc;
    g_mmio_mapped = 1;
    switch (soc) {
    case RPI_SOC_BCM2711:
        g_rpi.peri_base = 0xFE000000ULL;
        g_rpi.local_base = 0xFF800000ULL;
        g_rpi.uart_base = 0xFE201000ULL;
        g_rpi.gpio_base = 0xFE200000ULL;
        g_rpi.mbox_base = 0xFE00B880ULL;
        g_rpi.sdhci_base = 0xFE300000ULL;
        g_rpi.usb_base = 0xFE980000ULL;
        break;
    case RPI_SOC_BCM2712:
        /* Bases sit above 4 GiB; boot page tables currently cover 0–4 GiB only. */
        g_rpi.peri_base = 0x107C000000ULL;
        g_rpi.local_base = 0x107C000000ULL;
        g_rpi.uart_base = 0x107D001000ULL;
        g_rpi.gpio_base = 0x107D508000ULL;
        g_rpi.mbox_base = 0x107C00B880ULL;
        g_rpi.sdhci_base = 0x1000FFF000ULL;
        g_rpi.usb_base = 0;
        g_mmio_mapped = 0;
        break;
    case RPI_SOC_BCM2837:
    default:
        g_rpi.peri_base = 0x3F000000ULL;
        g_rpi.local_base = 0x40000000ULL;
        g_rpi.uart_base = 0x3F201000ULL;
        g_rpi.gpio_base = 0x3F200000ULL;
        g_rpi.mbox_base = 0x3F00B880ULL;
        g_rpi.sdhci_base = 0x3F300000ULL;
        g_rpi.usb_base = 0x3F980000ULL;
        break;
    }
}

int platform_init(struct peak_bootinfo *info) {
    enum rpi_soc soc = RPI_SOC_BCM2837;
    const void *fdt = 0;
    if (info && (info->flags & PEAK_BOOT_FLAG_HAS_DTB) && info->dtb_phys) {
        fdt = (const void *)(uintptr_t)(info->hhdm_offset + info->dtb_phys);
        soc = rpi_detect_soc(fdt);
    } else {
        soc = rpi_detect_soc(0);
    }
    rpi_fill_bases(soc);
    g_rpi.fdt = fdt;

    serial_write_str("rpi: soc=");
    char b[8];
    itoa_u((uint64_t)soc, b, 10);
    serial_write_str(b);
    serial_write_str("\n");

    if (!g_mmio_mapped) {
        serial_write_str("rpi: SoC MMIO above 4GiB is not mapped; "
                         "Pi 5 peripheral bring-up deferred\n");
        return 0;
    }

    rpi_gpio_init();
    rpi_sdhci_init();
    blockdev_register_sdhci();
    return 0;
}

void platform_late_init(struct peak_bootinfo *info) {
    (void)info;
    if (!g_mmio_mapped)
        return;
    rpi_gpu_init();
    rpi_sound_init();
    rpi_wifi_init();
    /* USB/net: Pi 3 DWC2 path; Pi 4/5 host controllers stay staged. */
    rpi_usb_init();
    rpi_net_init();
}

void platform_poll(void) {
    rpi_usb_poll();
}

const char *platform_name(void) {
    static char name[8];
    name[0] = 'r';
    name[1] = 'p';
    name[2] = 'i';
    switch (g_rpi.soc) {
    case RPI_SOC_BCM2711: name[3] = '4'; name[4] = '\0'; break;
    case RPI_SOC_BCM2712: name[3] = '5'; name[4] = '\0'; break;
    case RPI_SOC_BCM2837: name[3] = '3'; name[4] = '\0'; break;
    default: name[3] = '\0'; break;
    }
    return name;
}

void platform_reboot(void) {
    volatile uint32_t *pm_rstc = (volatile uint32_t *)(uintptr_t)(g_rpi.peri_base + 0x10001c);
    volatile uint32_t *pm_wdog = (volatile uint32_t *)(uintptr_t)(g_rpi.peri_base + 0x100024);
    const uint32_t PM_PASSWORD = 0x5a000000;
    *pm_wdog = PM_PASSWORD | 1;
    *pm_rstc = PM_PASSWORD | 0x20;
    for (;;)
        __asm__ volatile ("wfi");
}

void platform_shutdown(void) {
    platform_reboot();
}

void platform_dma_clean(void *addr, size_t len) {
    uintptr_t a = (uintptr_t)addr & ~63ULL;
    uintptr_t end = (uintptr_t)addr + len;
    for (; a < end; a += 64)
        __asm__ volatile ("dc cvac, %0" : : "r"(a) : "memory");
    __asm__ volatile ("dsb sy");
}

void platform_dma_invalidate(void *addr, size_t len) {
    /* Clean+invalidate: bare dc ivac can drop dirty CPU lines. */
    uintptr_t a = (uintptr_t)addr & ~63ULL;
    uintptr_t end = (uintptr_t)addr + len;
    for (; a < end; a += 64)
        __asm__ volatile ("dc civac, %0" : : "r"(a) : "memory");
    __asm__ volatile ("dsb sy");
}

uint64_t platform_phys_to_bus(uint64_t phys) {
    /* VC GPU bus alias for low DRAM; identity for high/peripheral phys. */
    if (phys < 0x40000000ULL)
        return phys | VC_BUS_ALIAS;
    return phys;
}

uint64_t platform_virt_to_bus(void *virt) {
    return platform_phys_to_bus(arch_virt_to_phys(virt));
}

int platform_mmio_mapped(void) {
    return g_mmio_mapped;
}

void platform_timer_enable(void) {
    if (g_rpi.soc == RPI_SOC_BCM2837) {
        volatile uint32_t *core0_timer =
            (volatile uint32_t *)(uintptr_t)(g_rpi.local_base + 0x40);
        *core0_timer = 0x2; /* CNTPNSIRQ → core 0 IRQ */
        return;
    }
    if (g_rpi.soc == RPI_SOC_BCM2711 && g_mmio_mapped) {
        volatile uint32_t *gicd = (volatile uint32_t *)(uintptr_t)RPI4_GICD;
        /* Enable PPI 30 (CNTPNSIRQ) in GICD_ISENABLER0. */
        gicd[GICD_ISENABLER0 / 4] = (1u << GIC_PPI_CNTPNS);
    }
}

int platform_timer_irq_pending(void) {
    if (g_rpi.soc == RPI_SOC_BCM2837) {
        volatile const uint32_t *core0_irq_source =
            (volatile const uint32_t *)(uintptr_t)(g_rpi.local_base + 0x60);
        /* Bit 1 is the core physical non-secure timer (CNTPNSIRQ). */
        return (*core0_irq_source & (1u << 1)) != 0;
    }

    uint64_t ctl;
    __asm__ volatile ("mrs %0, cntp_ctl_el0" : "=r"(ctl));
    return (ctl & (1ull << 2)) != 0; /* ISTATUS */
}

int platform_irq_ack_dispatch(void) {
    if (g_rpi.soc == RPI_SOC_BCM2837) {
        volatile const uint32_t *src =
            (volatile const uint32_t *)(uintptr_t)(g_rpi.local_base + 0x60);
        uint32_t s = *src;
        int handled = 0;
        /* Bit 1: CNTPNSIRQ — portable IRQ 0 is the system timer. */
        if (s & (1u << 1)) {
            irq_dispatch(0);
            handled = 1;
        }
        /* Bits 8..11: GPU IRQs 0..3 — leave for future device drivers. */
        (void)s;
        return handled;
    }

    if (g_rpi.soc == RPI_SOC_BCM2711 && g_mmio_mapped) {
        volatile uint32_t *gicc = (volatile uint32_t *)(uintptr_t)RPI4_GICC;
        uint32_t iar = gicc[GICC_IAR / 4];
        uint32_t irq = iar & 0x3FFu;
        if (irq >= 1020u)
            return 0; /* spurious */
        if (irq == GIC_PPI_CNTPNS)
            irq_dispatch(0);
        else
            irq_dispatch(irq);
        gicc[GICC_EOIR / 4] = iar;
        return 1;
    }

    /* Pi 5 / unknown: fall back to timer pending check only. */
    if (platform_timer_irq_pending()) {
        irq_dispatch(0);
        return 1;
    }
    return 0;
}

void platform_irq_eoi(void) {
    /*
     * BCM2837 local timer IRQs deassert when CNTP_TVAL is reloaded.
     * GIC EOI is performed in platform_irq_ack_dispatch().
     */
    __asm__ volatile ("dsb sy" : : : "memory");
}
