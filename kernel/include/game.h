#ifndef PEAK_GAME_H
#define PEAK_GAME_H

#include "types.h"

/* Peak Runner mini-game — side-scrolling terrain with loot and animals. */
void game_reset(void);
void game_input(char c); /* A/D move, W/Space jump, R reset */
void game_tick(void);
void game_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
uint64_t game_distance(void);
int  game_wants_redraw(void);
void game_clear_redraw(void);

#endif
