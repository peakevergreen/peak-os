#ifndef PEAK_BROWSER_H
#define PEAK_BROWSER_H

#include "types.h"

void browser_reset(void);
void browser_input(char c);
void browser_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void browser_go(const char *url);
/* Navigate / reload a specific tab slot (not necessarily the active tab). */
void browser_go_tab(int tab, const char *url);
void browser_back(void);
void browser_forward(void);
void browser_reload(void);
void browser_reload_tab(int tab);
/* Tab index owning this JS host, or -1 if unknown. */
struct browser_js_host;
int browser_tab_index_for_js_host(const struct browser_js_host *h);
/* Click in browser content coords (relative to draw origin). */
void browser_click(int32_t lx, int32_t ly, uint32_t w, uint32_t h);
int  browser_wants_redraw(void);
void browser_clear_wants_redraw(void);
/* Zero cached chrome hit rects (call on UI scale change before redraw). */
void browser_clear_hit_rects(void);
/* Rebuild CSS layouts for all tabs after UI scale or Browser resize. */
void browser_on_ui_scale(void);
/* Drain per-tab JS timers/microtasks; may set wants_redraw. */
void browser_tick(void);
/* Aggregate JS metrics across tabs (for Monitor). */
void browser_js_metrics(uint32_t *tabs_with_js, uint32_t *objs, uint32_t *timers,
                        uint32_t *gc_runs);

#endif
