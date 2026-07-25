#ifndef PEAK_WALLPAPER_H
#define PEAK_WALLPAPER_H

#include "types.h"

void wallpaper_init(void);
const char *wallpaper_path(void); /* "" when solid theme bg */
int  wallpaper_enabled(void);
int  wallpaper_option_count(void);
int  wallpaper_option_index(void);
const char *wallpaper_option_label(int i);
int  wallpaper_set(const char *path); /* "" or "none" → solid; else PPM path */
void wallpaper_select_index(int i);
void wallpaper_next(void);            /* cycle none ↔ built-in evergreen */
void wallpaper_persist(void);
void wallpaper_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

#endif
void wallpaper_draw_option(int i, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
