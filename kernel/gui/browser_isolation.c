#include "browser_isolation.h"

static int enforce_ring3;

void browser_isolation_init(void) {
    enforce_ring3 = 0;
}

int browser_ring3_available(void) {
    return 0; /* scaffold: no ring-3 process yet */
}

void browser_isolation_set_enforce(int on) {
    enforce_ring3 = on ? 1 : 0;
}

int browser_isolation_enforced(void) {
    return enforce_ring3;
}

int browser_isolation_dom_allowed(void) {
    if (!enforce_ring3)
        return 1;
    return browser_ring3_available();
}

int browser_isolation_net_allowed(void) {
    if (!enforce_ring3)
        return 1;
    return browser_ring3_available();
}
