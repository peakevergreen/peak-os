#include "desktop_internal.h"
#include "display_clip.h"
#include "fb.h"

struct damage_rect damage_list[MAX_DAMAGE];
int damage_count;

void damage_clear(void) {
    damage_count = 0;
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

/* Inclusive: overlapping or edge-adjacent. */
static int rects_touch(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah,
                       uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
    return ax <= bx + bw && ax + aw >= bx && ay <= by + bh && ay + ah >= by;
}

/* Collapse overlapping / adjacent entries so later adds can reuse slots. */
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

void damage_add(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct framebuffer *fb = fb_get();
    if (!display_clip_rect((uint32_t)fb->width, (uint32_t)fb->height,
                           x, y, w, h, &x, &y, &w, &h))
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
            /* Compact only when slots are scarce — keeps finer rects while
             * the list has headroom (compose_damage still merges when count
             * is high). */
            if (damage_count >= MAX_DAMAGE - 2)
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

    /* List full: fold into the existing rect with the smallest area growth.
     * Keeps an honest (over-merged) damage set instead of full-frame overflow. */
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

    /* One rect already large vs the screen: further disjoint tracking pays
     * little — collapse to a single bbox immediately. */
    uint64_t screen = rect_area((uint32_t)fb->width, (uint32_t)fb->height);
    uint64_t grown = rect_area(damage_list[best].w, damage_list[best].h);
    if (screen && grown * 4 >= screen) {
        damage_merge_all();
        return;
    }
    damage_compact();
}

void damage_add_win(int idx) {
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open || wins[idx].minimized)
        return;
    damage_add(wins[idx].x, wins[idx].y, wins[idx].w, wins[idx].h);
}

void damage_merge_all(void) {
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
    if (x2 > x1 && y2 > y1)
        damage_add(x1, y1, x2 - x1, y2 - y1);
}
