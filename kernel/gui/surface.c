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
    s->dirty = 1;
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
    s->dirty = 0;
}

void surface_mark_dirty(struct win_surface *s) {
    if (s)
        s->dirty = 1;
}

void surface_blit(const struct win_surface *s, uint32_t dx, uint32_t dy) {
    if (!s || !s->px || !s->w || !s->h)
        return;
    fb_blit_argb(dx, dy, s->w, s->h, s->px, s->stride);
}
