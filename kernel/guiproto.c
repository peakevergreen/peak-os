#include "guiproto.h"
#include "heap.h"
#include "util.h"

#define GUI_MAX_WINS 16

static struct {
    int used;
    uint32_t x, y, w, h;
    char title[64];
    struct win_surface surf;
    int has_damage;
    uint32_t dx, dy, dw, dh;
} wins[GUI_MAX_WINS];

void guiproto_init(void) {
    for (int i = 0; i < GUI_MAX_WINS; i++) {
        if (wins[i].used)
            surface_free(&wins[i].surf);
    }
    memset(wins, 0, sizeof(wins));
}

uint32_t guiproto_window_count(void) {
    uint32_t n = 0;
    for (int i = 0; i < GUI_MAX_WINS; i++)
        if (wins[i].used)
            n++;
    return n;
}

struct win_surface *guiproto_surface(uint32_t win_id) {
    if (win_id >= GUI_MAX_WINS || !wins[win_id].used)
        return NULL;
    return &wins[win_id].surf;
}

int guiproto_take_damage(uint32_t win_id, uint32_t *x, uint32_t *y,
                         uint32_t *w, uint32_t *h) {
    if (win_id >= GUI_MAX_WINS || !wins[win_id].used || !wins[win_id].has_damage)
        return 0;
    if (x) *x = wins[win_id].dx;
    if (y) *y = wins[win_id].dy;
    if (w) *w = wins[win_id].dw;
    if (h) *h = wins[win_id].dh;
    wins[win_id].has_damage = 0;
    return 1;
}

int guiproto_for_each_surface(void (*fn)(int id, uint32_t x, uint32_t y,
                                         uint32_t w, uint32_t h,
                                         const struct win_surface *s,
                                         void *ctx),
                              void *ctx) {
    if (!fn)
        return -1;
    int n = 0;
    for (int i = 0; i < GUI_MAX_WINS; i++) {
        if (!wins[i].used || !wins[i].surf.px)
            continue;
        fn(i, wins[i].x, wins[i].y, wins[i].w, wins[i].h, &wins[i].surf, ctx);
        n++;
    }
    return n;
}

int guiproto_dispatch(const struct gui_msg *msg) {
    if (!msg)
        return -1;
    switch (msg->op) {
    case GUI_OP_NOP:
        return 0;
    case GUI_OP_CREATE: {
        for (int i = 0; i < GUI_MAX_WINS; i++) {
            if (!wins[i].used) {
                memset(&wins[i], 0, sizeof(wins[i]));
                wins[i].used = 1;
                wins[i].x = msg->x;
                wins[i].y = msg->y;
                wins[i].w = msg->w ? msg->w : 200;
                wins[i].h = msg->h ? msg->h : 120;
                size_t n = 0;
                for (; msg->title[n] && n + 1 < sizeof(wins[i].title); n++)
                    wins[i].title[n] = msg->title[n];
                wins[i].title[n] = '\0';
                /* Surface allocated on ATTACH — CREATE alone is geometry only. */
                return i;
            }
        }
        return -1;
    }
    case GUI_OP_DESTROY:
        if (msg->win_id >= GUI_MAX_WINS || !wins[msg->win_id].used)
            return -1;
        surface_free(&wins[msg->win_id].surf);
        wins[msg->win_id].used = 0;
        return 0;
    case GUI_OP_ATTACH: {
        if (msg->win_id >= GUI_MAX_WINS || !wins[msg->win_id].used)
            return -1;
        uint32_t aw = msg->w ? msg->w : wins[msg->win_id].w;
        uint32_t ah = msg->h ? msg->h : wins[msg->win_id].h;
        if (aw == 0)
            aw = 200;
        if (ah == 0)
            ah = 120;
        wins[msg->win_id].w = aw;
        wins[msg->win_id].h = ah;
        if (surface_ensure(&wins[msg->win_id].surf, aw, ah) != 0)
            return -1;
        wins[msg->win_id].has_damage = 1;
        wins[msg->win_id].dx = wins[msg->win_id].x;
        wins[msg->win_id].dy = wins[msg->win_id].y;
        wins[msg->win_id].dw = aw;
        wins[msg->win_id].dh = ah;
        return 0;
    }
    case GUI_OP_MOVE:
        if (msg->win_id >= GUI_MAX_WINS || !wins[msg->win_id].used)
            return -1;
        wins[msg->win_id].x = msg->x;
        wins[msg->win_id].y = msg->y;
        if (msg->w)
            wins[msg->win_id].w = msg->w;
        if (msg->h)
            wins[msg->win_id].h = msg->h;
        wins[msg->win_id].has_damage = 1;
        wins[msg->win_id].dx = wins[msg->win_id].x;
        wins[msg->win_id].dy = wins[msg->win_id].y;
        wins[msg->win_id].dw = wins[msg->win_id].w;
        wins[msg->win_id].dh = wins[msg->win_id].h;
        return 0;
    case GUI_OP_DAMAGE:
    case GUI_OP_SET_TITLE:
        if (msg->win_id >= GUI_MAX_WINS || !wins[msg->win_id].used)
            return -1;
        if (msg->op == GUI_OP_SET_TITLE) {
            size_t n = 0;
            for (; msg->title[n] && n + 1 < sizeof(wins[msg->win_id].title); n++)
                wins[msg->win_id].title[n] = msg->title[n];
            wins[msg->win_id].title[n] = '\0';
        } else {
            wins[msg->win_id].has_damage = 1;
            wins[msg->win_id].dx = msg->w ? msg->x : wins[msg->win_id].x;
            wins[msg->win_id].dy = msg->h ? msg->y : wins[msg->win_id].y;
            wins[msg->win_id].dw = msg->w ? msg->w : wins[msg->win_id].w;
            wins[msg->win_id].dh = msg->h ? msg->h : wins[msg->win_id].h;
            surface_mark_dirty(&wins[msg->win_id].surf);
        }
        return 0;
    default:
        return -1;
    }
}
