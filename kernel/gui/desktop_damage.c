#include "desktop_internal.h"
#include "fb.h"

struct damage_rect damage_list[MAX_DAMAGE];
int damage_count;
int damage_overflow;

void damage_clear(void) {
    damage_count = 0;
    damage_overflow = 0;
}

void damage_add(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct framebuffer *fb = fb_get();
    if (!w || !h || damage_overflow)
        return;
    if (x >= fb->width || y >= fb->height)
        return;
    if (x + w > fb->width)
        w = (uint32_t)fb->width - x;
    if (y + h > fb->height)
        h = (uint32_t)fb->height - y;
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
    }
    if (damage_count >= MAX_DAMAGE) {
        damage_overflow = 1;
        return;
    }
    damage_list[damage_count].x = x;
    damage_list[damage_count].y = y;
    damage_list[damage_count].w = w;
    damage_list[damage_count].h = h;
    damage_count++;
}

void damage_add_win(int idx) {
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open || wins[idx].minimized)
        return;
    damage_add(wins[idx].x, wins[idx].y, wins[idx].w, wins[idx].h);
}

void damage_merge_all(void) {
    if (damage_count <= 1 && !damage_overflow)
        return;
    uint32_t x1 = (uint32_t)-1, y1 = (uint32_t)-1, x2 = 0, y2 = 0;
    int any = 0;
    if (damage_overflow) {
        struct framebuffer *fb = fb_get();
        damage_clear();
        damage_add(0, 0, (uint32_t)fb->width, (uint32_t)fb->height);
        return;
    }
    for (int i = 0; i < damage_count; i++) {
        struct damage_rect *r = &damage_list[i];
        if (r->x < x1) x1 = r->x;
        if (r->y < y1) y1 = r->y;
        if (r->x + r->w > x2) x2 = r->x + r->w;
        if (r->y + r->h > y2) y2 = r->y + r->h;
        any = 1;
    }
    damage_clear();
    if (any && x2 > x1 && y2 > y1)
        damage_add(x1, y1, x2 - x1, y2 - y1);
}
