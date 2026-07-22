#include "boot_paging.h"
#include "boot_util.h"

#define PTE_P  (1ULL << 0)
#define PTE_W  (1ULL << 1)
#define PTE_HUGE (1ULL << 7)

static uint64_t *virt_of(uint64_t phys) {
    return (uint64_t *)(uintptr_t)phys;
}

static uint64_t alloc_table(struct boot_page_allocator *alloc) {
    uint64_t p = alloc->alloc_pages(1);
    if (!p)
        return 0;
    boot_memset(virt_of(p), 0, BOOT_PAGE_SIZE);
    return p;
}

static int map_4k(uint64_t pml4_phys, uint64_t virt, uint64_t phys, int writable,
                  struct boot_page_allocator *alloc) {
    uint64_t *pml4 = virt_of(pml4_phys);
    uint64_t pml4e = (virt >> 39) & 0x1FF;
    uint64_t pdpte = (virt >> 30) & 0x1FF;
    uint64_t pde   = (virt >> 21) & 0x1FF;
    uint64_t pte   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4e] & PTE_P)) {
        uint64_t t = alloc_table(alloc);
        if (!t)
            return -1;
        pml4[pml4e] = t | PTE_P | PTE_W;
    }
    uint64_t *pdpt = virt_of(pml4[pml4e] & ~0xFFFULL);
    if (!(pdpt[pdpte] & PTE_P)) {
        uint64_t t = alloc_table(alloc);
        if (!t)
            return -1;
        pdpt[pdpte] = t | PTE_P | PTE_W;
    }
    uint64_t *pd = virt_of(pdpt[pdpte] & ~0xFFFULL);
    if (pd[pde] & PTE_P) {
        if (pd[pde] & PTE_HUGE)
            return -1; /* conflict with huge page */
    } else {
        uint64_t t = alloc_table(alloc);
        if (!t)
            return -1;
        pd[pde] = t | PTE_P | PTE_W;
    }
    uint64_t *pt = virt_of(pd[pde] & ~0xFFFULL);
    uint64_t flags = PTE_P | (writable ? PTE_W : 0);
    pt[pte] = (phys & ~0xFFFULL) | flags;
    return 0;
}

static int map_2m(uint64_t pml4_phys, uint64_t virt, uint64_t phys, int writable,
                  struct boot_page_allocator *alloc) {
    uint64_t *pml4 = virt_of(pml4_phys);
    uint64_t pml4e = (virt >> 39) & 0x1FF;
    uint64_t pdpte = (virt >> 30) & 0x1FF;
    uint64_t pde   = (virt >> 21) & 0x1FF;

    if (!(pml4[pml4e] & PTE_P)) {
        uint64_t t = alloc_table(alloc);
        if (!t)
            return -1;
        pml4[pml4e] = t | PTE_P | PTE_W;
    }
    uint64_t *pdpt = virt_of(pml4[pml4e] & ~0xFFFULL);
    if (!(pdpt[pdpte] & PTE_P)) {
        uint64_t t = alloc_table(alloc);
        if (!t)
            return -1;
        pdpt[pdpte] = t | PTE_P | PTE_W;
    }
    uint64_t *pd = virt_of(pdpt[pdpte] & ~0xFFFULL);
    uint64_t flags = PTE_P | PTE_HUGE | (writable ? PTE_W : 0);
    pd[pde] = (phys & ~0x1FFFFFULL) | flags;
    return 0;
}

int boot_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, int writable,
                  struct boot_page_allocator *alloc) {
    return map_4k(pml4_phys, virt, phys, writable, alloc);
}

int boot_map_range(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t size,
                   int writable, struct boot_page_allocator *alloc) {
    uint64_t off = 0;
    while (off < size) {
        if (((virt + off) & 0x1FFFFF) == 0 &&
            ((phys + off) & 0x1FFFFF) == 0 &&
            size - off >= 0x200000) {
            if (map_2m(pml4_phys, virt + off, phys + off, writable, alloc) != 0)
                return -1;
            off += 0x200000;
            continue;
        }
        if (map_4k(pml4_phys, virt + off, phys + off, writable, alloc) != 0)
            return -1;
        off += 0x1000;
    }
    return 0;
}

int boot_paging_init(struct boot_page_allocator *alloc,
                     uint64_t identity_end,
                     uint64_t *pml4_phys_out) {
    if (!alloc || !pml4_phys_out)
        return -1;
    uint64_t pml4 = alloc_table(alloc);
    if (!pml4)
        return -1;

    /* Identity + HHDM for low memory (at least 4 GiB for QEMU guests). */
    uint64_t end = identity_end;
    if (end < 0x100000000ULL)
        end = 0x100000000ULL;
    end = (end + 0x1FFFFF) & ~0x1FFFFFULL;

    for (uint64_t phys = 0; phys < end; phys += 0x200000) {
        if (map_2m(pml4, phys, phys, 1, alloc) != 0)
            return -1;
        if (map_2m(pml4, PEAK_HHDM_OFFSET + phys, phys, 1, alloc) != 0)
            return -1;
    }

    *pml4_phys_out = pml4;
    return 0;
}

void boot_paging_activate(uint64_t pml4_phys) {
    __asm__ volatile(
        "mov %0, %%cr3\n"
        :
        : "r"(pml4_phys)
        : "memory");
}
