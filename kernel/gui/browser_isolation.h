#ifndef PEAK_BROWSER_ISOLATION_H
#define PEAK_BROWSER_ISOLATION_H

#include "types.h"

/* Ring-3 browser isolation scaffold (process boundary not yet available). */
void browser_isolation_init(void);
int browser_ring3_available(void);
/* When enforce=1 and ring-3 unavailable, DOM/net bridges fail closed. */
void browser_isolation_set_enforce(int on);
int browser_isolation_enforced(void);
int browser_isolation_dom_allowed(void);
int browser_isolation_net_allowed(void);
void browser_isolation_status_line(char *out, size_t cap);

#endif
