#ifndef PEAK_DOM_INTERNAL_H
#define PEAK_DOM_INTERNAL_H

/* Shared helpers for dom_core.c and dom_parse.c. */

static inline int dom_tag_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

#endif
