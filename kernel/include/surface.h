#ifndef PEAK_SURFACE_H
#define PEAK_SURFACE_H

#include "types.h"

/* Per-window ARGB surface for retained-mode compositing. */
struct win_surface {
    uint32_t *px;
    uint32_t w, h;
    uint32_t stride; /* pixels per row */
    int dirty;       /* client content needs redraw into px */
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
void surface_mark_dirty(struct win_surface *s);

/* Blit opaque surface into current fb draw target at (dx,dy). */
void surface_blit(const struct win_surface *s, uint32_t dx, uint32_t dy);

#endif
