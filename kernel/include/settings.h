#ifndef PEAK_SETTINGS_H
#define PEAK_SETTINGS_H

#include "types.h"

/* OS preferences (display + general). Persisted under /etc/peak/. */

void settings_init(void);
void settings_persist(void);

uint32_t settings_gui_scale(void);
void     settings_set_gui_scale(uint32_t scale); /* 1..4 */
void     settings_cycle_gui_scale(void);

int  settings_show_brand(void);
void settings_toggle_brand(void);

int  settings_show_clock(void);
void settings_toggle_clock(void);

#endif
