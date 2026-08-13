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
/* Shared with window frames, client origins, and hit tests. */
uint32_t desktop_title_h(void);
/* Titlebar min/max/close strip width; also used to clip the title string. */
uint32_t desktop_chrome_btn_strip_w(void);
/* Title text origin + max width: left pad and chrome strip reserved. */
void desktop_title_text_geom(uint32_t win_w, uint32_t *pad_x, uint32_t *pad_y,
                             uint32_t *tw);

#endif
