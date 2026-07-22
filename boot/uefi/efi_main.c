#include "efi.h"
#include "boot_util.h"
#include "boot_elf.h"
#include "boot_paging.h"
#include "peak_boot.h"
#include "peak_conf.h"

static efi_system_table *ST;
static efi_boot_services *BS;
static efi_handle IH;

static uint64_t pt_next, pt_end;
static uint64_t g_pml4;

static uint64_t alloc_pages(size_t n) {
    uint64_t need = (uint64_t)n * BOOT_PAGE_SIZE;
    if (pt_next + need > pt_end)
        return 0;
    uint64_t p = pt_next;
    pt_next += need;
    boot_memset((void *)(uintptr_t)p, 0, (size_t)need);
    return p;
}

static int map_page_cb(uint64_t virt, uint64_t phys, int writable) {
    struct boot_page_allocator a = { .alloc_pages = alloc_pages };
    return boot_map_page(g_pml4, virt, phys, writable, &a);
}

static uint32_t mask_size(uint32_t mask) {
    uint32_t n = 0;
    while (mask) {
        n += mask & 1;
        mask >>= 1;
    }
    return n;
}

static uint32_t mask_shift(uint32_t mask) {
    uint32_t s = 0;
    if (!mask)
        return 0;
    while ((mask & 1) == 0) {
        s++;
        mask >>= 1;
    }
    return s;
}

static efi_status load_kernel_file(uint8_t **data_out, uint64_t *size_out) {
    efi_loaded_image_protocol *li = NULL;
    efi_status st = BS->locate_protocol((efi_guid *)&EFI_LOADED_IMAGE_GUID, NULL,
                                        (void **)&li);
    /* Prefer open_protocol on image handle */
    typedef efi_status (*open_proto_fn)(efi_handle, efi_guid *, void **, efi_handle,
                                        efi_handle, uint32_t);
    open_proto_fn open_protocol = (open_proto_fn)BS->open_protocol;
    st = open_protocol(IH, (efi_guid *)&EFI_LOADED_IMAGE_GUID, (void **)&li, IH,
                       NULL, 0x00000001 /* GET_PROTOCOL */);
    if (st != EFI_SUCCESS || !li)
        return EFI_NOT_FOUND;

    efi_simple_fs_protocol *fs = NULL;
    st = open_protocol(li->device_handle, (efi_guid *)&EFI_SIMPLE_FS_GUID,
                       (void **)&fs, IH, NULL, 0x00000001);
    if (st != EFI_SUCCESS || !fs)
        return st;

    efi_file_protocol *root = NULL;
    st = fs->open_volume(fs, &root);
    if (st != EFI_SUCCESS)
        return st;

    static efi_char16 *paths[] = {
        (efi_char16 *)L"\\EFI\\PEAK\\KERNEL.ELF",
        (efi_char16 *)L"\\BOOT\\KERNEL.ELF",
        (efi_char16 *)L"\\kernel.elf",
        NULL
    };

    efi_file_protocol *file = NULL;
    for (int i = 0; paths[i]; i++) {
        st = root->open(root, &file, paths[i], EFI_FILE_MODE_READ, 0);
        if (st == EFI_SUCCESS)
            break;
        file = NULL;
    }
    root->close(root);
    if (!file)
        return EFI_NOT_FOUND;

    /* Read in growing chunks */
    uint64_t cap = 4 * 1024 * 1024;
    uint64_t page_count = (cap + 0xFFF) / 0x1000;
    uint64_t phys = 0;
    st = BS->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_MEMORY_LOADER_DATA,
                            page_count, &phys);
    if (st != EFI_SUCCESS) {
        file->close(file);
        return st;
    }
    uint8_t *buf = (uint8_t *)(uintptr_t)phys;
    efi_uintn n = (efi_uintn)cap;
    st = file->read(file, &n, buf);
    file->close(file);
    if (st != EFI_SUCCESS)
        return st;
    *data_out = buf;
    *size_out = (uint64_t)n;
    return EFI_SUCCESS;
}

static void efi_mmap_to_peak(efi_memory_descriptor *map, efi_uintn map_size,
                             efi_uintn desc_size, struct peak_bootinfo *info) {
    efi_uintn count = map_size / desc_size;
    for (efi_uintn i = 0; i < count && info->mmap_count < PEAK_MMAP_MAX; i++) {
        efi_memory_descriptor *d =
            (efi_memory_descriptor *)((uint8_t *)map + i * desc_size);
        uint32_t t = PEAK_MMAP_RESERVED;
        if (d->type == EFI_MEMORY_CONVENTIONAL)
            t = PEAK_MMAP_USABLE;
        else if (d->type == EFI_MEMORY_ACPI_RECLAIM)
            t = PEAK_MMAP_ACPI_RECLAIM;
        else if (d->type == EFI_MEMORY_ACPI_NVS)
            t = PEAK_MMAP_ACPI_NVS;
        else if (d->type == EFI_MEMORY_UNUSABLE)
            t = PEAK_MMAP_BAD;
        else if (d->type == EFI_MEMORY_LOADER_CODE ||
                 d->type == EFI_MEMORY_LOADER_DATA ||
                 d->type == EFI_MEMORY_BS_CODE ||
                 d->type == EFI_MEMORY_BS_DATA)
            t = PEAK_MMAP_BOOTLOADER;
        info->mmap[info->mmap_count++] = (struct peak_mmap_entry){
            .base = d->physical_start,
            .length = d->number_of_pages * 0x1000ULL,
            .type = t
        };
    }
}

static int efi_load_text(efi_char16 *path, char *buf, size_t cap, size_t *out_len) {
    efi_loaded_image_protocol *li = NULL;
    typedef efi_status (*open_proto_fn)(efi_handle, efi_guid *, void **, efi_handle,
                                        efi_handle, uint32_t);
    open_proto_fn open_protocol = (open_proto_fn)BS->open_protocol;
    if (open_protocol(IH, (efi_guid *)&EFI_LOADED_IMAGE_GUID, (void **)&li, IH,
                      NULL, 0x00000001) != EFI_SUCCESS ||
        !li)
        return -1;
    efi_simple_fs_protocol *fs = NULL;
    if (open_protocol(li->device_handle, (efi_guid *)&EFI_SIMPLE_FS_GUID,
                      (void **)&fs, IH, NULL, 0x00000001) != EFI_SUCCESS ||
        !fs)
        return -1;
    efi_file_protocol *root = NULL;
    if (fs->open_volume(fs, &root) != EFI_SUCCESS)
        return -1;
    efi_file_protocol *file = NULL;
    efi_status st = root->open(root, &file, path, EFI_FILE_MODE_READ, 0);
    root->close(root);
    if (st != EFI_SUCCESS || !file)
        return -1;
    efi_uintn n = (efi_uintn)(cap > 0 ? cap - 1 : 0);
    st = file->read(file, &n, buf);
    file->close(file);
    if (st != EFI_SUCCESS)
        return -1;
    if (cap)
        buf[n < cap ? n : cap - 1] = '\0';
    if (out_len)
        *out_len = (size_t)n;
    return 0;
}

efi_status efi_main(efi_handle image_handle, efi_system_table *system_table) {
    IH = image_handle;
    ST = system_table;
    BS = ST->boot_services;

    boot_serial_init();
    boot_serial_write_str("Peak UEFI loader\n");

    struct peak_loader_conf conf;
    peak_conf_defaults(&conf);
    {
        char conf_buf[2048];
        size_t csz = 0;
        if (efi_load_text((efi_char16 *)L"\\EFI\\PEAK\\PEAK.CONF", conf_buf,
                          sizeof(conf_buf), &csz) == 0 ||
            efi_load_text((efi_char16 *)L"\\boot\\peak.conf", conf_buf,
                          sizeof(conf_buf), &csz) == 0) {
            peak_conf_parse(conf_buf, csz, &conf);
            boot_serial_write_str("uefi: peak.conf loaded\n");
        }
    }

    /* GOP */
    efi_graphics_output_protocol *gop = NULL;
    if (BS->locate_protocol((efi_guid *)&EFI_GOP_GUID, NULL, (void **)&gop) !=
            EFI_SUCCESS ||
        !gop || !gop->mode) {
        boot_serial_write_str("uefi: no GOP\n");
        return EFI_UNSUPPORTED;
    }

    typedef struct {
        uint32_t version;
        uint32_t hres;
        uint32_t vres;
        uint32_t pixel_format;
        uint32_t r, g, b, x;
        uint32_t ppsl;
    } gop_info_full;
    uint32_t chosen = gop->mode->mode;
    uint32_t score_best = 0;
    for (uint32_t m = 0; m < gop->mode->max_mode; m++) {
        efi_uintn sz = 0;
        gop_info_full *mi = NULL;
        if (gop->query_mode(gop, m, &sz, (efi_gop_mode_info **)&mi) != EFI_SUCCESS ||
            !mi)
            continue;
        if (mi->pixel_format > 2) /* RGB/BGR only */
            continue;
        uint32_t score = mi->hres * mi->vres;
        if (mi->hres == conf.width && mi->vres == conf.height)
            score += 0x10000000;
        if (score > score_best) {
            score_best = score;
            chosen = m;
        }
    }
    if (gop->set_mode(gop, chosen) != EFI_SUCCESS) {
        boot_serial_write_str("uefi: set_mode failed\n");
        return EFI_UNSUPPORTED;
    }

    gop_info_full *active = (gop_info_full *)gop->mode->info;
    uint32_t rm = active->r, gm = active->g, bm = active->b;
    if (active->pixel_format == 1) { /* BGR */
        rm = active->b;
        bm = active->r;
        /* masks in PixelBitMask differ; OVMF often uses PixelBlueGreenRedReserved8BitPerColor */
    }

    uint8_t *kdata = NULL;
    uint64_t ksize = 0;
    if (load_kernel_file(&kdata, &ksize) != EFI_SUCCESS) {
        boot_serial_write_str("uefi: kernel missing\n");
        return EFI_NOT_FOUND;
    }
    boot_serial_write_str("uefi: kernel loaded\n");

    /* Allocate page-table arena + kernel image pages (32 MiB). */
    uint64_t arena = 0;
    const efi_uintn arena_pages = 8192; /* 32 MiB */
    if (BS->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_MEMORY_LOADER_DATA,
                           arena_pages, &arena) != EFI_SUCCESS) {
        boot_serial_write_str("uefi: arena alloc failed\n");
        return EFI_LOAD_ERROR;
    }
    pt_next = arena;
    pt_end = arena + (uint64_t)arena_pages * 0x1000ULL;

    uint64_t bootinfo_phys = 0;
    if (BS->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_MEMORY_LOADER_DATA, 1,
                           &bootinfo_phys) != EFI_SUCCESS)
        return EFI_LOAD_ERROR;
    struct peak_bootinfo *info = (struct peak_bootinfo *)(uintptr_t)bootinfo_phys;
    boot_memset(info, 0, sizeof(*info));

    uint64_t stack_phys = 0;
    if (BS->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_MEMORY_LOADER_DATA, 16,
                           &stack_phys) != EFI_SUCCESS)
        return EFI_LOAD_ERROR;

    struct boot_page_allocator a = { .alloc_pages = alloc_pages };
    if (boot_paging_init(&a, 0x100000000ULL, &g_pml4) != 0) {
        boot_serial_write_str("uefi: paging init failed\n");
        return EFI_LOAD_ERROR;
    }

    struct boot_elf_image img = { .data = kdata, .size = (size_t)ksize };
    struct boot_loaded_kernel k;
    if (boot_elf_load(&img, alloc_pages, map_page_cb, &k) != 0) {
        boot_serial_write_str("uefi: ELF load failed\n");
        return EFI_LOAD_ERROR;
    }

    /* Memory map + ExitBootServices */
    efi_uintn map_size = 0, map_key = 0, desc_size = 0;
    uint32_t desc_ver = 0;
    BS->get_memory_map(&map_size, NULL, &map_key, &desc_size, &desc_ver);
    map_size += 4 * desc_size;
    efi_memory_descriptor *map = NULL;
    if (BS->allocate_pool(EFI_MEMORY_LOADER_DATA, map_size, (void **)&map) !=
        EFI_SUCCESS)
        return EFI_LOAD_ERROR;
    if (BS->get_memory_map(&map_size, map, &map_key, &desc_size, &desc_ver) !=
        EFI_SUCCESS)
        return EFI_LOAD_ERROR;

    efi_mmap_to_peak(map, map_size, desc_size, info);

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
    info->stack_phys = stack_phys;
    info->stack_size = 16 * 0x1000ULL;
    info->entropy_len = 0;
    info->kaslr_slide_pages = 0;
    {
        efi_rng_protocol *rng = NULL;
        if (BS->locate_protocol((efi_guid *)&EFI_RNG_PROTOCOL_GUID, NULL,
                                (void **)&rng) == EFI_SUCCESS && rng &&
            rng->get_rng) {
            if (rng->get_rng(rng, NULL, PEAK_BOOT_ENTROPY_MAX, info->entropy) ==
                EFI_SUCCESS) {
                info->entropy_len = PEAK_BOOT_ENTROPY_MAX;
                info->flags |= PEAK_BOOT_FLAG_ENTROPY_OK;
                boot_serial_write_str("uefi: EFI_RNG ok\n");
            }
        }
        if (info->entropy_len == 0) {
            uint64_t t = 0;
            for (int i = 0; i < PEAK_BOOT_ENTROPY_MAX; i++) {
                __asm__ volatile ("rdtsc" : "=A"(t));
                for (volatile int j = 0; j < 50 + i; j++)
                    t ^= (uint64_t)j;
                info->entropy[i] = (uint8_t)(t ^ (t >> 17));
            }
            info->entropy_len = PEAK_BOOT_ENTROPY_MAX;
            info->flags |= PEAK_BOOT_FLAG_ENTROPY_WEAK;
            /* Prefer RDRAND when CPU supports it. */
            uint32_t aa, bb, cc, dd;
            __asm__ volatile ("cpuid" : "=a"(aa), "=b"(bb), "=c"(cc), "=d"(dd)
                              : "a"(1), "c"(0));
            if ((cc >> 30) & 1) {
                for (int i = 0; i < 8; i++) {
                    uint64_t v = 0;
                    uint8_t ok = 0;
                    __asm__ volatile ("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
                    if (ok) {
                        boot_memcpy(info->entropy + i * 8, &v, 8);
                        info->flags |= PEAK_BOOT_FLAG_ENTROPY_OK;
                        info->flags &= ~PEAK_BOOT_FLAG_ENTROPY_WEAK;
                    }
                }
            }
        }
    }
    info->net = conf.net;
    info->fb.addr = gop->mode->frame_buffer_base + PEAK_HHDM_OFFSET;
    info->fb.width = active->hres;
    info->fb.height = active->vres;
    info->fb.pitch = (uint64_t)active->ppsl * 4;
    info->fb.bpp = 32;
    if (active->pixel_format == 1) {
        /* BGRR */
        info->fb.red_mask_size = 8;
        info->fb.red_mask_shift = 16;
        info->fb.green_mask_size = 8;
        info->fb.green_mask_shift = 8;
        info->fb.blue_mask_size = 8;
        info->fb.blue_mask_shift = 0;
    } else if (active->pixel_format == 0) {
        /* RGBR */
        info->fb.red_mask_size = 8;
        info->fb.red_mask_shift = 0;
        info->fb.green_mask_size = 8;
        info->fb.green_mask_shift = 8;
        info->fb.blue_mask_size = 8;
        info->fb.blue_mask_shift = 16;
    } else {
        info->fb.red_mask_size = (uint8_t)mask_size(rm);
        info->fb.red_mask_shift = (uint8_t)mask_shift(rm);
        info->fb.green_mask_size = (uint8_t)mask_size(gm);
        info->fb.green_mask_shift = (uint8_t)mask_shift(gm);
        info->fb.blue_mask_size = (uint8_t)mask_size(bm);
        info->fb.blue_mask_shift = (uint8_t)mask_shift(bm);
    }

    if (info->mmap_count < PEAK_MMAP_MAX) {
        info->mmap[info->mmap_count++] = (struct peak_mmap_entry){
            .base = k.phys_base, .length = k.phys_size, .type = PEAK_MMAP_KERNEL
        };
    }

    if (BS->exit_boot_services(IH, map_key) != EFI_SUCCESS) {
        /* Retry once */
        map_size = 0;
        BS->get_memory_map(&map_size, NULL, &map_key, &desc_size, &desc_ver);
        map_size += 4 * desc_size;
        BS->get_memory_map(&map_size, map, &map_key, &desc_size, &desc_ver);
        if (BS->exit_boot_services(IH, map_key) != EFI_SUCCESS) {
            boot_serial_write_str("uefi: ExitBootServices failed\n");
            return EFI_LOAD_ERROR;
        }
    }

    boot_paging_activate(g_pml4);
    boot_serial_write_str("uefi: entering kernel\n");

    struct peak_bootinfo *info_v =
        (struct peak_bootinfo *)(PEAK_HHDM_OFFSET + bootinfo_phys);
    uint64_t stack_top = PEAK_HHDM_OFFSET + stack_phys + info->stack_size;
    peak_kernel_entry_fn entry = (peak_kernel_entry_fn)k.entry_virt;

    /* SysV ABI: entry %rsp must be ≡ 8 (mod 16), as if reached via call.
     * Push a NULL fake return address so jmp'ing in keeps alignment. */
    __asm__ volatile(
        "mov %[stk], %%rsp\n"
        "xor %%ebp, %%ebp\n"
        "mov %[info], %%rdi\n"
        "pushq $0\n"
        "jmp *%[entry]\n"
        :
        : [stk] "r"(stack_top), [info] "r"(info_v), [entry] "r"(entry)
        : "memory", "rsp", "rdi", "rbp");

    return EFI_LOAD_ERROR;
}
