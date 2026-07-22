#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "peak_boot.h"
#include "boot_elf.h"
#include "boot_util.h"

static uint8_t arena[1024 * 1024];
static size_t arena_off;
static uint64_t maps[256][3];
static int map_count;

static uint64_t alloc_pages(size_t n) {
    size_t need = n * 4096;
    if (arena_off + need > sizeof(arena))
        return 0;
    uint64_t p = (uint64_t)(uintptr_t)(arena + arena_off);
    arena_off += need;
    memset((void *)(uintptr_t)p, 0, need);
    return p;
}

static int map_page(uint64_t virt, uint64_t phys, int writable) {
    if (map_count >= 256)
        return -1;
    maps[map_count][0] = virt;
    maps[map_count][1] = phys;
    maps[map_count][2] = (uint64_t)writable;
    map_count++;
    return 0;
}

/* Minimal ELF64 with one PT_LOAD */
static uint8_t *make_elf(size_t *out_size) {
    size_t sz = 4096;
    uint8_t *buf = calloc(1, sz);
    /* e_ident */
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 2; buf[5] = 1; buf[6] = 1;
    /* e_type ET_EXEC, e_machine EM_X86_64 */
    buf[16] = 2; buf[17] = 0;
    buf[18] = 62; buf[19] = 0;
    buf[20] = 1; /* version */
    /* e_entry */
    uint64_t entry = 0xffffffff80001000ULL;
    memcpy(buf + 24, &entry, 8);
    uint64_t phoff = 64;
    memcpy(buf + 32, &phoff, 8);
    uint16_t ehsize = 64, phentsize = 56, phnum = 1;
    memcpy(buf + 52, &ehsize, 2);
    memcpy(buf + 54, &phentsize, 2);
    memcpy(buf + 56, &phnum, 2);

    uint8_t *ph = buf + 64;
    uint32_t p_type = 1; /* PT_LOAD */
    uint32_t p_flags = 5;
    uint64_t p_offset = 0x200;
    uint64_t p_vaddr = 0xffffffff80001000ULL;
    uint64_t p_filesz = 16;
    uint64_t p_memsz = 4096;
    uint64_t p_align = 0x1000;
    memcpy(ph + 0, &p_type, 4);
    memcpy(ph + 4, &p_flags, 4);
    memcpy(ph + 8, &p_offset, 8);
    memcpy(ph + 16, &p_vaddr, 8);
    memcpy(ph + 24, &p_vaddr, 8);
    memcpy(ph + 32, &p_filesz, 8);
    memcpy(ph + 40, &p_memsz, 8);
    memcpy(ph + 48, &p_align, 8);
    memcpy(buf + 0x200, "HELLOWORLD123456", 16);
    *out_size = sz;
    return buf;
}

static int failures;

static void expect(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    } else {
        printf("ok: %s\n", msg);
    }
}

int main(void) {
    failures = 0;
    expect(PEAK_BOOT_VERSION == 4, "boot ABI version");
    expect(sizeof(struct peak_bootinfo) > 64, "bootinfo size");

    size_t esz = 0;
    uint8_t *elf = make_elf(&esz);
    struct boot_elf_image img = { .data = elf, .size = esz };
    struct boot_loaded_kernel k;
    arena_off = 0;
    map_count = 0;
    expect(boot_elf_load(&img, alloc_pages, map_page, &k) == 0, "elf load");
    expect(k.entry_virt == 0xffffffff80001000ULL, "elf entry");
    expect(k.phys_size >= 4096, "elf span");
    expect(map_count >= 1, "mapped pages");
    free(elf);

    /* Reject truncated ELF */
    uint8_t bad[16] = {0};
    img.data = bad;
    img.size = sizeof(bad);
    expect(boot_elf_load(&img, alloc_pages, map_page, &k) != 0, "reject short elf");

    char a[] = "BOOT";
    char b[] = "boot";
    expect(boot_strncasecmp(a, b, 4) == 0, "strncasecmp");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all boot host tests passed\n");
    return 0;
}
