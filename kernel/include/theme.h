#ifndef PEAK_THEME_H
#define PEAK_THEME_H

#include "types.h"

struct peak_theme {
    const char *name;
    uint32_t bg;
    uint32_t fg;
    uint32_t dim;
    uint32_t accent;
    uint32_t danger;
    uint32_t surface;
    uint32_t border;
    uint32_t title;
    uint32_t cursor;
};

void theme_init(void);
const struct peak_theme *theme_get(void);
const char *theme_name(void);
int  theme_set(const char *name); /* 0 ok */
void theme_next(void);
void theme_apply_console(void);
int  theme_list(char *out, size_t out_len); /* newline-separated names */
void theme_persist(void); /* write /etc/peak/theme */

#endif
