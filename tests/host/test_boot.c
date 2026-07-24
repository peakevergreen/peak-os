#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "peak_boot.h"
#include "boot_elf.h"
#include "boot_util.h"
#include "boot_sha256.h"
#include "boot_verify.h"
#include "peak_conf.h"

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

/* Minimal ELF64 with one PT_LOAD; p_flags defaults to R+X (5). */
static uint8_t *make_elf(size_t *out_size, uint32_t p_flags) {
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

static int try_load(uint8_t *elf, size_t esz) {
    struct boot_elf_image img = { .data = elf, .size = esz };
    struct boot_loaded_kernel k;
    arena_off = 0;
    map_count = 0;
    return boot_elf_load(&img, alloc_pages, map_page, &k);
}

/* Adversarial mutations — must reject without crashing. */
static int fuzz_mutations(unsigned seed, int iters) {
    unsigned s = seed ? seed : 1u;
    int bad_accept = 0;
    for (int i = 0; i < iters; i++) {
        size_t esz = 0;
        uint8_t *elf = make_elf(&esz, 5);
        s = s * 1103515245u + 12345u;
        unsigned kind = s % 8;
        switch (kind) {
        case 0: /* truncate */
            esz = 16 + (s % 48);
            break;
        case 1: /* W+X */
            {
                uint32_t flags = 7;
                memcpy(elf + 64 + 4, &flags, 4);
            }
            break;
        case 2: /* bad magic */
            elf[1] = 'X';
            break;
        case 3: /* memsz < filesz */
            {
                uint64_t fs = 4096, ms = 16;
                memcpy(elf + 64 + 32, &fs, 8);
                memcpy(elf + 64 + 40, &ms, 8);
            }
            break;
        case 4: /* unaligned vaddr */
            {
                uint64_t v = 0xffffffff80001001ULL;
                memcpy(elf + 64 + 16, &v, 8);
                memcpy(elf + 64 + 24, &v, 8);
            }
            break;
        case 5: /* huge phoff */
            {
                uint64_t phoff = 0xffffff00ULL;
                memcpy(elf + 32, &phoff, 8);
            }
            break;
        case 6: /* zero phnum */
            {
                uint16_t z = 0;
                memcpy(elf + 56, &z, 2);
            }
            break;
        default: /* corrupt e_machine */
            elf[18] = 0xff;
            break;
        }
        if (try_load(elf, esz) == 0)
            bad_accept++;
        free(elf);
    }
    return bad_accept;
}

int main(int argc, char **argv) {
    int fuzz_iters = 0;
    unsigned fuzz_seed = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) {
            fuzz_iters = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            fuzz_seed = (unsigned)atoi(argv[++i]);
        }
    }

    failures = 0;
    expect(PEAK_BOOT_VERSION == 4, "boot ABI version");
    expect(sizeof(struct peak_bootinfo) > 64, "bootinfo size");

    size_t esz = 0;
    uint8_t *elf = make_elf(&esz, 5);
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

    /* Reject W+X PHDR */
    elf = make_elf(&esz, 7); /* R|W|X */
    expect(try_load(elf, esz) != 0, "reject W+X phdr");
    free(elf);

    char a[] = "BOOT";
    char b[] = "boot";
    expect(boot_strncasecmp(a, b, 4) == 0, "strncasecmp");

    /* SHA256 empty string vector */
    {
        struct boot_sha256_ctx ctx;
        boot_sha256_init(&ctx);
        uint8_t dig[BOOT_SHA256_DIGEST_LEN];
        boot_sha256_final(&ctx, dig);
        static const uint8_t empty_ref[32] = {
            0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
            0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
            0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
        };
        expect(memcmp(dig, empty_ref, 32) == 0, "sha256 empty");
    }

    /* boot_verify_kernel: optional skip, required fail-closed, manifest match */
    {
        struct peak_loader_conf conf;
        peak_conf_defaults(&conf);
        const char payload[] = "kernel-bytes-for-verify";
        expect(boot_verify_kernel(&conf, (const uint8_t *)payload, sizeof(payload) - 1,
                                  NULL, 0) == 0,
               "verify skips when not required");

        conf.verify_required = 1;
        expect(boot_verify_kernel(&conf, (const uint8_t *)payload, sizeof(payload) - 1,
                                  NULL, 0) != 0,
               "verify required without digest fails");

        struct boot_sha256_ctx ctx;
        boot_sha256_init(&ctx);
        boot_sha256_update(&ctx, payload, sizeof(payload) - 1);
        uint8_t dig[BOOT_SHA256_DIGEST_LEN];
        boot_sha256_final(&ctx, dig);
        char hex[BOOT_SHA256_DIGEST_LEN * 2 + 1];
        static const char k[] = "0123456789abcdef";
        for (int i = 0; i < BOOT_SHA256_DIGEST_LEN; i++) {
            hex[i * 2] = k[dig[i] >> 4];
            hex[i * 2 + 1] = k[dig[i] & 0xF];
        }
        hex[BOOT_SHA256_DIGEST_LEN * 2] = '\0';
        strcpy(conf.kernel_sha256, hex);
        expect(boot_verify_kernel(&conf, (const uint8_t *)payload, sizeof(payload) - 1,
                                  NULL, 0) == 0,
               "verify conf digest ok");

        char manifest[128];
        snprintf(manifest, sizeof(manifest), "%s  boot/kernel.elf\n", hex);
        conf.kernel_sha256[0] = '\0';
        conf.verify_required = 1;
        expect(boot_verify_kernel(&conf, (const uint8_t *)payload, sizeof(payload) - 1,
                                  manifest, strlen(manifest)) == 0,
               "verify manifest digest ok");
    }

    if (fuzz_iters > 0) {
        int bad_accept = fuzz_mutations(fuzz_seed, fuzz_iters);
        expect(bad_accept == 0, "fuzz mutations all rejected");
        printf("fuzz: %d mutations (seed=%u)\n", fuzz_iters, fuzz_seed);
    }

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all boot host tests passed\n");
    return 0;
}
