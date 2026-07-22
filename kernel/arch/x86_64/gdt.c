#include "gdt.h"
#include "util.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[7];
static struct tss tss;
static uint8_t kernel_stack[16384] __attribute__((aligned(16)));

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_high = (base >> 24) & 0xFF;
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].granularity = (limit >> 16) & 0x0F;
    gdt[i].granularity |= gran & 0xF0;
    gdt[i].access = access;
}

void gdt_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = (uint64_t)(kernel_stack + sizeof(kernel_stack));
    tss.iomap_base = sizeof(tss);

    memset(gdt, 0, sizeof(gdt));
    /* 0: null */
    gdt_set(1, 0, 0, 0x9A, 0xA0); /* kernel code */
    gdt_set(2, 0, 0, 0x92, 0xA0); /* kernel data */
    gdt_set(3, 0, 0, 0xFA, 0xA0); /* user code */
    gdt_set(4, 0, 0, 0xF2, 0xA0); /* user data */

    /* TSS at index 5 (16 bytes) */
    uint64_t tb = (uint64_t)&tss;
    struct gdt_tss_entry *te = (struct gdt_tss_entry *)&gdt[5];
    te->limit_low = sizeof(tss) - 1;
    te->base_low = tb & 0xFFFF;
    te->base_mid = (tb >> 16) & 0xFF;
    te->access = 0x89;
    te->granularity = 0;
    te->base_high = (tb >> 24) & 0xFF;
    te->base_upper = (uint32_t)(tb >> 32);
    te->reserved = 0;

    struct gdt_ptr gp = { sizeof(gdt) - 1, (uint64_t)&gdt };
    __asm__ volatile (
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(gp)
        : "rax", "memory"
    );
}
