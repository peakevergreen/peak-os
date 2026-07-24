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
    /* When set, pixels may be freed under soft-budget pressure (e.g. minimized). */
    int reclaimable;
};

/*
 * Soft memory budget for retained window surfaces (bytes).
 * Host tests may override via -DSURFACE_BUDGET_BYTES=...
 */
#ifndef SURFACE_BUDGET_BYTES
#define SURFACE_BUDGET_BYTES (48ull * 1024ull * 1024ull)
#endif

/* surface_ensure / surface_reclaim status codes */
#define SURFACE_OK          0
#define SURFACE_ERR_INVAL  (-1) /* bad args */
#define SURFACE_ERR_NOMEM  (-2) /* kmalloc failed */
#define SURFACE_ERR_BUDGET (-3) /* soft budget exceeded after reclaim */

void surface_init(void);
uint64_t surface_bytes_used(void);
uint64_t surface_budget(void);
int surface_pressure_pct(void); /* 0–100 */

/* Mark surface reclaimable under budget pressure (minimized / unused). */
void surface_set_reclaimable(struct win_surface *s, int on);

/*
 * Free reclaimable surfaces until at least want_bytes are released, or no
 * reclaimable pixels remain. want_bytes == 0 reclaims every reclaimable
 * surface. Returns bytes freed. skip is never freed.
 */
uint64_t surface_reclaim(uint64_t want_bytes, struct win_surface *skip);

/*
 * Allocate or resize. On soft-budget pressure, reclaims reclaimable surfaces
 * first. Returns SURFACE_OK, or SURFACE_ERR_* (all negative).
 */
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
