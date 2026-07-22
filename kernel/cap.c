#include "cap.h"
#include "util.h"

static uint32_t g_caps = CAP_DEFAULT_SHELL;

void cap_init(void) {
    g_caps = CAP_DEFAULT_SHELL;
}

uint32_t cap_current(void) {
    return g_caps;
}

void cap_set_current(uint32_t caps) {
    g_caps = caps;
}

int cap_check(uint32_t need) {
    return (g_caps & need) == need;
}
