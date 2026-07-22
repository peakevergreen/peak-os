#include "rpi.h"
#include "platform.h"
#include "util.h"

#define MBOX_READ   0x00
#define MBOX_STATUS 0x18
#define MBOX_WRITE  0x20
#define MBOX_FULL   0x80000000
#define MBOX_EMPTY  0x40000000
#define MBOX_SPIN_MAX 2000000u

static volatile uint32_t *mbox_regs(void) {
    struct rpi_plat *p = rpi_get();
    uint64_t b = (p && p->mbox_base) ? p->mbox_base : 0x3F00B880ULL;
    return (volatile uint32_t *)(uintptr_t)b;
}

static volatile uint32_t __attribute__((aligned(64))) mbox_buf[36];

/* With MMU+caches on, the VC reads RAM directly: clean CPU cache lines
 * before the call and discard them before reading the response. */
static void mbox_cache_flush(void) {
    uintptr_t a = (uintptr_t)mbox_buf & ~63ULL;
    uintptr_t end = (uintptr_t)mbox_buf + sizeof(mbox_buf);
    for (; a < end; a += 64)
        __asm__ volatile ("dc civac, %0" : : "r"(a) : "memory");
    __asm__ volatile ("dsb sy" ::: "memory");
}

static int mbox_call(uint8_t ch) {
    volatile uint32_t *mb = mbox_regs();
    uint32_t addr = (uint32_t)platform_virt_to_bus((void *)mbox_buf);
    uint32_t spins;
    mbox_cache_flush();
    for (spins = 0; spins < MBOX_SPIN_MAX; spins++) {
        if (!(mb[MBOX_STATUS / 4] & MBOX_FULL))
            break;
    }
    if (spins >= MBOX_SPIN_MAX)
        return 0;
    mb[MBOX_WRITE / 4] = (addr & ~0xFu) | (ch & 0xF);
    for (spins = 0; spins < MBOX_SPIN_MAX; spins++) {
        if (mb[MBOX_STATUS / 4] & MBOX_EMPTY)
            continue;
        uint32_t r = mb[MBOX_READ / 4];
        if ((r & 0xF) == ch && (r & ~0xFu) == (addr & ~0xFu)) {
            mbox_cache_flush();
            return mbox_buf[1] == 0x80000000;
        }
    }
    return 0;
}

int platform_mailbox_fb(struct peak_framebuffer_info *fb) {
    if (!fb)
        return -1;
    memset((void *)mbox_buf, 0, sizeof(mbox_buf));
    mbox_buf[0] = 35 * 4;
    mbox_buf[1] = 0; /* request */
    /* set phy wh */
    mbox_buf[2] = 0x00048003;
    mbox_buf[3] = 8;
    mbox_buf[4] = 8;
    mbox_buf[5] = 1920;
    mbox_buf[6] = 1080;
    /* set virt wh */
    mbox_buf[7] = 0x00048004;
    mbox_buf[8] = 8;
    mbox_buf[9] = 8;
    mbox_buf[10] = 1920;
    mbox_buf[11] = 1080;
    /* set depth */
    mbox_buf[12] = 0x00048005;
    mbox_buf[13] = 4;
    mbox_buf[14] = 4;
    mbox_buf[15] = 32;
    /* set pixel order RGB */
    mbox_buf[16] = 0x00048006;
    mbox_buf[17] = 4;
    mbox_buf[18] = 4;
    mbox_buf[19] = 1;
    /* allocate buffer */
    mbox_buf[20] = 0x00040001;
    mbox_buf[21] = 8;
    mbox_buf[22] = 8;
    mbox_buf[23] = 4096;
    mbox_buf[24] = 0;
    /* get pitch */
    mbox_buf[25] = 0x00040008;
    mbox_buf[26] = 4;
    mbox_buf[27] = 4;
    mbox_buf[28] = 0;
    mbox_buf[29] = 0; /* end tag */

    if (!mbox_call(8))
        return -1;

    uint32_t addr = mbox_buf[23] & 0x3FFFFFFF;
    uint32_t pitch = mbox_buf[28];
    if (!addr || !pitch)
        return -1;

    fb->addr = addr;
    fb->width = mbox_buf[10] ? mbox_buf[10] : 1920;
    fb->height = mbox_buf[11] ? mbox_buf[11] : 1080;
    fb->pitch = pitch;
    fb->bpp = 32;
    fb->red_mask_size = 8;
    fb->red_mask_shift = 0;
    fb->green_mask_size = 8;
    fb->green_mask_shift = 8;
    fb->blue_mask_size = 8;
    fb->blue_mask_shift = 16;
    return 0;
}

int platform_mailbox_fb_set_virt(uint32_t w, uint32_t h) {
    if (!w || !h)
        return -1;
    memset((void *)mbox_buf, 0, sizeof(mbox_buf));
    mbox_buf[0] = 8 * 4;
    mbox_buf[1] = 0;
    mbox_buf[2] = 0x00048004; /* set virtual wh */
    mbox_buf[3] = 8;
    mbox_buf[4] = 8;
    mbox_buf[5] = w;
    mbox_buf[6] = h;
    mbox_buf[7] = 0;
    return mbox_call(8) ? 0 : -1;
}

int platform_mailbox_fb_set_offset(uint32_t x, uint32_t y) {
    memset((void *)mbox_buf, 0, sizeof(mbox_buf));
    mbox_buf[0] = 8 * 4;
    mbox_buf[1] = 0;
    mbox_buf[2] = 0x00048009; /* set virtual offset */
    mbox_buf[3] = 8;
    mbox_buf[4] = 8;
    mbox_buf[5] = x;
    mbox_buf[6] = y;
    mbox_buf[7] = 0;
    return mbox_call(8) ? 0 : -1;
}

int platform_mailbox_fb_get_pitch(uint32_t *pitch) {
    if (!pitch)
        return -1;
    memset((void *)mbox_buf, 0, sizeof(mbox_buf));
    mbox_buf[0] = 7 * 4;
    mbox_buf[1] = 0;
    mbox_buf[2] = 0x00040008; /* get pitch */
    mbox_buf[3] = 4;
    mbox_buf[4] = 4;
    mbox_buf[5] = 0;
    mbox_buf[6] = 0;
    if (!mbox_call(8))
        return -1;
    *pitch = mbox_buf[5];
    return *pitch ? 0 : -1;
}
