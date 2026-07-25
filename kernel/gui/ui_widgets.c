#include "ui_widgets.h"
#include "fb.h"
#include "desktop_internal.h"

uint32_t ctx_menu_row_h(uint32_t (*scale)(uint32_t)) {
    return fb_cell_h() + scale(4);
}

uint32_t ctx_menu_pad(uint32_t (*scale)(uint32_t)) {
    return scale(8);
}

static uint32_t label_w(const char *label, uint32_t (*scale)(uint32_t)) {
    uint32_t w = scale(24);
    if (!label)
        return w;
    for (const char *p = label; *p; p++)
        w += fb_cell_w();
    return w;
}

void ctx_menu_measure(struct ctx_menu_spec *spec, uint32_t (*scale)(uint32_t)) {
    if (!spec || !spec->items || spec->count <= 0) {
        if (spec) {
            spec->w = scale(120);
            spec->h = scale(40);
        }
        return;
    }
    uint32_t pad = ctx_menu_pad(scale);
    uint32_t row_h = ctx_menu_row_h(scale);
    uint32_t mw = scale(100);
    int rows = 0;
    for (int i = 0; i < spec->count; i++) {
        if (spec->items[i].separator) {
            rows++;
            continue;
        }
        uint32_t lw = label_w(spec->items[i].label, scale);
        if (lw > mw)
            mw = lw;
        rows++;
    }
    spec->w = mw + pad * 2;
    spec->h = pad + (uint32_t)rows * row_h + pad / 2;
}

void ctx_menu_clamp(struct ctx_menu_spec *spec, uint32_t screen_w, uint32_t screen_h,
                    uint32_t (*scale)(uint32_t)) {
    ctx_menu_measure(spec, scale);
    if (!spec)
        return;
    if (spec->x + (int32_t)spec->w > (int32_t)screen_w)
        spec->x = (int32_t)screen_w - (int32_t)spec->w;
    if (spec->y + (int32_t)spec->h > (int32_t)screen_h)
        spec->y = (int32_t)screen_h - (int32_t)spec->h;
    if (spec->x < 0)
        spec->x = 0;
    if (spec->y < 0)
        spec->y = 0;
}

void ctx_menu_draw(const struct ctx_menu_spec *spec, uint32_t (*scale)(uint32_t),
                   uint32_t fg, uint32_t bg, uint32_t accent, uint32_t dim) {
    if (!spec || spec->count <= 0)
        return;
    uint32_t pad = ctx_menu_pad(scale);
    uint32_t row_h = ctx_menu_row_h(scale);
    uint32_t x = (uint32_t)spec->x;
    uint32_t y = (uint32_t)spec->y;
    fb_fill_rect(x, y, spec->w, spec->h, bg);
    fb_fill_rect(x, y, spec->w, scale(2), accent);
    uint32_t cy = y + pad;
    int row = 0;
    for (int i = 0; i < spec->count; i++) {
        const struct ctx_menu_item *it = &spec->items[i];
        if (it->separator) {
            fb_fill_rect(x + pad, cy + row_h / 2, spec->w - pad * 2, scale(1), dim);
            cy += row_h;
            row++;
            continue;
        }
        uint32_t item_fg = it->enabled ? fg : dim;
        uint32_t item_bg = bg;
        if (row == spec->hover_row && it->enabled) {
            item_bg = accent;
            item_fg = bg;
        }
        if (row == spec->hover_row && it->enabled)
            fb_fill_rect(x + scale(2), cy, spec->w - scale(4), row_h, item_bg);
        fb_draw_string(x + pad, cy + scale(2), it->label ? it->label : "", item_fg, item_bg);
        cy += row_h;
        row++;
    }
}

int ctx_menu_hit_row(const struct ctx_menu_spec *spec, int32_t mx, int32_t my,
                     uint32_t (*scale)(uint32_t)) {
    if (!spec || !desktop_point_in(mx, my, (uint32_t)spec->x, (uint32_t)spec->y, spec->w, spec->h))
        return -1;
    uint32_t pad = ctx_menu_pad(scale);
    uint32_t row_h = ctx_menu_row_h(scale);
    int row = (int)((my - spec->y - (int32_t)pad) / (int32_t)row_h);
    if (row < 0)
        return -1;
    int visual = 0;
    for (int i = 0; i < spec->count; i++) {
        if (spec->items[i].separator) {
            if (visual == row)
                return -1;
            visual++;
            continue;
        }
        if (visual == row)
            return spec->items[i].enabled ? i : -1;
        visual++;
    }
    return -1;
}

void ctx_menu_damage_rect(const struct ctx_menu_spec *spec) {
    if (!spec || spec->w == 0 || spec->h == 0)
        return;
    damage_add((uint32_t)spec->x, (uint32_t)spec->y, spec->w, spec->h);
}

int ctx_menu_update_hover(struct ctx_menu_spec *spec, int32_t mx, int32_t my,
                          uint32_t (*scale)(uint32_t)) {
    if (!spec)
        return 0;
    int row = -1;
    if (desktop_point_in(mx, my, (uint32_t)spec->x, (uint32_t)spec->y, spec->w, spec->h)) {
        uint32_t pad = ctx_menu_pad(scale);
        uint32_t row_h = ctx_menu_row_h(scale);
        int vr = (int)((my - spec->y - (int32_t)pad) / (int32_t)row_h);
        int visual = 0;
        for (int i = 0; i < spec->count; i++) {
            if (spec->items[i].separator) {
                if (visual == vr)
                    break;
                visual++;
                continue;
            }
            if (visual == vr && spec->items[i].enabled) {
                row = visual;
                break;
            }
            visual++;
        }
    }
    if (row == spec->hover_row)
        return 0;
    spec->hover_row = row;
    ctx_menu_damage_rect(spec);
    dirty_bits |= DIRTY_MOVE;
    return 1;
}
