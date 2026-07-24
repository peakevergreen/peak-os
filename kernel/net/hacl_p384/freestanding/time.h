#if defined(PEAK_HOST_TEST)
#include_next <time.h>
#else
#ifndef PEAK_HACL_TIME_H
#define PEAK_HACL_TIME_H
#ifndef NULL
#define NULL ((void *)0)
#endif
typedef long time_t;
/* Unused on ECDSA verify path; satisfy krml target.h. */
static inline time_t time(time_t *t) {
    if (t)
        *t = 0;
    return 0;
}
#endif
#endif
