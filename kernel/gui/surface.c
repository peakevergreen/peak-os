#include "surface.h"
#include "display_clip.h"
#include "fb.h"
#include "heap.h"
#include "serial.h"
#include "util.h"

/* Desktop MAX_WINS + guiproto GUI_MAX_WINS, with headroom. */
#define SURFACE_LIVE_MAX 32

static uint64_t g_surf_bytes;
static int g_warned;
static struct win_surface *g_live[SURFACE_LIVE_MAX];
static int g_live_n;

void surface_init(void) {
    g_surf_bytes = 0;
    g_warned = 0;
    g_live_n = 0;
    for (int i = 0; i < SURFACE_LIVE_MAX; i++)
        g_live[i] = NULL;
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

static void live_add(struct win_surface *s) {
    if (!s)
        return;
    for (int i = 0; i < g_live_n; i++) {
        if (g_live[i] == s)
            return;
    }
    if (g_live_n >= SURFACE_LIVE_MAX)
        return;
    g_live[g_live_n++] = s;
}

static void live_del(struct win_surface *s) {
    if (!s)
        return;
    for (int i = 0; i < g_live_n; i++) {
        if (g_live[i] != s)
            continue;
        g_live[i] = g_live[g_live_n - 1];
        g_live[g_live_n - 1] = NULL;
        g_live_n--;
        return;
    }
}

void surface_set_reclaimable(struct win_surface *s, int on) {
    if (!s)
        return;
    s->reclaimable = on ? 1 : 0;
}

static void surf_damage_clear(struct win_surface *s) {
    s->dmg_count = 0;
    s->dmg_overflow = 0;
}

static void surf_damage_add(struct win_surface *s, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h) {
    if (!s || s->dmg_overflow)
        return;
    if (!display_clip_rect(s->w, s->h, x, y, w, h, &x, &y, &w, &h))
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

/*
 * Drop pixel backing for reclaimable live surfaces (minimized / unused) until
 * want_bytes have been released, or nothing reclaimable remains.
 */
uint64_t surface_reclaim(uint64_t want_bytes, struct win_surface *skip) {
    uint64_t freed = 0;

    for (;;) {
        struct win_surface *victim = NULL;
        for (int i = 0; i < g_live_n; i++) {
            struct win_surface *s = g_live[i];
            if (!s || s == skip || !s->reclaimable || !s->px)
                continue;
            victim = s;
            break;
        }
        if (!victim)
            break;

        uint64_t b = surf_bytes(victim->w, victim->h);
        kfree(victim->px);
        victim->px = NULL;
        victim->w = victim->h = victim->stride = 0;
        surf_damage_clear(victim);
        victim->reclaimable = 0;

        if (g_surf_bytes >= b)
            g_surf_bytes -= b;
        else
            g_surf_bytes = 0;
        freed += b;
        live_del(victim);
        serial_write_str("surface: reclaimed unused/closed surface under budget pressure\n");

        /* want_bytes == 0 → reclaim every reclaimable surface. */
        if (want_bytes > 0 && freed >= want_bytes)
            break;
    }
    return freed;
}

int surface_ensure(struct win_surface *s, uint32_t w, uint32_t h) {
    if (!s || !w || !h)
        return SURFACE_ERR_INVAL;
    if (s->px && s->w == w && s->h == h) {
        /* Actively retained — do not reclaim this surface under pressure. */
        s->reclaimable = 0;
        return SURFACE_OK;
    }

    uint64_t old_b = s->px ? surf_bytes(s->w, s->h) : 0;
    uint64_t new_b = surf_bytes(w, h);
    uint64_t after = g_surf_bytes - old_b + new_b;

    if (SURFACE_BUDGET_BYTES > 0 && after > SURFACE_BUDGET_BYTES) {
        /* Prefer reclaiming minimized/unused surfaces over refusing the alloc. */
        surface_reclaim(after - SURFACE_BUDGET_BYTES, s);
        after = g_surf_bytes - old_b + new_b;
        if (after > SURFACE_BUDGET_BYTES) {
            if (!g_warned) {
                serial_write_str(
                    "surface: soft budget exceeded — alloc refused after reclaim\n");
                g_warned = 1;
            }
            return SURFACE_ERR_BUDGET;
        }
    }

    uint32_t *np = (uint32_t *)kmalloc((size_t)new_b);
    if (!np)
        return SURFACE_ERR_NOMEM;
    memset(np, 0, (size_t)new_b);
    if (s->px)
        kfree(s->px);
    else
        live_add(s);
    g_surf_bytes = after;
    s->px = np;
    s->w = w;
    s->h = h;
    s->stride = w;
    s->reclaimable = 0;
    surface_mark_dirty(s);
    return SURFACE_OK;
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
        live_del(s);
    }
    s->px = NULL;
    s->w = s->h = s->stride = 0;
    s->reclaimable = 0;
    surf_damage_clear(s);
}

void surface_blit_rect(const struct win_surface *s, uint32_t dx, uint32_t dy,
                       uint32_t sx, uint32_t sy, uint32_t w, uint32_t h) {
    if (!s || !s->px)
        return;
    if (!display_clip_rect(s->w, s->h, sx, sy, w, h, &sx, &sy, &w, &h))
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
