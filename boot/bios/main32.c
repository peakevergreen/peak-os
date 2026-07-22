#include "bios_call.h"
#include "boot_util.h"
#include "boot_elf.h"
#include "boot_paging.h"
#include "peak_boot.h"
#include "peak_conf.h"

#define KERNEL_LOAD_PHYS 0x3000000UL
#define KERNEL_LOAD_MAX  (8UL * 1024UL * 1024UL)
#define PT_ARENA_PHYS    0x1000000UL /* 16 MiB arena: page tables + kernel image */
#define PT_ARENA_SIZE    (32UL * 1024UL * 1024UL)
#define BOOTINFO_PHYS    0xF000UL
#define STACK_PHYS       0x8E000UL

extern void bios_enter_long_mode(void);
extern uint64_t params_pml4, params_entry, params_stack, params_bootinfo;

int iso_load_file(const char *path, void *dest, uint32_t dest_cap, uint32_t *out_size);
uint32_t bios_e820(struct peak_mmap_entry *out, uint32_t max_entries);
int bios_vbe_init(struct peak_framebuffer_info *fb, uint16_t want_w, uint16_t want_h);

static uint64_t pt_alloc_next;
static uint64_t pt_alloc_end;
static uint64_t g_pml4;

static uint64_t alloc_pages(size_t n) {
    uint64_t need = (uint64_t)n * BOOT_PAGE_SIZE;
    if (pt_alloc_next + need > pt_alloc_end)
        return 0;
    uint64_t p = pt_alloc_next;
    pt_alloc_next += need;
    boot_memset((void *)(uintptr_t)p, 0, (size_t)need);
    return p;
}

static int map_page_cb(uint64_t virt, uint64_t phys, int writable) {
    struct boot_page_allocator a = { .alloc_pages = alloc_pages };
    return boot_map_page(g_pml4, virt, phys, writable, &a);
}

void bios_main32(uint32_t drive) {
    (void)drive;
    boot_serial_init();
    boot_serial_write_str("Peak BIOS loader\n");

    struct peak_bootinfo *info = (struct peak_bootinfo *)(uintptr_t)BOOTINFO_PHYS;
    boot_memset(info, 0, sizeof(*info));

    struct peak_loader_conf conf;
    peak_conf_defaults(&conf);
    {
        /* ISO9660 Level 1 truncates peak.conf → PEAK.CON;1 */
        static char conf_buf[2048];
        uint32_t csz = 0;
        if (iso_load_file("/boot/PEAK.CON", conf_buf, sizeof(conf_buf) - 1, &csz) == 0 ||
            iso_load_file("/BOOT/PEAK.CON", conf_buf, sizeof(conf_buf) - 1, &csz) == 0 ||
            iso_load_file("/boot/peak.conf", conf_buf, sizeof(conf_buf) - 1, &csz) == 0 ||
            iso_load_file("/boot/PEAK.CFG", conf_buf, sizeof(conf_buf) - 1, &csz) == 0) {
            conf_buf[csz] = '\0';
            peak_conf_parse(conf_buf, csz, &conf);
            boot_serial_write_str("bios: peak.conf loaded\n");
        } else {
            boot_serial_write_str("bios: peak.conf missing, defaults\n");
        }
    }
    info->net = conf.net;

    info->mmap_count = bios_e820(info->mmap, PEAK_MMAP_MAX);
    if (info->mmap_count == 0) {
        boot_serial_write_str("bios: E820 failed\n");
        boot_hang();
    }
    boot_serial_write_str("bios: E820 ok\n");

    if (bios_vbe_init(&info->fb, conf.width, conf.height) != 0)
        boot_hang();
    boot_serial_write_str("bios: VBE ok\n");

    if (info->mmap_count < PEAK_MMAP_MAX) {
        uint64_t fb_size = info->fb.pitch * info->fb.height;
        info->mmap[info->mmap_count++] = (struct peak_mmap_entry){
            .base = info->fb.addr,
            .length = fb_size,
            .type = PEAK_MMAP_FRAMEBUFFER
        };
    }

    uint32_t ksz = 0;
    if (iso_load_file("/BOOT/KERNEL.ELF", (void *)KERNEL_LOAD_PHYS,
                      (uint32_t)KERNEL_LOAD_MAX, &ksz) != 0 &&
        iso_load_file("/boot/kernel.elf", (void *)KERNEL_LOAD_PHYS,
                      (uint32_t)KERNEL_LOAD_MAX, &ksz) != 0) {
        boot_serial_write_str("bios: kernel not found on ISO\n");
        boot_hang();
    }
    boot_serial_write_str("bios: kernel loaded\n");

    pt_alloc_next = PT_ARENA_PHYS;
    pt_alloc_end = PT_ARENA_PHYS + PT_ARENA_SIZE;
    struct boot_page_allocator a = { .alloc_pages = alloc_pages };
    if (boot_paging_init(&a, 0x100000000ULL, &g_pml4) != 0) {
        boot_serial_write_str("bios: paging init failed\n");
        boot_hang();
    }

    struct boot_elf_image img = {
        .data = (const uint8_t *)(uintptr_t)KERNEL_LOAD_PHYS,
        .size = ksz,
    };
    struct boot_loaded_kernel k;
    if (boot_elf_load(&img, alloc_pages, map_page_cb, &k) != 0) {
        boot_serial_write_str("bios: ELF load failed\n");
        boot_hang();
    }
    boot_serial_write_str("bios: ELF mapped\n");

    info->magic = PEAK_BOOT_MAGIC;
    info->version = PEAK_BOOT_VERSION;
    info->flags = PEAK_BOOT_FLAG_HAS_FB;
    if (conf.smoke_persist)
        info->flags |= PEAK_BOOT_FLAG_SMOKE_PERSIST;
    info->dtb_phys = 0;
    info->dtb_size = 0;
    info->hhdm_offset = PEAK_HHDM_OFFSET;
    info->kernel_phys_base = k.phys_base;
    info->kernel_phys_size = k.phys_size;
    info->stack_phys = STACK_PHYS;
    info->stack_size = 0x2000;
    info->fb.addr = info->fb.addr + PEAK_HHDM_OFFSET;
    info->entropy_len = 0;
    info->kaslr_slide_pages = 0;
    /* Collect boot entropy: RDRAND when available, else timing jitter (weak). */
    {
        uint32_t a = 0, b = 0, c = 0, d = 0;
        __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
        int have_rdrand = (c >> 30) & 1;
        uint16_t elen = 0;
        int trusted = 0;
        if (have_rdrand) {
            for (int i = 0; i < 8 && elen + 4 <= PEAK_BOOT_ENTROPY_MAX; i++) {
                uint32_t v = 0;
                uint8_t ok = 0;
                __asm__ volatile ("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
                if (!ok)
                    break;
                boot_memcpy(info->entropy + elen, &v, 4);
                elen = (uint16_t)(elen + 4);
                trusted = 1;
            }
        }
        /* Always mix PIT/TSC-ish timing as supplemental. */
        for (int i = 0; i < 16 && elen < PEAK_BOOT_ENTROPY_MAX; i++) {
            uint32_t t = 0;
            for (volatile int j = 0; j < 200 + i * 17; j++)
                t ^= (uint32_t)j;
            __asm__ volatile ("rdtsc" : "=a"(a), "=d"(d));
            t ^= a ^ d;
            info->entropy[elen++] = (uint8_t)(t ^ (t >> 8));
        }
        info->entropy_len = elen;
        if (trusted)
            info->flags |= PEAK_BOOT_FLAG_ENTROPY_OK;
        else
            info->flags |= PEAK_BOOT_FLAG_ENTROPY_WEAK;
    }

    if (info->mmap_count < PEAK_MMAP_MAX) {
        info->mmap[info->mmap_count++] = (struct peak_mmap_entry){
            .base = k.phys_base,
            .length = k.phys_size,
            .type = PEAK_MMAP_KERNEL
        };
    }
    if (info->mmap_count < PEAK_MMAP_MAX) {
        info->mmap[info->mmap_count++] = (struct peak_mmap_entry){
            .base = PT_ARENA_PHYS,
            .length = PT_ARENA_SIZE,
            .type = PEAK_MMAP_BOOTLOADER
        };
    }

    params_pml4 = g_pml4;
    params_entry = k.entry_virt;
    params_stack = PEAK_HHDM_OFFSET + STACK_PHYS + 0x2000;
    params_bootinfo = PEAK_HHDM_OFFSET + BOOTINFO_PHYS;

    boot_serial_write_str("bios: entering kernel\n");
    bios_enter_long_mode();
    boot_hang();
}
