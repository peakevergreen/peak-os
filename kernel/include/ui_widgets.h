#ifndef PEAK_UI_WIDGETS_H
#define PEAK_UI_WIDGETS_H

#include "types.h"

struct ctx_menu_item {
    const char *label;
    int enabled;
    int separator;
    int action_id;
};

struct ctx_menu_spec {
    struct ctx_menu_item *items;
    int count;
    int32_t x, y;
    uint32_t w, h;
    int hover_row;
};

uint32_t ctx_menu_row_h(uint32_t (*scale)(uint32_t));
uint32_t ctx_menu_pad(uint32_t (*scale)(uint32_t));
void ctx_menu_measure(struct ctx_menu_spec *spec, uint32_t (*scale)(uint32_t));
void ctx_menu_clamp(struct ctx_menu_spec *spec, uint32_t screen_w, uint32_t screen_h,
                    uint32_t (*scale)(uint32_t));
void ctx_menu_draw(const struct ctx_menu_spec *spec, uint32_t (*scale)(uint32_t),
                   uint32_t fg, uint32_t bg, uint32_t accent, uint32_t dim);
int ctx_menu_hit_row(const struct ctx_menu_spec *spec, int32_t mx, int32_t my,
                     uint32_t (*scale)(uint32_t));
void ctx_menu_damage_rect(const struct ctx_menu_spec *spec);
int ctx_menu_update_hover(struct ctx_menu_spec *spec, int32_t mx, int32_t my,
                          uint32_t (*scale)(uint32_t));

#endif
