#include "clipboard.h"
#include "cap.h"
#include "random.h"
#include "timer.h"
#include "util.h"

#define CLIPBOARD_SLOTS 4

static char slots[CLIPBOARD_SLOTS][CLIPBOARD_MAX];
static size_t lens[CLIPBOARD_SLOTS];
static uint64_t set_ticks[CLIPBOARD_SLOTS];
static int cur;
static int filled;
static uint64_t ttl_ticks = 3000; /* ~30s at 100Hz */

void clipboard_init(void) {
    clipboard_clear();
}

void clipboard_clear(void) {
    for (int i = 0; i < CLIPBOARD_SLOTS; i++) {
        memzero_explicit(slots[i], CLIPBOARD_MAX);
        lens[i] = 0;
        set_ticks[i] = 0;
    }
    cur = 0;
    filled = 0;
}

void clipboard_set_ttl_ticks(uint64_t t) {
    ttl_ticks = t;
}

static void clipboard_expire(void) {
    if (!lens[cur] || !ttl_ticks)
        return;
    if (timer_ticks() - set_ticks[cur] >= ttl_ticks) {
        memzero_explicit(slots[cur], CLIPBOARD_MAX);
        lens[cur] = 0;
    }
}

void clipboard_set(const char *text, size_t n) {
    if (!cap_check(CAP_CLIPBOARD))
        return;
    if (!text)
        n = 0;
    if (n >= CLIPBOARD_MAX)
        n = CLIPBOARD_MAX - 1;
    if (n && lens[cur] == n && !memcmp(slots[cur], text, n)) {
        set_ticks[cur] = timer_ticks();
        return;
    }
    cur = (cur + 1) % CLIPBOARD_SLOTS;
    if (filled < CLIPBOARD_SLOTS)
        filled++;
    memzero_explicit(slots[cur], CLIPBOARD_MAX);
    if (n && text)
        memcpy(slots[cur], text, n);
    slots[cur][n] = '\0';
    lens[cur] = n;
    set_ticks[cur] = timer_ticks();
}

size_t clipboard_get(char *out, size_t cap) {
    if (!cap_check(CAP_CLIPBOARD))
        return 0;
    clipboard_expire();
    if (!out || cap == 0)
        return 0;
    size_t n = lens[cur];
    if (n + 1 > cap)
        n = cap - 1;
    if (n)
        memcpy(out, slots[cur], n);
    out[n] = '\0';
    return n;
}

size_t clipboard_get_previous(char *out, size_t cap) {
    if (!cap_check(CAP_CLIPBOARD))
        return 0;
    if (filled <= 1)
        return 0;
    cur = (cur + CLIPBOARD_SLOTS - 1) % CLIPBOARD_SLOTS;
    set_ticks[cur] = timer_ticks();
    return clipboard_get(out, cap);
}

int clipboard_has(void) {
    clipboard_expire();
    return lens[cur] > 0;
}

int clipboard_history_count(void) {
    clipboard_expire();
    return filled;
}
