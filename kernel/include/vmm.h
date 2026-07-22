#ifndef PEAK_VMM_H
#define PEAK_VMM_H

#include "types.h"

#define PAGE_SIZE 4096ULL
#define USER_STACK_TOP 0x7FFFFFF000ULL
#define USER_HEAP_BASE 0x500000ULL

/* Explicit mapping flags (software; translated to PTE bits). */
#define VMM_PRESENT   (1u << 0)
#define VMM_WRITE     (1u << 1)
#define VMM_USER      (1u << 2)
#define VMM_EXEC      (1u << 3)
#define VMM_GLOBAL    (1u << 4)
#define VMM_NOCACHE   (1u << 5)

#define VMM_PROT_RX   (VMM_PRESENT | VMM_USER | VMM_EXEC)
#define VMM_PROT_RW   (VMM_PRESENT | VMM_USER | VMM_WRITE)
#define VMM_PROT_R    (VMM_PRESENT | VMM_USER)
#define VMM_PROT_RWX  (VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_EXEC) /* rejected */

void vmm_init(uint64_t hhdm_offset);
void vmm_set_kernel_phys_base(uint64_t phys);
void vmm_enable_nx(void);
uint64_t vmm_hhdm(void);
void *vmm_phys_to_virt(uint64_t phys);
uint64_t vmm_virt_to_phys(void *virt);

uint64_t vmm_create_address_space(void);
void vmm_destroy_address_space(uint64_t cr3_phys);
int vmm_map_page_flags(uint64_t cr3_phys, uint64_t vaddr, uint64_t paddr, uint32_t flags);
int vmm_map_page(uint64_t cr3_phys, uint64_t vaddr, uint64_t paddr, int user_writable);
int vmm_unmap_page(uint64_t cr3_phys, uint64_t vaddr);
int vmm_query(uint64_t cr3_phys, uint64_t vaddr, uint64_t *paddr_out, uint32_t *flags_out);
int vmm_protect_range(uint64_t cr3_phys, uint64_t vaddr, size_t len, uint32_t flags);
void vmm_switch(uint64_t cr3_phys);

uint64_t vmm_kernel_cr3(void);
uint64_t vmm_current_cr3(void);
void vmm_invlpg(uint64_t vaddr);

#ifdef PEAK_HOST_TEST
void vmm_host_test_set_cr3(uint64_t cr3_phys);
int access_ok(const void *user_ptr, size_t len);
int access_ok_write(const void *user_ptr, size_t len);
#endif

/* User <-> kernel copy helpers (validate user range against current CR3). */
int copy_from_user(void *kdst, const void *user_src, size_t len);
int copy_to_user(void *user_dst, const void *ksrc, size_t len);
int copyinstr_from_user(char *kdst, const void *user_src, size_t max_len);

#endif
