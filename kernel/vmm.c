#include "vmm.h"
#include "pmm.h"
#include "arch.h"
#include "util.h"
#include "peak_boot.h"

static uint64_t hhdm;
static uint64_t kernel_phys = 0x80000ULL; /* Peak default load (Pi kernel8.img) */
static uint64_t kernel_cr3;
static int nx_enabled;

#if defined(__aarch64__)
#define PTE_AF          (1ULL << 10)
#define PTE_ATTR_NORMAL (1ULL << 2)
#define PTE_TABLE       (0b11ULL)
#define PTE_PAGE_RW     (PTE_TABLE | PTE_AF | PTE_ATTR_NORMAL)
#define PTE_AP_EL0_RW   (1ULL << 6)
#define PTE_UXN         (1ULL << 54)
#define PTE_PXN         (1ULL << 53)
#else
#define PTE_NX          (1ULL << 63)
#endif

void vmm_init(uint64_t hhdm_offset) {
    hhdm = hhdm_offset;
    if (!kernel_phys)
        kernel_phys = 0x80000ULL; /* Peak default load address */
    kernel_cr3 = vmm_current_cr3();
    nx_enabled = 0;
}

void vmm_set_kernel_phys_base(uint64_t phys) {
    if (phys)
        kernel_phys = phys;
}

void vmm_enable_nx(void) {
#ifdef PEAK_HOST_TEST
    nx_enabled = 1;
    return;
#endif
#if defined(__x86_64__)
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001u), "c"(0));
    if ((d >> 20) & 1) {
        uint64_t efer;
        __asm__ volatile ("rdmsr" : "=a"(a), "=d"(d) : "c"(0xC0000080u));
        efer = ((uint64_t)d << 32) | a;
        efer |= (1ULL << 11); /* NXE */
        a = (uint32_t)efer;
        d = (uint32_t)(efer >> 32);
        __asm__ volatile ("wrmsr" : : "a"(a), "d"(d), "c"(0xC0000080u));
        /* CR0.WP */
        uint64_t cr0;
        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= (1ULL << 16);
        __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
        nx_enabled = 1;
    }
#else
    nx_enabled = 1;
#endif
}

uint64_t vmm_kernel_cr3(void) {
    return kernel_cr3 ? kernel_cr3 : vmm_current_cr3();
}

uint64_t vmm_hhdm(void) {
    return hhdm;
}

void *vmm_phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm);
}

uint64_t vmm_virt_to_phys(void *virt) {
    uint64_t v = (uint64_t)virt;
    /* Kernel image window sits above the HHDM window; check it first. */
    if (v >= PEAK_KERNEL_VMA)
        return (v - PEAK_KERNEL_VMA) + kernel_phys;
    /* HHDM: phys = virt - hhdm_offset */
    if (hhdm && v >= hhdm)
        return v - hhdm;
    if (v >= PEAK_HHDM_OFFSET)
        return v - PEAK_HHDM_OFFSET;
    /* Identity / low VA */
    return v;
}

uint64_t arch_virt_to_phys(void *virt) {
    return vmm_virt_to_phys(virt);
}

uint64_t vmm_current_cr3(void) {
#ifdef PEAK_HOST_TEST
    extern uint64_t vmm_host_test_cr3;
    return vmm_host_test_cr3;
#endif
    uint64_t cr3;
#if defined(__aarch64__)
    __asm__ volatile ("mrs %0, ttbr1_el1" : "=r"(cr3));
#else
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
#endif
    return cr3 & ~0xFFFULL;
}

void vmm_invlpg(uint64_t vaddr) {
#ifdef PEAK_HOST_TEST
    (void)vaddr;
    return;
#endif
#if defined(__x86_64__)
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr) : "memory");
#else
    (void)vaddr;
    __asm__ volatile ("tlbi vmalle1is; dsb sy; isb" ::: "memory");
#endif
}

static uint64_t *pt_walk_alloc(uint64_t cr3_phys, uint64_t vaddr, int create) {
    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(cr3_phys);
    int i4 = (vaddr >> 39) & 0x1FF;
    int i3 = (vaddr >> 30) & 0x1FF;
    int i2 = (vaddr >> 21) & 0x1FF;
    int i1 = (vaddr >> 12) & 0x1FF;
    int user_half = (i4 < 256);

#if defined(__aarch64__)
    const uint64_t table_flags = PTE_TABLE;
#else
    /* Intermediate tables: user bit only for user-half walks. */
    const uint64_t table_flags = user_half ? 0b111u : 0b011u;
#endif
    (void)user_half;

    if (!(pml4[i4] & 1)) {
        if (!create) return NULL;
        void *p = pmm_alloc();
        if (!p) return NULL;
        memset(vmm_phys_to_virt((uint64_t)p), 0, 4096);
        pml4[i4] = ((uint64_t)p) | table_flags;
    }
    uint64_t *pdpt = (uint64_t *)vmm_phys_to_virt(pml4[i4] & ~0xFFFULL);

    if (!(pdpt[i3] & 1)) {
        if (!create) return NULL;
        void *p = pmm_alloc();
        if (!p) return NULL;
        memset(vmm_phys_to_virt((uint64_t)p), 0, 4096);
        pdpt[i3] = ((uint64_t)p) | table_flags;
    }
    uint64_t *pd = (uint64_t *)vmm_phys_to_virt(pdpt[i3] & ~0xFFFULL);

    if (!(pd[i2] & 1)) {
        if (!create) return NULL;
        void *p = pmm_alloc();
        if (!p) return NULL;
        memset(vmm_phys_to_virt((uint64_t)p), 0, 4096);
        pd[i2] = ((uint64_t)p) | table_flags;
    }
    uint64_t *pt = (uint64_t *)vmm_phys_to_virt(pd[i2] & ~0xFFFULL);
    return &pt[i1];
}

static uint64_t flags_to_pte(uint32_t flags) {
#if defined(__aarch64__)
    uint64_t pte = PTE_PAGE_RW;
    if (!(flags & VMM_EXEC))
        pte |= PTE_UXN | PTE_PXN;
    if (flags & VMM_USER)
        pte |= PTE_AP_EL0_RW;
    if (!(flags & VMM_WRITE) && (flags & VMM_USER))
        pte |= (1ULL << 7); /* AP[2] read-only if set with AP */
    return pte;
#else
    uint64_t pte = 0;
    if (flags & VMM_PRESENT)
        pte |= 1;
    if (flags & VMM_WRITE)
        pte |= 2;
    if (flags & VMM_USER)
        pte |= 4;
    if (flags & VMM_GLOBAL)
        pte |= (1ULL << 8);
    if (flags & VMM_NOCACHE)
        pte |= (1ULL << 4); /* PCD */
    if (nx_enabled && !(flags & VMM_EXEC))
        pte |= PTE_NX;
    return pte;
#endif
}

uint64_t vmm_create_address_space(void) {
    void *p = pmm_alloc();
    if (!p)
        return 0;
    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt((uint64_t)p);
    memset(pml4, 0, 4096);
#ifndef PEAK_HOST_TEST
    uint64_t *cur = (uint64_t *)vmm_phys_to_virt(vmm_current_cr3());
    for (int i = 256; i < 512; i++)
        pml4[i] = cur[i];
#endif
    return (uint64_t)p;
}

int vmm_map_page_flags(uint64_t cr3_phys, uint64_t vaddr, uint64_t paddr, uint32_t flags) {
    if (!(flags & VMM_PRESENT))
        return -1;
    /* Reject W^X for user mappings. */
    if ((flags & VMM_USER) && (flags & VMM_WRITE) && (flags & VMM_EXEC))
        return -1;
    if (vaddr & 0xFFFULL)
        return -1;
    if ((flags & VMM_USER) && vaddr >= 0x0000800000000000ULL)
        return -1;
    uint64_t *pte = pt_walk_alloc(cr3_phys, vaddr, 1);
    if (!pte)
        return -1;
    *pte = (paddr & ~0xFFFULL) | flags_to_pte(flags);
    return 0;
}

int vmm_map_page(uint64_t cr3_phys, uint64_t vaddr, uint64_t paddr, int user_writable) {
    /* Compat: writable data NX; readonly treated as RX for legacy callers. */
    uint32_t f = VMM_PRESENT | VMM_USER;
    if (user_writable)
        f |= VMM_WRITE; /* NX */
    else
        f |= VMM_EXEC; /* RX */
    return vmm_map_page_flags(cr3_phys, vaddr, paddr, f);
}

int vmm_unmap_page(uint64_t cr3_phys, uint64_t vaddr) {
    uint64_t *pte = pt_walk_alloc(cr3_phys, vaddr, 0);
    if (!pte || !(*pte & 1))
        return -1;
    *pte = 0;
    vmm_invlpg(vaddr);
    return 0;
}

int vmm_query(uint64_t cr3_phys, uint64_t vaddr, uint64_t *paddr_out, uint32_t *flags_out) {
    uint64_t *pte = pt_walk_alloc(cr3_phys, vaddr, 0);
    if (!pte || !(*pte & 1))
        return -1;
    if (paddr_out)
        *paddr_out = *pte & ~0xFFFULL;
    if (flags_out) {
        uint32_t f = VMM_PRESENT;
#if defined(__x86_64__)
        if (*pte & 2)
            f |= VMM_WRITE;
        if (*pte & 4)
            f |= VMM_USER;
        if (!nx_enabled || !(*pte & PTE_NX))
            f |= VMM_EXEC;
#else
        f |= VMM_USER;
        if (!(*pte & (1ULL << 7)))
            f |= VMM_WRITE;
        if (!(*pte & PTE_UXN))
            f |= VMM_EXEC;
#endif
        *flags_out = f;
    }
    return 0;
}

int vmm_protect_range(uint64_t cr3_phys, uint64_t vaddr, size_t len, uint32_t flags) {
    if (vaddr & 0xFFFULL)
        return -1;
    uint64_t end = vaddr + len;
    if (end < vaddr)
        return -1;
    for (uint64_t v = vaddr; v < end; v += PAGE_SIZE) {
        uint64_t pa = 0;
        if (vmm_query(cr3_phys, v, &pa, NULL) != 0)
            return -1;
        if (vmm_map_page_flags(cr3_phys, v, pa, flags) != 0)
            return -1;
        vmm_invlpg(v);
    }
    return 0;
}

void vmm_switch(uint64_t cr3_phys) {
#ifdef PEAK_HOST_TEST
    (void)cr3_phys;
    return;
#endif
#if defined(__aarch64__)
    __asm__ volatile ("msr ttbr0_el1, %0" : : "r"(cr3_phys) : "memory");
    __asm__ volatile ("msr ttbr1_el1, %0" : : "r"(cr3_phys) : "memory");
    __asm__ volatile ("isb");
#else
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3_phys) : "memory");
#endif
}

static void free_pt_level(uint64_t *table, int level) {
    if (!table)
        return;
    for (int i = 0; i < 512; i++) {
        if (!(table[i] & 1))
            continue;
        uint64_t phys = table[i] & ~0xFFFULL;
        if (level > 1) {
            uint64_t *child = (uint64_t *)vmm_phys_to_virt(phys);
            free_pt_level(child, level - 1);
        }
        pmm_free((void *)phys);
        table[i] = 0;
    }
}

void vmm_destroy_address_space(uint64_t cr3_phys) {
    if (!cr3_phys || cr3_phys == kernel_cr3)
        return;
    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(cr3_phys);
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & 1))
            continue;
        uint64_t phys = pml4[i] & ~0xFFFULL;
        uint64_t *pdpt = (uint64_t *)vmm_phys_to_virt(phys);
        free_pt_level(pdpt, 3);
        pmm_free((void *)phys);
        pml4[i] = 0;
    }
    pmm_free((void *)cr3_phys);
}

#define USER_PTR_MAX 0x00007FFFFFFFFFFFULL

int access_ok(const void *user_ptr, size_t len) {
    uint64_t p = (uint64_t)user_ptr;
    if (!user_ptr && len)
        return 0;
    if (p > USER_PTR_MAX)
        return 0;
    if (len && p + len - 1 < p)
        return 0; /* overflow */
    if (len && p + len - 1 > USER_PTR_MAX)
        return 0;
    return 1;
}

int access_ok_write(const void *user_ptr, size_t len) {
    if (!access_ok(user_ptr, len))
        return 0;
    uint64_t cr3 = vmm_current_cr3();
    uint64_t start = (uint64_t)user_ptr & ~0xFFFULL;
    uint64_t end = ((uint64_t)user_ptr + (len ? len - 1 : 0)) | 0xFFFULL;
    for (uint64_t v = start; v <= end; v += PAGE_SIZE) {
        uint32_t f = 0;
        if (vmm_query(cr3, v, NULL, &f) != 0)
            return 0;
        if (!(f & VMM_USER) || !(f & VMM_WRITE))
            return 0;
    }
    return 1;
}

int copy_from_user(void *kdst, const void *user_src, size_t len) {
    if (!access_ok(user_src, len))
        return -1;
    memcpy(kdst, user_src, len);
    return 0;
}

int copy_to_user(void *user_dst, const void *ksrc, size_t len) {
    if (!access_ok(user_dst, len))
        return -1;
    memcpy(user_dst, ksrc, len);
    return 0;
}

int copyinstr_from_user(char *kdst, const void *user_src, size_t max_len) {
    if (!kdst || max_len == 0 || !access_ok(user_src, 1))
        return -1;
    const char *s = (const char *)user_src;
    size_t i = 0;
    for (; i + 1 < max_len; i++) {
        if (!access_ok(s + i, 1))
            return -1;
        kdst[i] = s[i];
        if (kdst[i] == '\0')
            return 0;
    }
    kdst[i] = '\0';
    return -1; /* not terminated */
}
