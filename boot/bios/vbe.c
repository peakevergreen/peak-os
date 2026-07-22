#include "bios_call.h"
#include "boot_util.h"
#include "peak_boot.h"
#include <stdint.h>

struct __attribute__((packed)) vbe_info {
    char sig[4];
    uint16_t version;
    uint32_t oem;
    uint32_t caps;
    uint32_t mode_ptr;
    uint16_t total_mem;
    uint8_t reserved[236];
};

struct __attribute__((packed)) vbe_mode {
    uint16_t attr;
    uint8_t win_a, win_b;
    uint16_t granularity;
    uint16_t win_size;
    uint16_t seg_a, seg_b;
    uint32_t win_func;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t char_w, char_h, planes, bpp;
    uint8_t banks, model, bank_size, image_pages, reserved0;
    uint8_t red_mask, red_pos, green_mask, green_pos, blue_mask, blue_pos;
    uint8_t reserved_mask, reserved_pos, direct_color;
    uint32_t fb_phys;
    uint32_t off_screen;
    uint16_t off_size;
    uint8_t reserved[206];
};

static struct vbe_info vinfo __attribute__((aligned(16)));
static struct vbe_mode minfo __attribute__((aligned(16)));

static int vbe_get_mode(uint16_t mode) {
    boot_memset(&minfo, 0, sizeof(minfo));
    struct bios_regs r;
    boot_memset(&r, 0, sizeof(r));
    r.eax = 0x4F01;
    r.ecx = mode;
    r.edi = (uint32_t)(uintptr_t)&minfo;
    r.es = 0;
    bios_int(0x10, &r);
    return ((r.eax & 0xFFFF) == 0x004F) ? 0 : -1;
}

static int vbe_set_mode(uint16_t mode) {
    struct bios_regs r;
    boot_memset(&r, 0, sizeof(r));
    r.eax = 0x4F02;
    r.ebx = (uint32_t)mode | 0x4000; /* linear FB */
    bios_int(0x10, &r);
    return ((r.eax & 0xFFFF) == 0x004F) ? 0 : -1;
}

int bios_vbe_init(struct peak_framebuffer_info *fb, uint16_t want_w, uint16_t want_h) {
    boot_memset(&vinfo, 0, sizeof(vinfo));
    boot_memcpy(vinfo.sig, "VBE2", 4);
    struct bios_regs r;
    boot_memset(&r, 0, sizeof(r));
    r.eax = 0x4F00;
    r.edi = (uint32_t)(uintptr_t)&vinfo;
    r.es = 0;
    bios_int(0x10, &r);
    if ((r.eax & 0xFFFF) != 0x004F || boot_memcmp(vinfo.sig, "VESA", 4) != 0) {
        boot_serial_write_str("bios: no VBE\n");
        return -1;
    }

    uint32_t mode_ptr = vinfo.mode_ptr;
    uint16_t seg = (uint16_t)(mode_ptr >> 16);
    uint16_t off = (uint16_t)(mode_ptr & 0xFFFF);
    uint16_t best = 0xFFFF;
    uint32_t best_score = 0;

    for (;;) {
        uint32_t addr = ((uint32_t)seg << 4) + off;
        uint16_t mode = *(uint16_t *)(uintptr_t)addr;
        if (mode == 0xFFFF)
            break;
        off += 2;
        if (off == 0)
            seg++;

        if (vbe_get_mode(mode) != 0)
            continue;
        if (!(minfo.attr & 0x91))
            continue;
        if (minfo.bpp != 32)
            continue;
        if (minfo.width < 640 || minfo.height < 480)
            continue;

        uint32_t score = (uint32_t)minfo.width * minfo.height;
        if (minfo.width == want_w && minfo.height == want_h)
            score += 0x10000000;
        else if (minfo.width <= want_w && minfo.height <= want_h)
            score += 0x8000000;
        if (score > best_score) {
            best_score = score;
            best = mode;
        }
    }

    if (best == 0xFFFF) {
        boot_serial_write_str("bios: no suitable VBE mode\n");
        return -1;
    }
    if (vbe_get_mode(best) != 0 || vbe_set_mode(best) != 0) {
        boot_serial_write_str("bios: set mode failed\n");
        return -1;
    }

    fb->addr = (uint64_t)minfo.fb_phys;
    fb->width = minfo.width;
    fb->height = minfo.height;
    fb->pitch = minfo.pitch;
    fb->bpp = minfo.bpp;
    fb->red_mask_size = minfo.red_mask;
    fb->red_mask_shift = minfo.red_pos;
    fb->green_mask_size = minfo.green_mask;
    fb->green_mask_shift = minfo.green_pos;
    fb->blue_mask_size = minfo.blue_mask;
    fb->blue_mask_shift = minfo.blue_pos;
    return 0;
}
