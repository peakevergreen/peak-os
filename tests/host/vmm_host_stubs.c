/*
 * Host stubs for linking kernel/vmm.c under PEAK_HOST_TEST.
 * Uses hhdm=0 so malloc page frames serve as both phys and virt.
 */
#include "pmm.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

uint64_t vmm_host_test_cr3;

void vmm_host_test_set_cr3(uint64_t cr3_phys) {
    vmm_host_test_cr3 = cr3_phys;
}

void pmm_init(struct peak_bootinfo *info) {
    (void)info;
}

void *pmm_alloc(void) {
    void *p = aligned_alloc(4096, 4096);
    if (p)
        memset(p, 0, 4096);
    return p;
}

void *pmm_alloc_pages(size_t n) {
    if (!n)
        return NULL;
    return aligned_alloc(4096, n * 4096);
}

void pmm_free(void *phys) {
    free(phys);
}

void pmm_free_n(void *phys, size_t n) {
    (void)n;
    free(phys);
}

uint64_t pmm_total_pages(void) {
    return 0;
}

uint64_t pmm_free_pages(void) {
    return 0;
}

uint64_t pmm_hhdm(void) {
    return 0;
}
