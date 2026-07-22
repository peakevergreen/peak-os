#include "surface.h"
#include "fb.h"
#include "heap.h"
#include "serial.h"
#include "util.h"

static uint64_t g_surf_bytes;
static int g_warned;

void surface_init(void) {
    g_surf_bytes = 0;
    g_warned = 0;
}

uint64_t surface_bytes_used(void) { return g_surf_bytes; }
uint64_t surface_budget(void) { return SURFACE_BUDGET_BYTES; }

int surface_pressure_pct(void) {
    if (SURFACE_BUDGET_BYTES == 0)
        return 0;
    uint64_t p = (g_surf_bytes * 100ull) / SURFACE_BUDGET_BYTES;
    if (p > 100)
        p = 100;
    return (int)p;
}

static uint64_t surf_bytes(uint32_t w, uint32_t h) {
    return (uint64_t)w * (uint64_t)h * 4ull;
}

static void surf_damage_clear(struct win_surface *s) {
    s->dmg_count = 0;
    s->dmg_overflow = 0;
}

static void surf_damage_add(struct win_surface *s, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h) {
    if (!s || !w || !h || s->dmg_overflow)
        return;
    if (x >= s->w || y >= s->h)
        return;
    if (x + w > s->w)
        w = s->w - x;
    if (y + h > s->h)
        h = s->h - y;
    if (!w || !h)
        return;
    for (int i = 0; i < s->dmg_count; i++) {
        struct surface_rect *r = &s->dmg[i];
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
    if (s->dmg_count >= SURF_MAX_DAMAGE) {
        s->dmg_overflow = 1;
        s->dmg_count = 0;
        return;
    }
    s->dmg[s->dmg_count].x = x;
    s->dmg[s->dmg_count].y = y;
    s->dmg[s->dmg_count].w = w;
    s->dmg[s->dmg_count].h = h;
    s->dmg_count++;
}

int surface_is_dirty(const struct win_surface *s) {
    return s && (s->dmg_overflow || s->dmg_count > 0);
}

void surface_damage_clear(struct win_surface *s) {
    if (s)
        surf_damage_clear(s);
}

void surface_mark_dirty(struct win_surface *s) {
    if (!s)
        return;
    s->dmg_overflow = 1;
    s->dmg_count = 0;
}

void surface_mark_dirty_rect(struct win_surface *s, uint32_t x, uint32_t y,
                             uint32_t w, uint32_t h) {
    if (!s)
        return;
    if (s->dmg_overflow)
        return;
    surf_damage_add(s, x, y, w, h);
}

int surface_ensure(struct win_surface *s, uint32_t w, uint32_t h) {
    if (!s || !w || !h)
        return -1;
    if (s->px && s->w == w && s->h == h)
        return 0;

    uint64_t old_b = s->px ? surf_bytes(s->w, s->h) : 0;
    uint64_t new_b = surf_bytes(w, h);
    uint64_t after = g_surf_bytes - old_b + new_b;
    if (after > SURFACE_BUDGET_BYTES) {
        if (!g_warned) {
            serial_write_str("surface: budget exceeded — soft-fail alloc\n");
            g_warned = 1;
        }
        return -1;
    }

    uint32_t *np = (uint32_t *)kmalloc((size_t)new_b);
    if (!np)
        return -1;
    memset(np, 0, (size_t)new_b);
    if (s->px)
        kfree(s->px);
    g_surf_bytes = after;
    s->px = np;
    s->w = w;
    s->h = h;
    s->stride = w;
    surface_mark_dirty(s);
    return 0;
}

void surface_free(struct win_surface *s) {
    if (!s)
        return;
    if (s->px) {
        uint64_t b = surf_bytes(s->w, s->h);
        if (g_surf_bytes >= b)
            g_surf_bytes -= b;
        else
            g_surf_bytes = 0;
        kfree(s->px);
    }
    s->px = NULL;
    s->w = s->h = s->stride = 0;
    surf_damage_clear(s);
}

void surface_blit_rect(const struct win_surface *s, uint32_t dx, uint32_t dy,
                       uint32_t sx, uint32_t sy, uint32_t w, uint32_t h) {
    if (!s || !s->px || !w || !h)
        return;
    if (sx >= s->w || sy >= s->h)
        return;
    if (sx + w > s->w)
        w = s->w - sx;
    if (sy + h > s->h)
        h = s->h - sy;
    if (!w || !h)
        return;
    fb_blit_argb(dx, dy, w, h, s->px + (uint64_t)sy * s->stride + sx, s->stride);
}

void surface_blit(const struct win_surface *s, uint32_t dx, uint32_t dy) {
    if (!s || !s->px || !s->w || !s->h)
        return;
    surface_blit_rect(s, dx, dy, 0, 0, s->w, s->h);
}

static int rect_intersect(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah,
                          uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                          uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh) {
    if (!aw || !ah || !bw || !bh)
        return 0;
    uint32_t x1 = ax > bx ? ax : bx;
    uint32_t y1 = ay > by ? ay : by;
    uint32_t ax2 = ax + aw, ay2 = ay + ah;
    uint32_t bx2 = bx + bw, by2 = by + bh;
    uint32_t x2 = ax2 < bx2 ? ax2 : bx2;
    uint32_t y2 = ay2 < by2 ? ay2 : by2;
    if (x1 >= x2 || y1 >= y2)
        return 0;
    *ox = x1;
    *oy = y1;
    *ow = x2 - x1;
    *oh = y2 - y1;
    return 1;
}

void surface_blit_damage(const struct win_surface *s, uint32_t dx, uint32_t dy,
                         uint32_t cx, uint32_t cy, uint32_t cw, uint32_t ch) {
    if (!s || !s->px || !s->w || !s->h)
        return;
    int use_clip = cw > 0 && ch > 0;

    if (!surface_is_dirty(s)) {
        if (!use_clip) {
            surface_blit(s, dx, dy);
            return;
        }
        uint32_t ix, iy, iw, ih;
        if (!rect_intersect(dx, dy, s->w, s->h, cx, cy, cw, ch, &ix, &iy, &iw, &ih))
            return;
        surface_blit_rect(s, ix, iy, ix - dx, iy - dy, iw, ih);
        return;
    }

    if (s->dmg_overflow || s->dmg_count == 0) {
        if (!use_clip) {
            surface_blit(s, dx, dy);
            return;
        }
        uint32_t ix, iy, iw, ih;
        if (!rect_intersect(dx, dy, s->w, s->h, cx, cy, cw, ch, &ix, &iy, &iw, &ih))
            return;
        surface_blit_rect(s, ix, iy, ix - dx, iy - dy, iw, ih);
        return;
    }

    for (int i = 0; i < s->dmg_count; i++) {
        const struct surface_rect *r = &s->dmg[i];
        uint32_t sx = r->x, sy = r->y, sw = r->w, sh = r->h;
        uint32_t bx = dx + sx, by = dy + sy;
        if (!use_clip) {
            surface_blit_rect(s, bx, by, sx, sy, sw, sh);
            continue;
        }
        uint32_t ix, iy, iw, ih;
        if (!rect_intersect(bx, by, sw, sh, cx, cy, cw, ch, &ix, &iy, &iw, &ih))
            continue;
        surface_blit_rect(s, ix, iy, ix - dx, iy - dy, iw, ih);
    }
}
