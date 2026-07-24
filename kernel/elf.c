#include "elf.h"
#include "ubin.h"
#include "vfs.h"
#include "heap.h"
#include "vmm.h"
#if defined(__x86_64__)
#include "gdt.h"
#endif
#include "pmm.h"
#include "console.h"
#include "serial.h"
#include "util.h"
#include "random.h"

#define PF_X 1
#define PF_W 2
#define PF_R 4

struct elf64_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

#define PT_LOAD 1
#define EM_X86_64 62
#define EM_AARCH64 183
#ifndef PEAK_ELF_MACHINE
#if defined(__aarch64__)
#define PEAK_ELF_MACHINE EM_AARCH64
#else
#define PEAK_ELF_MACHINE EM_X86_64
#endif
#endif

static int user_exit_code;
static int user_exited;
static uint64_t user_return_rsp;
static uint64_t user_return_rip;
static uint64_t user_cr3;

void proc_notify_exit(int code) {
    user_exited = 1;
    user_exit_code = code;
}

/* Called from SYS_exit — restore kernel context after enter_user. */
void proc_finish_exit(int code) {
    user_exited = 1;
    user_exit_code = code;
    vmm_switch(vmm_kernel_cr3());
    if (user_cr3) {
        vmm_destroy_address_space(user_cr3);
        user_cr3 = 0;
    }
    if (!user_return_rip)
        return;
#if defined(__aarch64__)
    __asm__ volatile (
        "mov sp, %[rsp]\n"
        "br %[rip]\n"
        :
        : [rsp] "r"(user_return_rsp), [rip] "r"(user_return_rip)
        : "memory"
    );
#else
    __asm__ volatile (
        "mov %[rsp], %%rsp\n"
        "jmp *%[rip]\n"
        :
        : [rsp] "r"(user_return_rsp), [rip] "r"(user_return_rip)
        : "memory"
    );
#endif
}

int elf_load(const uint8_t *file, size_t len, struct elf_image *out) {
    if (len < sizeof(struct elf64_ehdr))
        return -1;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)file;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return -1;
    if (eh->e_ident[4] != 2) /* ELFCLASS64 */
        return -1;
    if (eh->e_machine != PEAK_ELF_MACHINE)
        return -1;
    if (eh->e_phentsize != sizeof(struct elf64_phdr))
        return -1;
    if (eh->e_phnum == 0 || eh->e_phnum > 64)
        return -1;
    uint64_t ph_end = eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize;
    if (eh->e_phoff >= len || ph_end > len || ph_end < eh->e_phoff)
        return -1;
    /* Entry must be in user half. */
    if (eh->e_entry == 0 || eh->e_entry >= 0x0000800000000000ULL)
        return -1;
    out->entry = eh->e_entry;
    out->load_base = 0;
    out->load_size = 0;
    out->image = NULL;
    return 0;
}

static int phdr_valid(const struct elf64_phdr *ph, size_t flen) {
    if (ph->p_type != PT_LOAD)
        return 1;
    if (ph->p_memsz < ph->p_filesz)
        return 0;
    if (ph->p_offset > flen || ph->p_filesz > flen - ph->p_offset)
        return 0;
    /* Reject W+X. */
    if ((ph->p_flags & PF_W) && (ph->p_flags & PF_X))
        return 0;
    /* Null page / low addresses. */
    if (ph->p_vaddr < 0x1000)
        return 0;
    /* User canonical addresses only; no kernel map. */
    if (ph->p_vaddr >= 0x0000800000000000ULL)
        return 0;
    if (ph->p_memsz > 0 && ph->p_vaddr + ph->p_memsz < ph->p_vaddr)
        return 0; /* overflow */
    if (ph->p_vaddr + ph->p_memsz > 0x0000800000000000ULL)
        return 0;
    if (ph->p_align > 1) {
        uint64_t a = ph->p_align;
        if (a & (a - 1))
            return 0; /* not power of two */
    }
    return 1;
}

static uint32_t phdr_vmm_flags(uint32_t p_flags) {
    uint32_t f = VMM_PRESENT | VMM_USER;
    if (p_flags & PF_W)
        f |= VMM_WRITE;
    if (p_flags & PF_X)
        f |= VMM_EXEC;
    /* Readable implied by present user page. */
    return f;
}

static void enter_user(uint64_t entry, uint64_t stack) {
#if defined(__aarch64__)
    /* Drop to EL0t via eret; SYS_exit restores kernel SP/PC via proc_finish_exit. */
    uint64_t spsr = 0; /* M[3:0]=0 → EL0t */
    __asm__ volatile (
        "adr x9, 1f\n"
        "str x9, %[save_rip]\n"
        "mov x9, sp\n"
        "str x9, %[save_rsp]\n"
        "msr elr_el1, %[entry]\n"
        "msr spsr_el1, %[spsr]\n"
        "msr sp_el0, %[usp]\n"
        "mov x0, xzr\n"
        "mov x1, xzr\n"
        "mov x2, xzr\n"
        "mov x3, xzr\n"
        "mov x8, xzr\n"
        "eret\n"
        "1:\n"
        : [save_rip] "=m"(user_return_rip), [save_rsp] "=m"(user_return_rsp)
        : [entry] "r"(entry), [usp] "r"(stack), [spsr] "r"(spsr)
        : "x0", "x1", "x2", "x3", "x8", "x9", "memory"
    );
#else
    uint64_t user_ss = GDT_USER_DATA | 3;
    uint64_t user_cs = GDT_USER_CODE | 3;
    __asm__ volatile (
        "leaq 1f(%%rip), %%r11\n"
        "movq %%r11, %[save_rip]\n"
        "movq %%rsp, %[save_rsp]\n"
        "mov %[ss], %%rax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "pushq %[ss]\n"
        "pushq %[usp]\n"
        "pushq $0x202\n"
        "pushq %[cs]\n"
        "pushq %[entry]\n"
        "iretq\n"
        "1:\n"
        : [save_rip] "=m"(user_return_rip), [save_rsp] "=m"(user_return_rsp)
        : [ss] "r"(user_ss), [usp] "r"(stack), [cs] "r"(user_cs), [entry] "r"(entry)
        : "rax", "r11", "memory"
    );
#endif
}

static int exec_elf(const char *path) {
    uint8_t *file = kmalloc(256 * 1024);
    if (!file)
        return -1;
    size_t flen = 0;
    if (vfs_read_file(path, file, 256 * 1024, &flen) != 0) {
        kfree(file);
        return -999;
    }
    struct elf_image img;
    if (elf_load(file, flen, &img) != 0) {
        kfree(file);
        return -999;
    }

    uint64_t cr3 = vmm_create_address_space();
    if (!cr3) {
        kfree(file);
        return -1;
    }

    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)file;
    const struct elf64_phdr *ph = (const struct elf64_phdr *)(file + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (!phdr_valid(&ph[i], flen)) {
            vmm_destroy_address_space(cr3);
            kfree(file);
            return -999;
        }
        if (ph[i].p_type != PT_LOAD)
            continue;
        uint64_t v = ph[i].p_vaddr & ~0xFFFULL;
        uint64_t end = ph[i].p_vaddr + ph[i].p_memsz;
        for (; v < end; v += 4096) {
            void *pg = pmm_alloc();
            if (!pg) {
                vmm_destroy_address_space(cr3);
                kfree(file);
                return -1;
            }
            memset(vmm_phys_to_virt((uint64_t)pg), 0, 4096);
            uint64_t seg_start = ph[i].p_vaddr;
            for (uint64_t b = 0; b < 4096; b++) {
                uint64_t va = v + b;
                if (va >= seg_start && va < seg_start + ph[i].p_filesz) {
                    uint64_t src = ph[i].p_offset + (va - seg_start);
                    if (src < flen)
                        ((uint8_t *)vmm_phys_to_virt((uint64_t)pg))[b] = file[src];
                }
            }
            if (vmm_map_page_flags(cr3, v, (uint64_t)pg, phdr_vmm_flags(ph[i].p_flags)) != 0) {
                vmm_destroy_address_space(cr3);
                kfree(file);
                return -1;
            }
        }
    }

    /* ASLR: randomize stack top within a 256-page window; leave a guard page. */
    uint64_t stack_top = USER_STACK_TOP;
    uint8_t slide_b[2];
    if (random_get(RANDOM_DOMAIN_ASLR, slide_b, sizeof(slide_b)) == 0) {
        uint64_t slide = ((uint64_t)slide_b[0] | ((uint64_t)slide_b[1] << 8)) & 0xFFULL;
        stack_top -= slide * PAGE_SIZE;
        memzero_explicit(slide_b, sizeof(slide_b));
    }
    /* Guard page below stack (unmapped). */
    uint64_t stack_base = stack_top - 16ULL * PAGE_SIZE;
    for (int i = 0; i < 16; i++) {
        void *pg = pmm_alloc();
        if (!pg) {
            vmm_destroy_address_space(cr3);
            kfree(file);
            return -1;
        }
        memset(vmm_phys_to_virt((uint64_t)pg), 0, 4096);
        if (vmm_map_page_flags(cr3, stack_base + (uint64_t)i * PAGE_SIZE, (uint64_t)pg,
                               VMM_PROT_RW) != 0) {
            vmm_destroy_address_space(cr3);
            kfree(file);
            return -1;
        }
    }
    (void)stack_base;

    kfree(file);
    user_exited = 0;
    user_cr3 = cr3;
    vmm_switch(cr3);
    enter_user(img.entry, stack_top - 16);
    /* SYS_exit path already freed via proc_finish_exit. */
    if (user_cr3) {
        vmm_switch(vmm_kernel_cr3());
        vmm_destroy_address_space(user_cr3);
        user_cr3 = 0;
    }
    return user_exit_code;
}

int proc_exec(const char *path, int argc, char **argv) {
    int br = ubin_run(path, argc, argv);
    if (br != -999)
        return br;
    return exec_elf(path);
}
