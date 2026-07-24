#include "fb.h"
#include "display.h"
#include "display_clip.h"
#include "font_render.h"
#include "pmm.h"
#include "vmm.h"
#include "util.h"
#include "serial.h"

extern const uint8_t font8x16[256][16];

/* Small emergency BSS if PMM backbuffer alloc fails (heap/PMM preferred). */
#define FB_BACK_FALLBACK_W 640
#define FB_BACK_FALLBACK_H 480

static struct framebuffer g_fb;
static uint32_t g_scale = 3;

static uint32_t g_back_fallback[FB_BACK_FALLBACK_W * FB_BACK_FALLBACK_H];
static uint32_t *g_back;
static uint32_t g_back_w, g_back_h;
static void *g_back_phys;
static size_t g_back_pages;
static int g_back_ok;
static int g_in_frame; /* drawing targets back buffer */

/* Optional offscreen draw target (surfaces). */
static uint32_t *g_tgt;
static uint32_t g_tgt_w, g_tgt_h, g_tgt_stride;

static uint8_t *draw_addr(void) {
    if (g_tgt)
        return (uint8_t *)g_tgt;
    if (g_in_frame && g_back_ok)
        return (uint8_t *)g_back;
    return g_fb.addr;
}

static uint64_t draw_pitch(void) {
    if (g_tgt)
        return (uint64_t)g_tgt_stride * 4;
    if (g_in_frame && g_back_ok)
        return g_fb.width * 4;
    return g_fb.pitch;
}

static uint32_t draw_width(void) {
    if (g_tgt)
        return g_tgt_w;
    return (uint32_t)g_fb.width;
}

static uint32_t draw_height(void) {
    if (g_tgt)
        return g_tgt_h;
    return (uint32_t)g_fb.height;
}

static void ensure_color_masks(void) {
    if (g_fb.red_mask_size == 0 && g_fb.green_mask_size == 0 &&
        g_fb.blue_mask_size == 0) {
        g_fb.red_mask_size = 8;
        g_fb.red_mask_shift = 16;
        g_fb.green_mask_size = 8;
        g_fb.green_mask_shift = 8;
        g_fb.blue_mask_size = 8;
        g_fb.blue_mask_shift = 0;
    }
}

void fb_init(struct framebuffer *fb) {
    g_fb = *fb;
    ensure_color_masks();
    if (g_fb.bpp != 0 && g_fb.bpp != 32)
        serial_write_str("fb: non-32bpp mode — treating pixels as 32-bit; prefer 32bpp\n");
    if (g_scale < 1)
        g_scale = 3;
    g_in_frame = 0;
    g_back_ok = 0;
    g_back = NULL;
    g_back_phys = NULL;
    g_back_pages = 0;
    g_back_w = (uint32_t)g_fb.width;
    g_back_h = (uint32_t)g_fb.height;
    /* PMM may not be ready yet — use BSS fallback until fb_alloc_backbuffer(). */
    if (g_back_w > 0 && g_back_h > 0 &&
        g_back_w <= FB_BACK_FALLBACK_W && g_back_h <= FB_BACK_FALLBACK_H) {
        g_back = g_back_fallback;
        g_back_ok = 1;
    }
    display_init(&g_fb);
}

void fb_alloc_backbuffer(void) {
    if (g_back_w == 0 || g_back_h == 0)
        return;
    size_t bytes = (size_t)g_back_w * (size_t)g_back_h * 4;
    size_t pages = (bytes + 4095) / 4096;
    void *phys = pmm_alloc_pages(pages);
    if (!phys) {
        serial_write_str("fb: backbuffer alloc failed — GUI will tear (front-only)\n");
        if (g_back_w > FB_BACK_FALLBACK_W || g_back_h > FB_BACK_FALLBACK_H)
            g_back_ok = 0;
        return;
    }
    uint32_t *virt = (uint32_t *)vmm_phys_to_virt((uint64_t)phys);
    memset(virt, 0, bytes);
    g_back_phys = phys;
    g_back_pages = pages;
    g_back = virt;
    g_back_ok = 1;
}

uint32_t fb_recommend_scale(void) {
    uint32_t h = (uint32_t)g_fb.height;
    uint32_t w = (uint32_t)g_fb.width;
    /* Prefer readable text on high-res guests; keep room for ~80-col CLI. */
    if (h >= 1080 || w >= 1800)
        return 3;
    if (h >= 800 || w >= 1280)
        return 2;
    return 1;
}

void fb_set_ui_scale(uint32_t scale) {
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    if (scale != g_scale)
        font_render_invalidate();
    g_scale = scale;
}

uint32_t fb_ui_scale(void) { return g_scale; }
uint32_t fb_char_w(void) { return 8 * g_scale; }
uint32_t fb_char_h(void) { return 16 * g_scale; }
uint32_t fb_cell_w(void) { return fb_char_w(); }
uint32_t fb_cell_h(void) { return fb_char_h() + g_scale; }

struct framebuffer *fb_get(void) { return &g_fb; }

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    ensure_color_masks();
    uint32_t rs = g_fb.red_mask_size ? g_fb.red_mask_size : 8;
    uint32_t gs = g_fb.green_mask_size ? g_fb.green_mask_size : 8;
    uint32_t bs = g_fb.blue_mask_size ? g_fb.blue_mask_size : 8;
    uint32_t rv = rs >= 8 ? r : (r >> (8 - rs));
    uint32_t gv = gs >= 8 ? g : (g >> (8 - gs));
    uint32_t bv = bs >= 8 ? b : (b >> (8 - bs));
    return (rv << g_fb.red_mask_shift) | (gv << g_fb.green_mask_shift) |
           (bv << g_fb.blue_mask_shift);
}

int fb_backbuffer_ok(void) {
    return g_back_ok;
}

uint32_t *fb_back_buf(void) {
    return g_back_ok ? g_back : NULL;
}

void fb_begin_frame(void) {
    if (g_back_ok)
        g_in_frame = 1;
}

void fb_cancel_frame(void) {
    g_in_frame = 0;
}

static void copy_u32_rows(uint8_t *dst_base, uint64_t dst_pitch,
                          const uint32_t *src, uint32_t src_stride,
                          uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *dst = (uint32_t *)(dst_base + y * dst_pitch);
        const uint32_t *s = src + (uint64_t)y * src_stride;
        uint32_t x = 0;
        /* 64-bit stores cut present cost roughly in half */
        for (; x + 1 < w; x += 2) {
            uint64_t v = (uint64_t)s[x] | ((uint64_t)s[x + 1] << 32);
            *(uint64_t *)(dst + x) = v;
        }
        if (x < w)
            dst[x] = s[x];
    }
}

void fb_end_frame(void) {
    if (!g_in_frame || !g_back_ok)
        return;
    g_in_frame = 0;
    display_present_full(g_back);
}

void fb_present_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!g_back_ok)
        return;
    if (g_in_frame)
        g_in_frame = 0;
    if (!display_clip_rect((uint32_t)g_fb.width, (uint32_t)g_fb.height,
                           x, y, w, h, &x, &y, &w, &h))
        return;
    uint32_t fw = (uint32_t)g_fb.width;
    display_present_rect(x, y, w, h, g_back + (uint64_t)y * fw + x, fw);
}

void fb_blit_argb(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  const uint32_t *src, uint32_t src_stride) {
    if (!src)
        return;
    if (!display_clip_rect(draw_width(), draw_height(), x, y, w, h, &x, &y, &w, &h))
        return;
    copy_u32_rows(draw_addr() + y * draw_pitch() + x * 4, draw_pitch(),
                  src, src_stride, w, h);
}

void fb_copy_from_back(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t *dst, uint32_t dst_stride) {
    if (!g_back_ok || !dst)
        return;
    if (!display_clip_rect((uint32_t)g_fb.width, (uint32_t)g_fb.height,
                           x, y, w, h, &x, &y, &w, &h))
        return;
    uint32_t fw = (uint32_t)g_fb.width;
    for (uint32_t row = 0; row < h; row++) {
        const uint32_t *s = g_back + (uint64_t)(y + row) * fw + x;
        uint32_t *d = dst + (uint64_t)row * dst_stride;
        for (uint32_t col = 0; col < w; col++)
            d[col] = s[col];
    }
}

void fb_copy_to_back(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const uint32_t *src, uint32_t src_stride) {
    if (!g_back_ok || !src)
        return;
    if (!display_clip_rect((uint32_t)g_fb.width, (uint32_t)g_fb.height,
                           x, y, w, h, &x, &y, &w, &h))
        return;
    uint32_t fw = (uint32_t)g_fb.width;
    copy_u32_rows((uint8_t *)(g_back + (uint64_t)y * fw + x), (uint64_t)fw * 4,
                  src, src_stride, w, h);
}

void fb_set_draw_target(uint32_t *px, uint32_t w, uint32_t h, uint32_t stride) {
    if (!px || !w || !h || !stride) {
        g_tgt = NULL;
        g_tgt_w = g_tgt_h = g_tgt_stride = 0;
        return;
    }
    g_tgt = px;
    g_tgt_w = w;
    g_tgt_h = h;
    g_tgt_stride = stride;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= draw_width() || y >= draw_height())
        return;
    uint32_t *pixel = (uint32_t *)(draw_addr() + y * draw_pitch() + x * 4);
    *pixel = color;
}

uint32_t fb_get_pixel(uint32_t x, uint32_t y) {
    if (x >= draw_width() || y >= draw_height())
        return 0;
    uint32_t *pixel = (uint32_t *)(draw_addr() + y * draw_pitch() + x * 4);
    return *pixel;
}

uint32_t fb_front_get_pixel(uint32_t x, uint32_t y) {
    if (x >= g_fb.width || y >= g_fb.height)
        return 0;
    uint32_t *pixel = (uint32_t *)(g_fb.addr + y * g_fb.pitch + x * 4);
    return *pixel;
}

void fb_front_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_fb.width || y >= g_fb.height)
        return;
    uint32_t *pixel = (uint32_t *)(g_fb.addr + y * g_fb.pitch + x * 4);
    *pixel = color;
}

static void fill_u32_row(uint32_t *dst, uint32_t w, uint32_t color) {
    uint32_t col = 0;
    /* Two-at-a-time without type-punning (host TBAA can elide uint64 stores). */
    for (; col + 1 < w; col += 2) {
        dst[col] = color;
        dst[col + 1] = color;
    }
    if (col < w)
        dst[col] = color;
}

void fb_front_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!display_clip_rect((uint32_t)g_fb.width, (uint32_t)g_fb.height,
                           x, y, w, h, &x, &y, &w, &h))
        return;
    for (uint32_t row = 0; row < h; row++) {
        uint32_t *dst = (uint32_t *)(g_fb.addr + (y + row) * g_fb.pitch + x * 4);
        fill_u32_row(dst, w, color);
    }
}

/* Restore a rectangle on the front buffer from the last composed back buffer. */
void fb_restore_from_back(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!g_back_ok)
        return;
    if (!display_clip_rect((uint32_t)g_fb.width, (uint32_t)g_fb.height,
                           x, y, w, h, &x, &y, &w, &h))
        return;
    uint32_t fw = (uint32_t)g_fb.width;
    display_present_rect(x, y, w, h, g_back + (uint64_t)y * fw + x, fw);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!display_clip_rect(draw_width(), draw_height(), x, y, w, h, &x, &y, &w, &h))
        return;
    uint8_t *base = draw_addr();
    uint64_t pitch = draw_pitch();
    for (uint32_t row = 0; row < h; row++) {
        uint32_t *dst = (uint32_t *)(base + (y + row) * pitch + x * 4);
        fill_u32_row(dst, w, color);
    }
}

void fb_clear(uint32_t color) {
    uint32_t w = draw_width();
    uint32_t h = draw_height();
    uint8_t *base = draw_addr();
    uint64_t pitch = draw_pitch();
    if (pitch == (uint64_t)w * 4) {
        fill_u32_row((uint32_t *)base, w * h, color);
        return;
    }
    fb_fill_rect(0, 0, w, h, color);
}

static void fill_span(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    /* Clip: callers (glyph rendering) may compute coords past the screen,
     * and the back buffer is exactly width*height — no slack to overrun. */
    if (!display_clip_rect(draw_width(), draw_height(), x, y, w, h, &x, &y, &w, &h))
        return;
    uint8_t *base = draw_addr();
    uint64_t pitch = draw_pitch();
    for (uint32_t row = 0; row < h; row++) {
        uint32_t *dst = (uint32_t *)(base + (y + row) * pitch + x * 4);
        fill_u32_row(dst, w, color);
    }
}

void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    uint32_t s = g_scale;
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    font_render_cell_bg(x, y, cw, ch, bg, fill_span);
    font_render_glyph(c, x, y, fg, s, cw, ch, fill_span);
}

void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    uint32_t cx = x;
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    uint32_t scale = g_scale;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += ch;
            s++;
            continue;
        }
        /* One wide cell-bg fill for a contiguous run, then glyph ink. */
        const char *run = s;
        uint32_t n = 0;
        while (run[n] && run[n] != '\n')
            n++;
        if (n)
            font_render_cell_bg(cx, y, n * cw, ch, bg, fill_span);
        for (uint32_t i = 0; i < n; i++)
            font_render_glyph(s[i], cx + i * cw, y, fg, scale, cw, ch, fill_span);
        cx += n * cw;
        s += n;
    }
}

void fb_draw_string_fit(uint32_t x, uint32_t y, uint32_t max_w, const char *s,
                        uint32_t fg, uint32_t bg) {
    if (!s || !max_w)
        return;
    uint32_t cw = fb_cell_w();
    if (!cw)
        return;
    uint32_t nmax = max_w / cw;
    uint32_t n = 0;
    while (s[n] && s[n] != '\n' && n < nmax)
        n++;
    if (!n)
        return;
    uint32_t ch = fb_cell_h();
    uint32_t scale = g_scale;
    font_render_cell_bg(x, y, n * cw, ch, bg, fill_span);
    for (uint32_t i = 0; i < n; i++)
        font_render_glyph(s[i], x + i * cw, y, fg, scale, cw, ch, fill_span);
}
