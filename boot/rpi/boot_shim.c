/* Early aarch64 bring-up: identity + HHDM MMU, BootInfo, kernel_entry. */
#include "peak_boot.h"
#include "fdt.h"
#include "types.h"

extern uint64_t __boot_dtb;
extern char __kernel_start[];
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];
extern uint64_t boot_l0[];
extern uint64_t boot_l1_ident[];
extern uint64_t boot_l1_hhdm[];
extern uint64_t boot_l1_k[];
extern uint64_t boot_l2_id[];
extern uint64_t boot_l2_k[];
extern uint64_t boot_l3_k[];
extern struct peak_bootinfo boot_info;
extern uint8_t boot_kstack[];
extern uint8_t boot_kstack_top[];

extern void kernel_entry(struct peak_bootinfo *info);
extern int platform_mailbox_fb(struct peak_framebuffer_info *fb);
extern void platform_uart_putc(char c);

#define PTE_VALID (1ULL << 0)
#define PTE_TABLE (1ULL << 1)
#define PTE_AF    (1ULL << 10)
#define PTE_SH_INNER (3ULL << 8)
#define PTE_PXN   (1ULL << 53)
#define PTE_UXN   (1ULL << 54)
/* MAIR indices: 0 = Device-nGnRnE, 1 = Normal WB, 2 = Normal non-cacheable */
#define ATTR_NORMAL (1ULL << 2)
#define ATTR_NC     (2ULL << 2)
/* 2 MiB block descriptors (bits[1:0] = 0b01) */
#define BLK_NORMAL (PTE_VALID | PTE_AF | ATTR_NORMAL | PTE_SH_INNER)
#define BLK_DEVICE (PTE_VALID | PTE_AF | PTE_PXN | PTE_UXN)
#define BLK_FB_NC  (PTE_VALID | PTE_AF | ATTR_NC | PTE_PXN | PTE_UXN)
/* L0/L1/L2 table descriptor */
#define PTE_TABLE_DESC (PTE_VALID | PTE_TABLE)
/* L3 page descriptor (bits[1:0] = 0b11) */
#define PTE_PAGE (ATTR_NORMAL | PTE_SH_INNER | PTE_AF | PTE_VALID | PTE_TABLE)
#define KERNEL_MAP_MB 32
#define KERNEL_L3_TABLES (KERNEL_MAP_MB / 2) /* 2 MiB per L3 table */

static uint64_t vma_to_phys(uint64_t vma) {
    return (vma - PEAK_KERNEL_VMA) + 0x80000ULL;
}

static void early_putc(char c) {
    volatile uint32_t *u = (volatile uint32_t *)(uintptr_t)0x3F201000ULL;
    u[0] = (uint32_t)(uint8_t)c;
    for (volatile int i = 0; i < 1000; i++)
        ;
}

static void post_puts(const char *s) {
    while (*s)
        platform_uart_putc(*s++);
}

static void enable_mmu(uint64_t ttbr_phys) {
    /* attr0 Device-nGnRnE, attr1 Normal WB, attr2 Normal non-cacheable */
    uint64_t mair = (0x00ULL) | (0xFFULL << 8) | (0x44ULL << 16);
    __asm__ volatile ("msr mair_el1, %0" : : "r"(mair));
    /* T0SZ/T1SZ=16, IRGN/ORGN=WB, SH=inner (0b01 is reserved on silicon),
     * TG1=4K, IPS=40-bit so BCM2711 high-peri addresses translate. */
    uint64_t tcr = (16ULL << 0) | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) |
                   (16ULL << 16) | (1ULL << 24) | (1ULL << 26) | (3ULL << 28) |
                   (2ULL << 30) | (2ULL << 32);
    __asm__ volatile ("msr tcr_el1, %0" : : "r"(tcr));
    __asm__ volatile ("msr ttbr0_el1, %0" : : "r"(ttbr_phys));
    __asm__ volatile ("msr ttbr1_el1, %0" : : "r"(ttbr_phys));
    /* Page-table stores must be visible to the walker; stale TLB/icache out. */
    __asm__ volatile ("dsb ish\n isb");
    __asm__ volatile ("tlbi vmalle1\n dsb ish\n ic iallu\n dsb ish\n isb"
                      ::: "memory");
    uint64_t sctlr;
    __asm__ volatile ("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
    __asm__ volatile ("msr sctlr_el1, %0" : : "r"(sctlr));
    __asm__ volatile ("isb");
    __asm__ volatile ("tlbi vmalle1\n dsb ish\n isb" ::: "memory");
}

static void clear_bss(void) {
    for (char *p = __bss_start; p < __bss_end; p++)
        *p = 0;
}

void boot_shim_main(void) __attribute__((section(".text.boot")));

void boot_shim_main(void) {
    early_putc('S');

    uint64_t dtb_cell_phys;
    __asm__ volatile ("adr %0, __boot_dtb" : "=r"(dtb_cell_phys));
    uint64_t dtb_phys = *(uint64_t *)(uintptr_t)dtb_cell_phys;
    void *dtb = (void *)(uintptr_t)dtb_phys;

    uint64_t mem_size = 0x40000000ULL;
    uint64_t mem_base = 0;
    if (dtb_phys) {
        const uint8_t *b = (const uint8_t *)dtb;
        if (b[0] == 0xd0 && b[1] == 0x0d && b[2] == 0xfe && b[3] == 0xed)
            early_putc('D');
    }

    uint64_t *l0p, *l1ip, *l1hp, *l1kp, *l2ip, *l2kp, *l3kp;
    __asm__ volatile ("adr %0, boot_l0" : "=r"(l0p));
    __asm__ volatile ("adr %0, boot_l1_ident" : "=r"(l1ip));
    __asm__ volatile ("adr %0, boot_l1_hhdm" : "=r"(l1hp));
    __asm__ volatile ("adr %0, boot_l1_k" : "=r"(l1kp));
    __asm__ volatile ("adr %0, boot_l2_id" : "=r"(l2ip));
    __asm__ volatile ("adr %0, boot_l2_k" : "=r"(l2kp));
    __asm__ volatile ("adr %0, boot_l3_k" : "=r"(l3kp));

    for (int i = 0; i < 512; i++) {
        l0p[i] = 0;
        l1ip[i] = 0;
        l1hp[i] = 0;
        l1kp[i] = 0;
        l2kp[i] = 0;
    }
    for (int i = 0; i < KERNEL_L3_TABLES * 512; i++)
        l3kp[i] = 0;

    /* Peripheral window by CPU type: MMIO must be Device memory on silicon
     * (cacheable Normal mappings break mailbox/UART/USB on real BCM SoCs). */
    uint64_t midr = 0;
    __asm__ volatile ("mrs %0, midr_el1" : "=r"(midr));
    uint32_t part = (uint32_t)((midr >> 4) & 0xFFF);
    uint64_t dev_lo, dev_hi;
    if (part == 0xD08 || part == 0xD0B) {
        /* BCM2711 (A72) / BCM2712 (A76): 0xFC000000.. peri + local + GIC */
        dev_lo = 0xFC000000ULL;
        dev_hi = 0x100000000ULL;
    } else {
        /* BCM2837 (A53): 0x3F000000 peri + 0x40000000 local block */
        dev_lo = 0x3F000000ULL;
        dev_hi = 0x40200000ULL;
    }

    /* Identity + HHDM share 4 L2 tables of 2 MiB blocks covering 0-4 GiB. */
    for (int g = 0; g < 4; g++) {
        uint64_t *l2 = &l2ip[g * 512];
        for (int e = 0; e < 512; e++) {
            uint64_t phys = (((uint64_t)g) << 30) | (((uint64_t)e) << 21);
            uint64_t attr = (phys >= dev_lo && phys < dev_hi) ? BLK_DEVICE
                                                              : BLK_NORMAL;
            l2[e] = phys | attr;
        }
        l1ip[g] = (uint64_t)(uintptr_t)l2 | PTE_TABLE_DESC;
        l1hp[g] = (uint64_t)(uintptr_t)l2 | PTE_TABLE_DESC;
    }

    /*
     * Higher-half kernel at PEAK_KERNEL_VMA (L0=511, L1=510).
     * 4K pages: VA 0xffffffff80000000 + off → PA 0x80000 + off.
     * Required so absolute function pointers in .rodata work; the shim may
     * still enter the kernel via a PC-relative (identity) address.
     */
    for (int t = 0; t < KERNEL_L3_TABLES; t++) {
        uint64_t *l3 = &l3kp[t * 512];
        l2kp[t] = (uint64_t)(uintptr_t)l3 | PTE_TABLE_DESC;
        for (int p = 0; p < 512; p++) {
            uint64_t off = ((uint64_t)t * 512ULL + (uint64_t)p) << 12;
            l3[p] = (0x80000ULL + off) | PTE_PAGE;
        }
    }
    l1kp[510] = (uint64_t)(uintptr_t)l2kp | PTE_TABLE_DESC;

    l0p[0] = (uint64_t)(uintptr_t)l1ip | PTE_TABLE_DESC;
    l0p[256] = (uint64_t)(uintptr_t)l1hp | PTE_TABLE_DESC;
    l0p[511] = (uint64_t)(uintptr_t)l1kp | PTE_TABLE_DESC;

    /* Runtime assert: kernel image must fit in the boot L3 window. */
    {
        uint64_t ksz = (uint64_t)(__kernel_end - __kernel_start);
        if (ksz > ((uint64_t)KERNEL_MAP_MB << 20)) {
            early_putc('!'); /* kernel too large for boot map */
            for (;;)
                ;
        }
    }
    /* BCM2712 peri sits above 4 GiB — identity map stops at 4 GiB. */
    if (part == 0xD0B)
        early_putc('5'); /* Pi 5 detected; high MMIO not mapped yet */

    early_putc('M');
    enable_mmu((uint64_t)(uintptr_t)l0p);
    early_putc('U');

    post_puts("peak-rpi: mmu on\n");
    clear_bss();

    if (dtb_phys && fdt_check(dtb) == 0)
        fdt_memory_range(dtb, &mem_base, &mem_size);

    struct peak_bootinfo *info;
    __asm__ volatile ("adr %0, boot_info" : "=r"(info));

    info->magic = PEAK_BOOT_MAGIC;
    info->version = PEAK_BOOT_VERSION;
    info->flags = 0;
    info->entropy_len = 0;
    info->kaslr_slide_pages = 0;
    {
        /* aarch64: mix CNTVCT jitter (supplemental / weak until HW RNG). */
        for (int i = 0; i < PEAK_BOOT_ENTROPY_MAX; i++) {
            uint64_t t;
            __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(t));
            for (volatile int j = 0; j < 80 + i * 3; j++)
                t ^= (uint64_t)j * 0x9e3779b97f4a7c15ULL;
            info->entropy[i] = (uint8_t)(t ^ (t >> 11) ^ (t >> 29));
        }
        info->entropy_len = PEAK_BOOT_ENTROPY_MAX;
        info->flags |= PEAK_BOOT_FLAG_ENTROPY_WEAK;
    }
    info->hhdm_offset = PEAK_HHDM_OFFSET;
    info->rsdp_phys = 0;
    info->dtb_phys = 0;
    info->dtb_size = 0;
    if (dtb_phys && fdt_check(dtb) == 0) {
        info->dtb_phys = dtb_phys;
        info->dtb_size = fdt_totalsize(dtb);
        info->flags |= PEAK_BOOT_FLAG_HAS_DTB;
    }
    info->kernel_phys_base = 0x80000;
    info->kernel_phys_size = (uint64_t)(__kernel_end - __kernel_start);

    uint64_t kstack_phys;
    __asm__ volatile ("adr %0, boot_kstack" : "=r"(kstack_phys));
    info->stack_phys = kstack_phys;
    info->stack_size = PEAK_BOOT_STACK_SIZE;

    info->mmap_count = 0;
    info->mmap[info->mmap_count++] = (struct peak_mmap_entry){
        .base = 0x100000,
        .length = mem_size > 0x100000 ? mem_size - 0x100000 : 0x3F000000,
        .type = PEAK_MMAP_USABLE,
    };
    info->mmap[info->mmap_count++] = (struct peak_mmap_entry){
        .base = 0x80000,
        .length = info->kernel_phys_size,
        .type = PEAK_MMAP_KERNEL,
    };

    if (platform_mailbox_fb(&info->fb) == 0 && info->fb.addr) {
        /* Remap framebuffer (and optional pageflip second page) as Normal
         * non-cacheable so CPU writes reach scanout RAM. */
        uint64_t fb_lo = info->fb.addr & ~((1ULL << 21) - 1);
        uint64_t fb_bytes = (uint64_t)info->fb.pitch * info->fb.height * 2;
        uint64_t fb_hi = info->fb.addr + fb_bytes;
        for (uint64_t phys = fb_lo; phys < fb_hi; phys += (1ULL << 21)) {
            if (phys < (4ULL << 30))
                l2ip[phys >> 21] = phys | BLK_FB_NC;
        }
        __asm__ volatile ("dsb ish\n tlbi vmalle1\n dsb ish\n isb" ::: "memory");

        info->fb.addr = info->fb.addr + PEAK_HHDM_OFFSET;
        info->flags |= PEAK_BOOT_FLAG_HAS_FB;
        post_puts("peak-rpi: framebuffer ok\n");
    } else {
        info->fb.addr = 0;
        post_puts("peak-rpi: no framebuffer\n");
    }

    uint64_t kstack = PEAK_HHDM_OFFSET + info->stack_phys + PEAK_BOOT_STACK_SIZE;
    struct peak_bootinfo *info_v =
        (struct peak_bootinfo *)(PEAK_HHDM_OFFSET + (uint64_t)(uintptr_t)info);

    post_puts("peak-rpi: entering kernel\n");
    (void)vma_to_phys;
    (void)boot_kstack_top;

    __asm__ volatile (
        "mov sp, %0\n"
        "mov x0, %1\n"
        "br %2\n"
        :
        : "r"(kstack), "r"(info_v), "r"((uint64_t)kernel_entry)
        : "x0", "memory");

    for (;;)
        ;
}
