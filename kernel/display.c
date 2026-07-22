#include "display.h"
#include "display_clip.h"
#include "serial.h"
#include "util.h"

#if defined(__x86_64__)
#include "x86_io.h"
#endif

#if defined(__aarch64__)
#include "rpi.h"
#endif

static struct framebuffer g_disp;
static struct display_caps g_caps;
static int g_inited;
static int g_batch; /* frame batch open: VBlank already waited */

#if defined(__aarch64__)
static int g_flip_ok;
static int g_flip_page; /* 0 = showing y=0, 1 = showing y=height */
static int g_flip_dirty;
static uint32_t g_phys_h;
#endif

static void copy_u32_rows(uint8_t *dst_base, uint64_t dst_pitch,
                          const uint32_t *src, uint32_t src_stride,
                          uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *dst = (uint32_t *)(dst_base + y * dst_pitch);
        const uint32_t *s = src + (uint64_t)y * src_stride;
        uint32_t x = 0;
        for (; x + 1 < w; x += 2) {
            uint64_t v = (uint64_t)s[x] | ((uint64_t)s[x + 1] << 32);
            *(uint64_t *)(dst + x) = v;
        }
        if (x < w)
            dst[x] = s[x];
    }
}

#if defined(__x86_64__)
#define VGA_IS1 0x3DA
#define VGA_RETRACE 0x08

static int vga_retrace_probe(void) {
    uint8_t a = inb(VGA_IS1);
    for (int i = 0; i < 100000; i++) {
        if ((inb(VGA_IS1) & VGA_RETRACE) != (a & VGA_RETRACE))
            return 1;
    }
    return 0;
}

static void vga_wait_vblank(void) {
    /* Bound waits tightly — a stuck IS1 bit must not freeze the desktop. */
    int spins = 0;
    while ((inb(VGA_IS1) & VGA_RETRACE) && spins++ < 200000)
        ;
    spins = 0;
    while (!(inb(VGA_IS1) & VGA_RETRACE) && spins++ < 200000)
        ;
}
#endif

#if defined(__aarch64__)
static int rpi_try_pageflip(void) {
    uint32_t w = (uint32_t)g_disp.width;
    uint32_t h = (uint32_t)g_disp.height;
    if (!w || !h)
        return 0;
    /* Double virtual height; only enable flip if offset into page 1 works
     * (firmware rejects offsets past the allocated buffer). */
    if (platform_mailbox_fb_set_virt(w, h * 2) != 0)
        return 0;
    uint32_t pitch = 0;
    if (platform_mailbox_fb_get_pitch(&pitch) == 0 && pitch > 0)
        g_disp.pitch = pitch;
    if (platform_mailbox_fb_set_offset(0, h) != 0) {
        platform_mailbox_fb_set_virt(w, h);
        platform_mailbox_fb_set_offset(0, 0);
        return 0;
    }
    if (platform_mailbox_fb_set_offset(0, 0) != 0) {
        platform_mailbox_fb_set_virt(w, h);
        return 0;
    }
    g_phys_h = h;
    g_flip_page = 0;
    g_flip_dirty = 0;
    g_flip_ok = 1;
    return 1;
}

static void rpi_flip_to(int page) {
    if (!g_flip_ok)
        return;
    uint32_t y = page ? g_phys_h : 0;
    if (platform_mailbox_fb_set_offset(0, y) == 0)
        g_flip_page = page;
}

static uint8_t *rpi_hidden_base(void) {
    int hide = 1 - g_flip_page;
    return g_disp.addr + (uint64_t)hide * (uint64_t)g_phys_h * g_disp.pitch;
}
#endif

void display_init(struct framebuffer *fb) {
    g_caps.has_vblank = 0;
    g_caps.has_pageflip = 0;
    g_batch = 0;
    g_inited = 0;
    if (!fb || !fb->addr || !fb->width || !fb->height)
        return;
    g_disp = *fb;
    g_inited = 1;

#if defined(__x86_64__)
    if (vga_retrace_probe())
        g_caps.has_vblank = 1;
#endif

#if defined(__aarch64__)
    g_flip_ok = 0;
    if (rpi_try_pageflip())
        g_caps.has_pageflip = 1;
#endif

    serial_write_str("display: vblank=");
    serial_write_str(g_caps.has_vblank ? "1" : "0");
    serial_write_str(" pageflip=");
    serial_write_str(g_caps.has_pageflip ? "1" : "0");
    serial_write_str("\n");
}

struct display_caps display_get_caps(void) {
    return g_caps;
}

void display_wait_vblank(void) {
    if (!g_inited || !g_caps.has_vblank)
        return;
#if defined(__x86_64__)
    vga_wait_vblank();
#endif
}

void display_frame_begin(void) {
    if (!g_inited)
        return;
    if (g_batch)
        return;
    if (g_caps.has_vblank && !g_caps.has_pageflip)
        display_wait_vblank();
    /* RPi: do not memcpy the full visible page into the hidden page.
     * Full presents rewrite the entire hidden page; damage presents write
     * only damaged rects (and flip only from display_frame_end when dirty).
     * For damage-only without a prior full sync, present_rect targets the
     * visible scanout when flip is deferred — see display_present_rect. */
    g_batch = 1;
}

void display_present_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          const uint32_t *src, uint32_t src_stride) {
    if (!g_inited || !src)
        return;
    uint32_t fw = (uint32_t)g_disp.width;
    uint32_t fh = (uint32_t)g_disp.height;
    if (!display_clip_rect(fw, fh, x, y, w, h, &x, &y, &w, &h))
        return;

#if defined(__aarch64__)
    if (g_flip_ok) {
        /* Damage-only: write the visible page so undamaged pixels stay correct
         * without a full-page sync. Full frames still page-flip. */
        if (!g_batch) {
            uint8_t *vis = g_disp.addr +
                           (uint64_t)g_flip_page * (uint64_t)g_phys_h * g_disp.pitch;
            copy_u32_rows(vis + (uint64_t)y * g_disp.pitch + x * 4, g_disp.pitch,
                          src, src_stride, w, h);
            return;
        }
        uint8_t *base = rpi_hidden_base();
        copy_u32_rows(base + (uint64_t)y * g_disp.pitch + x * 4, g_disp.pitch,
                      src, src_stride, w, h);
        g_flip_dirty = 1;
        return;
    }
#endif

    /* Never wait for VBlank here — cursor erase and soft damage must be fast.
     * Callers that need tear-free full frames use display_frame_begin() first. */
    copy_u32_rows(g_disp.addr + (uint64_t)y * g_disp.pitch + x * 4, g_disp.pitch,
                  src, src_stride, w, h);
}

void display_present_full(const uint32_t *src) {
    if (!g_inited || !src)
        return;
    uint32_t w = (uint32_t)g_disp.width;
    uint32_t h = (uint32_t)g_disp.height;

#if defined(__aarch64__)
    if (g_flip_ok) {
        uint8_t *dst = rpi_hidden_base();
        if (g_disp.pitch == (uint64_t)w * 4) {
            uint64_t n = (uint64_t)w * h;
            uint64_t *d = (uint64_t *)dst;
            const uint64_t *s = (const uint64_t *)src;
            uint64_t q = n / 2;
            for (uint64_t i = 0; i < q; i++)
                d[i] = s[i];
            if (n & 1)
                ((uint32_t *)dst)[n - 1] = src[n - 1];
        } else {
            copy_u32_rows(dst, g_disp.pitch, src, w, w, h);
        }
        rpi_flip_to(1 - g_flip_page);
        g_flip_dirty = 0;
        return;
    }
#endif

    if (!g_batch && g_caps.has_vblank)
        display_wait_vblank();

    if (g_disp.pitch == (uint64_t)w * 4) {
        uint64_t n = (uint64_t)w * h;
        uint64_t *dst = (uint64_t *)g_disp.addr;
        const uint64_t *s = (const uint64_t *)src;
        uint64_t q = n / 2;
        for (uint64_t i = 0; i < q; i++)
            dst[i] = s[i];
        if (n & 1)
            ((uint32_t *)g_disp.addr)[n - 1] = src[n - 1];
        return;
    }
    copy_u32_rows(g_disp.addr, g_disp.pitch, src, w, w, h);
}

void display_frame_end(void) {
#if defined(__aarch64__)
    if (g_flip_ok && g_flip_dirty) {
        rpi_flip_to(1 - g_flip_page);
        g_flip_dirty = 0;
    }
#endif
    g_batch = 0;
}
