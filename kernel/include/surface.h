#ifndef PEAK_SURFACE_H
#define PEAK_SURFACE_H

#include "types.h"

#define SURF_MAX_DAMAGE 8

struct surface_rect {
    uint32_t x, y, w, h;
};

/* Per-window ARGB surface for retained-mode compositing. */
struct win_surface {
    uint32_t *px;
    uint32_t w, h;
    uint32_t stride; /* pixels per row */
    struct surface_rect dmg[SURF_MAX_DAMAGE];
    int dmg_count;
    int dmg_overflow; /* full surface needs redraw */
};

/* Soft memory budget for wallpaper + back + surfaces (bytes). */
#define SURFACE_BUDGET_BYTES (48ull * 1024ull * 1024ull)

void surface_init(void);
uint64_t surface_bytes_used(void);
uint64_t surface_budget(void);
int surface_pressure_pct(void); /* 0–100 */

/* Allocate or resize. Soft-fails (returns -1) under budget pressure. */
int  surface_ensure(struct win_surface *s, uint32_t w, uint32_t h);
void surface_free(struct win_surface *s);

int  surface_is_dirty(const struct win_surface *s);
void surface_mark_dirty(struct win_surface *s);
void surface_mark_dirty_rect(struct win_surface *s, uint32_t x, uint32_t y,
                             uint32_t w, uint32_t h);
void surface_damage_clear(struct win_surface *s);

/* Blit opaque surface into current fb draw target at (dx,dy). */
void surface_blit(const struct win_surface *s, uint32_t dx, uint32_t dy);
/* Blit a sub-rectangle of the surface. */
void surface_blit_rect(const struct win_surface *s, uint32_t dx, uint32_t dy,
                       uint32_t sx, uint32_t sy, uint32_t w, uint32_t h);
/*
 * Blit dirty regions to (dx,dy). Optional screen clip (cx,cy,cw,ch); pass
 * cw=0 to skip clipping. Clean surfaces blit only the clip intersection.
 */
void surface_blit_damage(const struct win_surface *s, uint32_t dx, uint32_t dy,
                         uint32_t cx, uint32_t cy, uint32_t cw, uint32_t ch);

#endif
