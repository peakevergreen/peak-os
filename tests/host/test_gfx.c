/* Host unit tests for GFX helpers (damage merge, pitch copy, glyph span fill). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* --- damage merge (mirrors desktop_damage.c logic) --- */
#define MAX_DAMAGE 16
struct damage_rect {
    uint32_t x, y, w, h;
};

static struct damage_rect damage_list[MAX_DAMAGE];
static int damage_count;
static int damage_overflow;
static uint32_t g_fw = 1920, g_fh = 1080;

static void damage_clear(void) {
    damage_count = 0;
    damage_overflow = 0;
}

static uint64_t rect_area(uint32_t w, uint32_t h) {
    return (uint64_t)w * (uint64_t)h;
}

static void rect_union_into(struct damage_rect *r, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h) {
    uint32_t x2 = x + w, y2 = y + h;
    uint32_t rx2 = r->x + r->w, ry2 = r->y + r->h;
    uint32_t nx = r->x < x ? r->x : x;
    uint32_t ny = r->y < y ? r->y : y;
    uint32_t nx2 = rx2 > x2 ? rx2 : x2;
    uint32_t ny2 = ry2 > y2 ? ry2 : y2;
    r->x = nx;
    r->y = ny;
    r->w = nx2 - nx;
    r->h = ny2 - ny;
}

static int rects_touch(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah,
                       uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
    return ax <= bx + bw && ax + aw >= bx && ay <= by + bh && ay + ah >= by;
}

static void damage_compact(void) {
    int changed = 1;
    while (changed && damage_count > 1) {
        changed = 0;
        for (int i = 0; i < damage_count && !changed; i++) {
            for (int j = i + 1; j < damage_count; j++) {
                struct damage_rect *a = &damage_list[i];
                struct damage_rect *b = &damage_list[j];
                if (!rects_touch(a->x, a->y, a->w, a->h, b->x, b->y, b->w, b->h))
                    continue;
                rect_union_into(a, b->x, b->y, b->w, b->h);
                damage_list[j] = damage_list[damage_count - 1];
                damage_count--;
                changed = 1;
                break;
            }
        }
    }
}

static void damage_add(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!w || !h || damage_overflow)
        return;
    if (x >= g_fw || y >= g_fh)
        return;
    /* Mirror display_clip_rect: avoid uint32 x+w / y+h wrap. */
    if (w > g_fw - x)
        w = g_fw - x;
    if (h > g_fh - y)
        h = g_fh - y;
    if (!w || !h)
        return;
    for (int i = 0; i < damage_count; i++) {
        struct damage_rect *r = &damage_list[i];
        uint32_t x2 = x + w, y2 = y + h;
        uint32_t rx2 = r->x + r->w, ry2 = r->y + r->h;
        if (x >= r->x && y >= r->y && x2 <= rx2 && y2 <= ry2)
            return;
        if (r->x >= x && r->y >= y && rx2 <= x2 && ry2 <= y2) {
            r->x = x;
            r->y = y;
            r->w = w;
            r->h = h;
            return;
        }
        if (rects_touch(x, y, w, h, r->x, r->y, r->w, r->h)) {
            rect_union_into(r, x, y, w, h);
            damage_compact();
            return;
        }
    }
    if (damage_count < MAX_DAMAGE) {
        damage_list[damage_count].x = x;
        damage_list[damage_count].y = y;
        damage_list[damage_count].w = w;
        damage_list[damage_count].h = h;
        damage_count++;
        return;
    }
    int best = 0;
    uint64_t best_growth = (uint64_t)-1;
    for (int i = 0; i < damage_count; i++) {
        struct damage_rect *r = &damage_list[i];
        uint32_t x2 = x + w, y2 = y + h;
        uint32_t rx2 = r->x + r->w, ry2 = r->y + r->h;
        uint32_t nx = r->x < x ? r->x : x;
        uint32_t ny = r->y < y ? r->y : y;
        uint32_t nx2 = rx2 > x2 ? rx2 : x2;
        uint32_t ny2 = ry2 > y2 ? ry2 : y2;
        uint64_t growth = rect_area(nx2 - nx, ny2 - ny) - rect_area(r->w, r->h);
        if (growth < best_growth) {
            best_growth = growth;
            best = i;
        }
    }
    rect_union_into(&damage_list[best], x, y, w, h);
    damage_compact();
}

static void damage_merge_all(void) {
    if (damage_count <= 1)
        return;
    uint32_t x1 = (uint32_t)-1, y1 = (uint32_t)-1, x2 = 0, y2 = 0;
    for (int i = 0; i < damage_count; i++) {
        struct damage_rect *r = &damage_list[i];
        if (r->x < x1) x1 = r->x;
        if (r->y < y1) y1 = r->y;
        if (r->x + r->w > x2) x2 = r->x + r->w;
        if (r->y + r->h > y2) y2 = r->y + r->h;
    }
    damage_count = 0;
    damage_overflow = 0;
    if (x2 > x1 && y2 > y1)
        damage_add(x1, y1, x2 - x1, y2 - y1);
}

static void damage_merge_bounds(uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh) {
    if (damage_count < 1) {
        *ox = *oy = *ow = *oh = 0;
        return;
    }
    uint32_t x1 = damage_list[0].x, y1 = damage_list[0].y;
    uint32_t x2 = x1 + damage_list[0].w, y2 = y1 + damage_list[0].h;
    for (int i = 1; i < damage_count; i++) {
        uint32_t rx2 = damage_list[i].x + damage_list[i].w;
        uint32_t ry2 = damage_list[i].y + damage_list[i].h;
        if (damage_list[i].x < x1) x1 = damage_list[i].x;
        if (damage_list[i].y < y1) y1 = damage_list[i].y;
        if (rx2 > x2) x2 = rx2;
        if (ry2 > y2) y2 = ry2;
    }
    *ox = x1;
    *oy = y1;
    *ow = x2 - x1;
    *oh = y2 - y1;
}

/* --- pitch copy --- */
static void copy_u32_rows(uint8_t *dst_base, uint64_t dst_pitch,
                          const uint32_t *src, uint32_t src_stride,
                          uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *dst = (uint32_t *)(dst_base + y * dst_pitch);
        const uint32_t *s = src + (uint64_t)y * src_stride;
        for (uint32_t x = 0; x < w; x++)
            dst[x] = s[x];
    }
}

/* --- span fill --- */
static void fill_u32_row(uint32_t *dst, uint32_t w, uint32_t color) {
    uint32_t col = 0;
    for (; col + 1 < w; col += 2) {
        dst[col] = color;
        dst[col + 1] = color;
    }
    if (col < w)
        dst[col] = color;
}

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        fails++;
    }
}

#define SURF_MAX_DAMAGE 8
struct surface_rect { uint32_t x, y, w, h; };

static void surf_clear_test(int *count, int *overflow) {
    *count = 0;
    *overflow = 0;
}

static void surf_add_test(struct surface_rect *d, int *count, int *overflow,
                          uint32_t sw, uint32_t sh,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!w || !h || *overflow)
        return;
    if (x >= sw || y >= sh)
        return;
    if (w > sw - x)
        w = sw - x;
    if (h > sh - y)
        h = sh - y;
    if (!w || !h)
        return;
    for (int si = 0; si < *count; si++) {
        struct surface_rect *r = &d[si];
        uint32_t x2 = x + w, y2 = y + h;
        uint32_t rx2 = r->x + r->w, ry2 = r->y + r->h;
        if (x >= r->x && y >= r->y && x2 <= rx2 && y2 <= ry2)
            return;
        if (r->x >= x && r->y >= y && rx2 <= x2 && ry2 <= y2) {
            r->x = x;
            r->y = y;
            r->w = w;
            r->h = h;
            return;
        }
    }
    if (*count >= SURF_MAX_DAMAGE) {
        *overflow = 1;
        *count = 0;
        return;
    }
    d[*count].x = x;
    d[*count].y = y;
    d[*count].w = w;
    d[*count].h = h;
    (*count)++;
}

int main(void) {
    uint32_t bx, by, bw, bh;

    damage_clear();
    damage_add(10, 10, 50, 50);
    damage_add(20, 20, 10, 10); /* contained → no new rect */
    expect(damage_count == 1, "contained damage merges");
    damage_add(100, 100, 40, 40);
    expect(damage_count == 2, "disjoint damage adds");
    damage_merge_bounds(&bx, &by, &bw, &bh);
    expect(bx == 10 && by == 10 && bw == 130 && bh == 130, "bounds union");

    damage_clear();
    damage_add(10, 10, 20, 20);
    damage_add(30, 10, 20, 20); /* edge-adjacent → coalesce */
    expect(damage_count == 1, "adjacent damage coalesces");
    expect(damage_list[0].x == 10 && damage_list[0].w == 40, "adjacent union width");

    damage_clear();
    for (int i = 0; i < MAX_DAMAGE + 4; i++)
        damage_add((uint32_t)(i * 40), (uint32_t)(i * 30), 8, 8);
    expect(damage_overflow == 0, "overflow coalesces without flag");
    expect(damage_count <= MAX_DAMAGE, "overflow keeps list bounded");
    expect(damage_count >= 1, "overflow keeps honest rects");
    damage_merge_all();
    expect(damage_count == 1, "merge_all collapses to one rect");
    expect(damage_list[0].w < g_fw || damage_list[0].h < g_fh,
           "merge_all is bbox not forced fullscreen");

    damage_clear();
    damage_add(1900, 10, 0xFFFFFFF0u, 8);
    expect(damage_count == 1, "wrapping width still adds");
    expect(damage_list[0].x == 1900 && damage_list[0].w == 20, "wrapping width clipped");
    damage_add(0xFFFFFFF0u, 0, 8, 8);
    expect(damage_count == 1, "off-screen overflow x ignored");

    uint32_t src[8 * 4];
    uint8_t dst[8 * 4 * 4 + 16];
    memset(dst, 0xAB, sizeof(dst));
    for (uint32_t i = 0; i < 32; i++)
        src[i] = 0xFF000000u | i;
    copy_u32_rows(dst, 8 * 4, src, 8, 8, 4);
    expect(((uint32_t *)dst)[0] == 0xFF000000u, "pitch copy first pixel");
    expect(((uint32_t *)dst)[8 * 3 + 7] == (0xFF000000u | 31), "pitch copy last pixel");

    uint32_t row[64];
    memset(row, 0, sizeof(row));
    fill_u32_row(row, 63, 0x11223344u);
    expect(row[0] == 0x11223344u && row[62] == 0x11223344u, "span fill odd width");
    expect(row[63] == 0, "span fill does not overrun");

    /* --- surface rect damage (mirrors surface.c) --- */
    struct surface_rect surf_dmg[SURF_MAX_DAMAGE];
    int surf_count = 0, surf_overflow = 0;
    uint32_t surf_w = 640, surf_h = 480;

    surf_clear_test(&surf_count, &surf_overflow);
    surf_add_test(surf_dmg, &surf_count, &surf_overflow, surf_w, surf_h, 10, 10, 20, 20);
    surf_add_test(surf_dmg, &surf_count, &surf_overflow, surf_w, surf_h, 12, 12, 5, 5);
    expect(surf_count == 1, "surface contained rect skipped");
    surf_add_test(surf_dmg, &surf_count, &surf_overflow, surf_w, surf_h, 50, 50, 10, 10);
    expect(surf_count == 2, "surface disjoint rect added");
    for (int i = 0; i < SURF_MAX_DAMAGE + 2; i++)
        surf_add_test(surf_dmg, &surf_count, &surf_overflow, surf_w, surf_h,
                      (uint32_t)(i * 10), 0, 5, 5);
    expect(surf_overflow == 1, "surface overflow flag");

    if (fails) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("test_gfx: ok\n");
    return 0;
}
