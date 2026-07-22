#ifndef PEAK_GUI_H
#define PEAK_GUI_H

#include "types.h"

void desktop_init(void);
void desktop_run(void);   /* blocks until returning to CLI */
void desktop_draw(void);
void gui_term_putc(char c);
void gui_term_reset(void);
/* Rewrite current input row: prompt+text with caret + optional selection. */
void gui_term_set_edit(const char *prompt, const char *text, uint32_t caret,
                       int sel_a, int sel_b);

void window_draw_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       const char *title, uint32_t bg);

#endif
