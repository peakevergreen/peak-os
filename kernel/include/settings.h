#ifndef PEAK_SETTINGS_H
#define PEAK_SETTINGS_H

#include "types.h"

/* Display preferences persisted under /etc/peak/display (see settings.c). */

void settings_init(void);
void settings_persist(void);

uint32_t settings_gui_scale(void);
void     settings_set_gui_scale(uint32_t scale); /* 1..4 */
void     settings_cycle_gui_scale(void);

int  settings_show_brand(void);
void settings_toggle_brand(void);

int  settings_show_clock(void);
void settings_toggle_clock(void);

/* TLS: TOFU is opt-in continuity; WebPKI is the default trust path. */
int  settings_tls_tofu(void);
void settings_set_tls_tofu(int on);
void settings_toggle_tls_tofu(void);

#endif
