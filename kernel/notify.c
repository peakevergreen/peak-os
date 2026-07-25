#include "notify.h"
#include "fb.h"
#include "theme.h"
#include "timer.h"
#include "util.h"

#define NOTIFY_MAX 4
#define NOTIFY_HIST 8
#define NOTIFY_MSG 96
#define NOTIFY_TTL 300 /* ticks ~3s at 100Hz */

struct toast {
    char msg[NOTIFY_MSG];
    uint64_t until;
    int used;
};

struct hist_entry {
    char msg[NOTIFY_MSG];
    uint64_t when;
};

static struct toast toasts[NOTIFY_MAX];
static struct hist_entry hist[NOTIFY_HIST];
static int hist_head;
static int hist_count;
static int toast_dirty;

static void notify_hist_push(const char *msg) {
    if (!msg || !msg[0])
        return;
    if (hist_count > 0) {
        int prev = (hist_head + NOTIFY_HIST - 1) % NOTIFY_HIST;
        if (!strcmp(hist[prev].msg, msg))
            return;
    }
    size_t i = 0;
    for (; msg[i] && i + 1 < NOTIFY_MSG; i++)
        hist[hist_head].msg[i] = msg[i];
    hist[hist_head].msg[i] = '\0';
    hist[hist_head].when = timer_ticks();
    hist_head = (hist_head + 1) % NOTIFY_HIST;
    if (hist_count < NOTIFY_HIST)
        hist_count++;
}

static int notify_active_index(int display_idx) {
    int n = 0;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (!toasts[i].used)
            continue;
        if (n == display_idx)
            return i;
        n++;
    }
    return -1;
}

static void notify_toast_metrics(uint32_t *tw, uint32_t *th, uint32_t *pad, uint32_t *s) {
    uint32_t scale = fb_ui_scale();
    if (s)
        *s = scale;
    if (pad)
        *pad = 8 * scale;
    if (tw)
        *tw = 240 * scale;
    if (th)
        *th = fb_cell_h() + 10 * scale;
}

void notify_init(void) {
    notify_clear();
}

void notify_clear(void) {
    memset(toasts, 0, sizeof(toasts));
    memset(hist, 0, sizeof(hist));
    hist_head = hist_count = 0;
    toast_dirty = 0;
}

void notify_push(const char *msg) {
    if (!msg)
        return;
    notify_hist_push(msg);
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (toasts[i].used && !strcmp(toasts[i].msg, msg)) {
            toasts[i].until = timer_ticks() + NOTIFY_TTL;
            toast_dirty = 1;
            return;
        }
    }
    int slot = -1;
    uint64_t oldest = (uint64_t)-1;
    int oldest_i = 0;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (!toasts[i].used) {
            slot = i;
            break;
        }
        if (toasts[i].until < oldest) {
            oldest = toasts[i].until;
            oldest_i = i;
        }
    }
    if (slot < 0)
        slot = oldest_i;
    size_t i = 0;
    for (; msg[i] && i + 1 < NOTIFY_MSG; i++)
        toasts[slot].msg[i] = msg[i];
    toasts[slot].msg[i] = '\0';
    toasts[slot].until = timer_ticks() + NOTIFY_TTL;
    toasts[slot].used = 1;
    toast_dirty = 1;
}

void notify_push_clipboard(const char *what) {
    char msg[NOTIFY_MSG];
    if (what && what[0])
        snprintf(msg, sizeof(msg), "Copied %s to clipboard", what);
    else
        snprintf(msg, sizeof(msg), "Copied to clipboard");
    notify_push(msg);
}

void notify_dismiss(int display_idx) {
    int idx = notify_active_index(display_idx);
    if (idx < 0)
        return;
    toasts[idx].used = 0;
    toast_dirty = 1;
}

void notify_dismiss_all(void) {
    int changed = 0;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (toasts[i].used) {
            toasts[i].used = 0;
            changed = 1;
        }
    }
    if (changed)
        toast_dirty = 1;
}

int notify_history_count(void) {
    return hist_count;
}

int notify_history_get(int idx, char *out, size_t cap) {
    if (idx < 0 || idx >= hist_count || !out || cap == 0)
        return 0;
    int slot = (hist_head + NOTIFY_HIST - 1 - idx) % NOTIFY_HIST;
    size_t i = 0;
    for (; hist[slot].msg[i] && i + 1 < cap; i++)
        out[i] = hist[slot].msg[i];
    out[i] = '\0';
    return 1;
}

int notify_click(int32_t mx, int32_t my, uint32_t screen_w) {
    if (!notify_active())
        return 0;
    uint32_t tw, th, pad, s;
    notify_toast_metrics(&tw, &th, &pad, &s);
    uint32_t x = screen_w > tw + pad ? screen_w - tw - pad : pad;
    uint32_t y = pad;
    uint32_t btn = 14 * s;
    int display = 0;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (!toasts[i].used)
            continue;
        uint32_t bx = x + tw - btn - 4 * s;
        uint32_t by = y + (th > btn ? (th - btn) / 2 : 0);
        if ((uint32_t)mx >= bx && (uint32_t)mx < bx + btn &&
            (uint32_t)my >= by && (uint32_t)my < by + btn) {
            notify_dismiss(display);
            return 1;
        }
        y += th + 4 * s;
        display++;
    }
    return 0;
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
    uint32_t tw, th, pad, s;
    notify_toast_metrics(&tw, &th, &pad, &s);
    static int last_n = 1;
    int n = 0;
    for (int i = 0; i < NOTIFY_MAX; i++)
        if (toasts[i].used)
            n++;
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
    uint32_t tw, th, pad, s;
    notify_toast_metrics(&tw, &th, &pad, &s);
    int drawn = 0;
    uint32_t y = pad;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (!toasts[i].used)
            continue;
        uint32_t x = screen_w > tw + pad ? screen_w - tw - pad : pad;
        fb_fill_rect(x, y, tw, th, t->surface);
        fb_fill_rect(x, y, 3 * s, th, t->accent);
        fb_draw_string_fit(x + 8 * s, y + 5 * s, tw - 28 * s, toasts[i].msg, t->fg, t->surface);
        uint32_t btn = 14 * s;
        uint32_t bx = x + tw - btn - 4 * s;
        uint32_t by = y + (th > btn ? (th - btn) / 2 : 0);
        fb_fill_rect(bx, by, btn, btn, t->bg);
        fb_draw_string(bx + 4 * s, by + 1 * s, "x", t->dim, t->bg);
        y += th + 4 * s;
        drawn = 1;
    }
    return drawn;
}
