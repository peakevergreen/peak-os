#include "clipboard.h"
#include "cap.h"
#include "random.h"
#include "timer.h"
#include "util.h"

static char buf[CLIPBOARD_MAX];
static size_t len;
static uint64_t set_tick;
static uint64_t ttl_ticks = 3000; /* ~30s at 100Hz */

void clipboard_init(void) {
    clipboard_clear();
}

void clipboard_clear(void) {
    memzero_explicit(buf, sizeof(buf));
    len = 0;
    set_tick = 0;
}

void clipboard_set_ttl_ticks(uint64_t t) {
    ttl_ticks = t;
}

static void clipboard_expire(void) {
    if (!len || !ttl_ticks)
        return;
    if (timer_ticks() - set_tick >= ttl_ticks) {
        memzero_explicit(buf, sizeof(buf));
        len = 0;
    }
}

void clipboard_set(const char *text, size_t n) {
    if (!cap_check(CAP_CLIPBOARD))
        return;
    if (!text)
        n = 0;
    if (n >= CLIPBOARD_MAX)
        n = CLIPBOARD_MAX - 1;
    memzero_explicit(buf, sizeof(buf));
    if (n && text)
        memcpy(buf, text, n);
    buf[n] = '\0';
    len = n;
    set_tick = timer_ticks();
}

size_t clipboard_get(char *out, size_t cap) {
    if (!cap_check(CAP_CLIPBOARD))
        return 0;
    clipboard_expire();
    if (!out || cap == 0)
        return 0;
    size_t n = len;
    if (n + 1 > cap)
        n = cap - 1;
    if (n)
        memcpy(out, buf, n);
    out[n] = '\0';
    return n;
}

int clipboard_has(void) {
    clipboard_expire();
    return len > 0;
}
