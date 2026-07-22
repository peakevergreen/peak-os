#include "boot_elf.h"
#include "boot_util.h"

#define EI_MAG0 0
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62
#define EM_AARCH64 183
#ifndef PEAK_ELF_MACHINE
#define PEAK_ELF_MACHINE EM_X86_64
#endif
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2

struct elf64_ehdr {
    uint8_t e_ident[16];
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
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

static int phdr_ok(const struct elf64_phdr *ph, size_t img_size) {
    if (ph->p_type != PT_LOAD)
        return 1;
    /* Reject W+X — same policy as kernel user ELF loader. */
    if ((ph->p_flags & PF_W) && (ph->p_flags & PF_X))
        return 0;
    if (ph->p_memsz < ph->p_filesz)
        return 0;
    if (ph->p_offset > img_size || ph->p_filesz > img_size - ph->p_offset)
        return 0;
    if (ph->p_vaddr & 0xFFFull)
        return 0;
    if (ph->p_align && (ph->p_vaddr & (ph->p_align - 1)))
        return 0;
    return 1;
}

int boot_elf_load(const struct boot_elf_image *img,
                  uint64_t (*alloc_pages)(size_t n),
                  int (*map_page)(uint64_t virt, uint64_t phys, int writable),
                  struct boot_loaded_kernel *out) {
    if (!img || !img->data || !alloc_pages || !map_page || !out)
        return -1;
    if (img->size < sizeof(struct elf64_ehdr))
        return -1;

    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)img->data;
    if (eh->e_ident[EI_MAG0] != ELFMAG0 ||
        eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 ||
        eh->e_ident[3] != ELFMAG3)
        return -1;
    if (eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB)
        return -1;
    if (eh->e_ident[6] != EV_CURRENT || eh->e_version != EV_CURRENT)
        return -1;
    if (eh->e_machine != PEAK_ELF_MACHINE)
        return -1;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)
        return -1;
    if (eh->e_phentsize != sizeof(struct elf64_phdr) || eh->e_phnum == 0)
        return -1;
    if (eh->e_phoff > img->size ||
        (uint64_t)eh->e_phnum * eh->e_phentsize > img->size - eh->e_phoff)
        return -1;

    uint64_t min_virt = ~0ULL;
    uint64_t max_virt = 0;
    const struct elf64_phdr *ph =
        (const struct elf64_phdr *)(img->data + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (!phdr_ok(&ph[i], img->size))
            return -1;
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        if (ph[i].p_vaddr < min_virt)
            min_virt = ph[i].p_vaddr;
        uint64_t end = ph[i].p_vaddr + ph[i].p_memsz;
        if (end > max_virt)
            max_virt = end;
    }
    if (min_virt == ~0ULL || max_virt <= min_virt)
        return -1;

    uint64_t span = (max_virt - min_virt + 0xFFF) & ~0xFFFULL;
    size_t pages = (size_t)(span / 0x1000);
    uint64_t phys_base = alloc_pages(pages);
    if (!phys_base)
        return -1;
    boot_memset((void *)(uintptr_t)phys_base, 0, (size_t)span);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        uint64_t off = ph[i].p_vaddr - min_virt;
        uint64_t dest = phys_base + off;
        if (ph[i].p_filesz)
            boot_memcpy((void *)(uintptr_t)dest,
                        img->data + ph[i].p_offset,
                        (size_t)ph[i].p_filesz);
        /* p_memsz - p_filesz already zeroed */

        int writable = (ph[i].p_flags & PF_W) ? 1 : 0;
        uint64_t v = ph[i].p_vaddr & ~0xFFFULL;
        uint64_t p = (dest) & ~0xFFFULL;
        uint64_t end = ph[i].p_vaddr + ph[i].p_memsz;
        while (v < end) {
            if (map_page(v, p, writable) != 0)
                return -1;
            v += 0x1000;
            p += 0x1000;
        }
    }

    out->entry_virt = eh->e_entry;
    out->phys_base = phys_base;
    out->phys_size = span;
    return 0;
}
