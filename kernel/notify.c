#include "notify.h"
#include "fb.h"
#include "theme.h"
#include "timer.h"
#include "util.h"

#define NOTIFY_MAX 4
#define NOTIFY_MSG 96
#define NOTIFY_TTL 300 /* ticks ~3s at 100Hz */

struct toast {
    char msg[NOTIFY_MSG];
    uint64_t until;
    int used;
};

static struct toast toasts[NOTIFY_MAX];
static int toast_dirty;

void notify_init(void) {
    notify_clear();
}

void notify_clear(void) {
    memset(toasts, 0, sizeof(toasts));
    toast_dirty = 0;
}

void notify_push(const char *msg) {
    if (!msg)
        return;
    int slot = -1;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (!toasts[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        slot = 0;
    size_t i = 0;
    for (; msg[i] && i + 1 < NOTIFY_MSG; i++)
        toasts[slot].msg[i] = msg[i];
    toasts[slot].msg[i] = '\0';
    toasts[slot].until = timer_ticks() + NOTIFY_TTL;
    toasts[slot].used = 1;
    toast_dirty = 1;
}

void notify_tick(void) {
    uint64_t now = timer_ticks();
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (toasts[i].used && now >= toasts[i].until) {
            toasts[i].used = 0;
            toast_dirty = 1;
        }
    }
}

int notify_active(void) {
    for (int i = 0; i < NOTIFY_MAX; i++)
        if (toasts[i].used)
            return 1;
    return 0;
}

int notify_consume_dirty(void) {
    int d = toast_dirty;
    toast_dirty = 0;
    return d;
}

void notify_bounds(uint32_t screen_w, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    uint32_t s = fb_ui_scale();
    uint32_t ch = fb_cell_h();
    uint32_t pad = 8 * s;
    uint32_t tw = 220 * s;
    uint32_t th = ch + 10 * s;
    static int last_n = 1;
    int n = 0;
    for (int i = 0; i < NOTIFY_MAX; i++)
        if (toasts[i].used)
            n++;
    /* When the last toast clears, keep prior strip height so erase covers all. */
    if (n < 1)
        n = last_n > 0 ? last_n : 1;
    else
        last_n = n;
    if (x)
        *x = screen_w > tw + pad ? screen_w - tw - pad : pad;
    if (y)
        *y = pad;
    if (w)
        *w = tw;
    if (h)
        *h = (uint32_t)n * (th + 4 * s);
}

int notify_draw(uint32_t screen_w, uint32_t screen_h) {
    (void)screen_h;
    const struct peak_theme *t = theme_get();
    uint32_t s = fb_ui_scale();
    uint32_t ch = fb_cell_h();
    uint32_t pad = 8 * s;
    uint32_t tw = 220 * s;
    uint32_t th = ch + 10 * s;
    int drawn = 0;
    uint32_t y = pad;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (!toasts[i].used)
            continue;
        uint32_t x = screen_w > tw + pad ? screen_w - tw - pad : pad;
        fb_fill_rect(x, y, tw, th, t->surface);
        fb_fill_rect(x, y, 3 * s, th, t->accent);
        fb_draw_string_fit(x + 8 * s, y + 5 * s, tw - 12 * s, toasts[i].msg, t->fg, t->surface);
        y += th + 4 * s;
        drawn = 1;
    }
    return drawn;
}
