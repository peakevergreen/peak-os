#ifndef PEAK_GUIPROTO_H
#define PEAK_GUIPROTO_H

#include "types.h"
#include "surface.h"

/*
 * Minimal userspace GUI protocol (Wave E).
 * Ring-3 apps issue these ops via SYS_peakgui; buffers attach to surfaces.
 */

#define GUI_OP_NOP       0
#define GUI_OP_CREATE    1
#define GUI_OP_DESTROY   2
#define GUI_OP_DAMAGE    3
#define GUI_OP_SET_TITLE 4
#define GUI_OP_ATTACH    5 /* attach/resize backing store (w,h) */
#define GUI_OP_MOVE      6 /* set geom x,y,w,h */

struct gui_msg {
    uint32_t op;
    uint32_t win_id;
    uint32_t x, y, w, h;
    char title[64];
};

void guiproto_init(void);
/* Handle a protocol message from userspace; returns win_id or 0 on success, -1 on error. */
int  guiproto_dispatch(const struct gui_msg *msg);
uint32_t guiproto_window_count(void);

/* Compositor integration: paint attached surfaces into the scene. */
int guiproto_for_each_surface(void (*fn)(int id, uint32_t x, uint32_t y,
                                         uint32_t w, uint32_t h,
                                         const struct win_surface *s,
                                         void *ctx),
                              void *ctx);
struct win_surface *guiproto_surface(uint32_t win_id);
int guiproto_take_damage(uint32_t win_id, uint32_t *x, uint32_t *y,
                         uint32_t *w, uint32_t *h);

#endif
