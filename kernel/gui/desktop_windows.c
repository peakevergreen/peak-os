#include "desktop_internal.h"
#include "gui.h"
#include "fb.h"
#include "theme.h"
#include "shell.h"
#include "settings.h"
#include "util.h"
#include "game.h"
#include "browser.h"
#include "monitor.h"
#include "notify.h"
#include "heap.h"

struct win wins[MAX_WINS];
int focus = -1;
int dragging;
int resizing;
int resize_edge;
int32_t drag_off_x, drag_off_y;
uint32_t resize_orig_w, resize_orig_h;
uint32_t resize_orig_x, resize_orig_y;
int32_t resize_origin_x, resize_origin_y;
uint32_t move_prev_x, move_prev_y, move_prev_w, move_prev_h;
int move_prev_valid;
uint32_t *move_pixmap, *move_underlay;
uint32_t move_pw, move_ph;
int move_live;
uint32_t band_x, band_y, band_w, band_h;
int band_live;

static uint32_t resize_grip(void) {
    uint32_t g = desktop_u(14);
    return g < 12 ? 12 : g;
}

int desktop_point_in(int32_t px, int32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

uint32_t desktop_win_min_w(void) { return desktop_u(180); }
uint32_t desktop_win_min_h(void) { return desktop_title_h() + desktop_u(100); }

int desktop_hit_resize_grip(struct win *w, int32_t mx, int32_t my) {
    uint32_t g = resize_grip();
    return desktop_point_in(mx, my, w->x + w->w - g, w->y + w->h - g, g, g);
}

int desktop_hit_resize_edge(struct win *w, int32_t mx, int32_t my) {
    uint32_t e = desktop_u(6);
    int m = 0;
    if (desktop_point_in(mx, my, w->x, w->y, e, w->h))
        m |= 1;
    if (desktop_point_in(mx, my, w->x + w->w - e, w->y, e, w->h))
        m |= 2;
    if (desktop_point_in(mx, my, w->x, w->y, w->w, e))
        m |= 4;
    if (desktop_point_in(mx, my, w->x, w->y + w->h - e, w->w, e))
        m |= 8;
    if (desktop_hit_resize_grip(w, mx, my))
        m |= 2 | 8;
    return m;
}

void desktop_clamp_win_geom(struct win *w) {
    struct framebuffer *fb = fb_get();
    uint32_t tb = desktop_taskbar_h();
    uint32_t max_w = (uint32_t)fb->width;
    uint32_t max_h = (uint32_t)fb->height > tb ? (uint32_t)fb->height - tb : (uint32_t)fb->height;
    if (w->w < desktop_win_min_w())
        w->w = desktop_win_min_w();
    if (w->h < desktop_win_min_h())
        w->h = desktop_win_min_h();
    if (w->w > max_w)
        w->w = max_w;
    if (w->h > max_h)
        w->h = max_h;
    if (w->x + w->w > max_w)
        w->x = max_w > w->w ? max_w - w->w : 0;
    if (w->y + w->h > max_h)
        w->y = max_h > w->h ? max_h - w->h : 0;
}

void desktop_rescale_windows(void) {
    struct framebuffer *fb = fb_get();
    for (int i = 0; i < MAX_WINS; i++) {
        if (!wins[i].open)
            continue;
        if (wins[i].maximized) {
            wins[i].x = 0;
            wins[i].y = 0;
            wins[i].w = (uint32_t)fb->width;
            wins[i].h = (uint32_t)fb->height > desktop_taskbar_h()
                            ? (uint32_t)fb->height - desktop_taskbar_h()
                            : (uint32_t)fb->height;
        } else {
            desktop_clamp_win_geom(&wins[i]);
        }
        surface_ensure(&wins[i].surf, wins[i].w, wins[i].h);
        surface_mark_dirty(&wins[i].surf);
    }
}

const char *desktop_app_title(enum app_kind k) {
    switch (k) {
    case APP_TERM: return "Terminal";
    case APP_FILES: return "Files";
    case APP_SETTINGS: return "Settings";
    case APP_AGENT: return "Agent";
    case APP_GAME: return "Peak Runner";
    case APP_BROWSER: return "Browser";
    case APP_MONITOR: return "Monitor";
    }
    return "Window";
}

int desktop_find_win(enum app_kind k) {
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open && wins[i].kind == k)
            return i;
    return -1;
}

void desktop_raise_win(int idx) {
    int prev_focus = focus;
    int maxz = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open && wins[i].z > maxz)
            maxz = wins[i].z;
    wins[idx].z = maxz + 1;
    focus = idx;
    if (wins[idx].kind == APP_TERM)
        desktop_term_activate(idx);
    if (prev_focus >= 0 && prev_focus != idx)
        damage_add_win(prev_focus);
    damage_add_win(idx);
    dirty_bits |= DIRTY_MOVE;
}

void desktop_maximize_win(int idx) {
    struct framebuffer *fb = fb_get();
    struct win *w = &wins[idx];
    uint32_t ox = w->x, oy = w->y, ow = w->w, oh = w->h;
    if (!w->maximized) {
        w->rx = w->x;
        w->ry = w->y;
        w->rw = w->w;
        w->rh = w->h;
        w->x = 0;
        w->y = 0;
        w->w = (uint32_t)fb->width;
        w->h = (uint32_t)fb->height > desktop_taskbar_h()
                   ? (uint32_t)fb->height - desktop_taskbar_h()
                   : (uint32_t)fb->height;
        w->maximized = 1;
        w->minimized = 0;
    } else {
        w->x = w->rx;
        w->y = w->ry;
        w->w = w->rw;
        w->h = w->rh;
        w->maximized = 0;
        desktop_clamp_win_geom(w);
    }
    damage_add(ox, oy, ow, oh);
    damage_add(w->x, w->y, w->w, w->h);
    surface_ensure(&w->surf, w->w, w->h);
    surface_mark_dirty(&w->surf);
    dirty_bits |= DIRTY_MOVE;
}

void desktop_minimize_win(int idx) {
    uint32_t ox = wins[idx].x, oy = wins[idx].y, ow = wins[idx].w, oh = wins[idx].h;
    int prev_focus = focus;
    wins[idx].minimized = 1;
    if (focus == idx) {
        focus = -1;
        int best = -1, bz = -1;
        for (int i = 0; i < MAX_WINS; i++)
            if (wins[i].open && !wins[i].minimized && wins[i].z > bz) {
                bz = wins[i].z;
                best = i;
            }
        focus = best;
    }
    damage_add(ox, oy, ow, oh);
    if (prev_focus >= 0 && prev_focus != idx)
        damage_add_win(prev_focus);
    if (focus >= 0)
        damage_add_win(focus);
    dirty_bits |= DIRTY_MOVE;
}

int desktop_open_app(enum app_kind k) {
    if (k != APP_TERM) {
        int existing = desktop_find_win(k);
        if (existing >= 0) {
            wins[existing].minimized = 0;
            desktop_raise_win(existing);
            dirty_bits |= DIRTY_FULL;
            return existing;
        }
    }
    int slot = -1;
    for (int i = 0; i < MAX_WINS; i++)
        if (!wins[i].open) {
            slot = i;
            break;
        }
    if (slot < 0)
        return -1;
    struct framebuffer *fb = fb_get();
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    memset(&wins[slot], 0, sizeof(wins[slot]));
    wins[slot].kind = k;
    wins[slot].open = 1;
    wins[slot].w = TERM_COLS * cw / 2 + desktop_u(40);
    if (wins[slot].w < desktop_u(420))
        wins[slot].w = desktop_u(420);
    wins[slot].h = desktop_title_h() + TERM_VIEW * ch + desktop_u(40);
    if (k == APP_TERM) {
        desktop_term_reset_slot(slot);
        desktop_term_activate(slot);
        shell_redraw_prompt();
    }
    if (k == APP_SETTINGS) {
        wins[slot].w = desktop_u(480);
        wins[slot].h = desktop_title_h() + desktop_u(380);
        settings_page = 0;
    }
    if (k == APP_AGENT) {
        wins[slot].w = desktop_u(420);
        wins[slot].h = desktop_title_h() + desktop_u(260);
        desktop_app_opened(k);
    }
    if (k == APP_GAME) {
        wins[slot].w = desktop_u(420);
        wins[slot].h = desktop_title_h() + desktop_u(220);
        game_reset();
    }
    if (k == APP_BROWSER) {
        wins[slot].w = desktop_u(520);
        wins[slot].h = desktop_title_h() + desktop_u(320);
        browser_reset();
    }
    if (k == APP_MONITOR) {
        wins[slot].w = desktop_u(640);
        wins[slot].h = desktop_title_h() + desktop_u(460);
        monitor_reset();
    }
    if (k == APP_FILES) {
        wins[slot].w = desktop_u(480);
        wins[slot].h = desktop_title_h() + desktop_u(360);
    }
    if (wins[slot].w > fb->width - desktop_u(40))
        wins[slot].w = (uint32_t)fb->width - desktop_u(40);
    wins[slot].x = desktop_u(40) + (uint32_t)(slot * 24);
    wins[slot].y = desktop_u(40) + (uint32_t)(slot * 24);
    desktop_clamp_win_geom(&wins[slot]);
    desktop_raise_win(slot);
    surface_ensure(&wins[slot].surf, wins[slot].w, wins[slot].h);
    surface_mark_dirty(&wins[slot].surf);
    notify_push(desktop_app_title(k));
    dirty_bits |= DIRTY_FULL;
    return slot;
}

void desktop_close_win(int idx) {
    surface_free(&wins[idx].surf);
    wins[idx].open = 0;
    wins[idx].minimized = 0;
    wins[idx].maximized = 0;
    if (focus == idx) {
        focus = -1;
        int best = -1, bz = -1;
        for (int i = 0; i < MAX_WINS; i++)
            if (wins[i].open && !wins[i].minimized && wins[i].z > bz) {
                bz = wins[i].z;
                best = i;
            }
        focus = best;
    }
    dirty_bits |= DIRTY_FULL;
}

static void draw_win_chrome(struct win *w, int focused) {
    window_draw_frame(w->x, w->y, w->w, w->h, desktop_app_title(w->kind),
                      focused ? desktop_color_bg() : desktop_color_surface());
    uint32_t by = w->y + desktop_u(6);
    uint32_t bs = desktop_u(14);
    uint32_t gap = desktop_u(4);
    uint32_t bx = w->x + w->w - desktop_u(22);
    fb_fill_rect(bx, by, bs, bs, theme_get()->danger);
    bx -= bs + gap;
    fb_fill_rect(bx, by, bs, bs, desktop_color_accent());
    bx -= bs + gap;
    fb_fill_rect(bx, by, bs, bs, desktop_color_dim());
    fb_fill_rect(bx + desktop_u(2), by + bs / 2, bs - desktop_u(4), desktop_u(2), desktop_color_fg());

    if (!w->maximized) {
        uint32_t g = resize_grip();
        uint32_t gx = w->x + w->w - g;
        uint32_t gy = w->y + w->h - g;
        uint32_t accent = focused ? desktop_color_accent() : desktop_color_dim();
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t o = desktop_u(3) + i * desktop_u(3);
            fb_fill_rect(gx + o, gy + g - desktop_u(3), g - o - desktop_u(2), desktop_u(2), accent);
            fb_fill_rect(gx + g - desktop_u(3), gy + o, desktop_u(2), g - o - desktop_u(2), accent);
        }
    }
}

void desktop_draw_win_content(int i) {
    struct win *w = &wins[i];
    draw_win_chrome(w, i == focus);
    if (w->kind == APP_TERM)
        desktop_terminal_draw(w);
    else if (w->kind == APP_FILES)
        desktop_files_draw(w);
    else if (w->kind == APP_SETTINGS)
        desktop_settings_draw(w);
    else if (w->kind == APP_AGENT)
        desktop_agent_draw(w);
    else if (w->kind == APP_GAME) {
        game_draw(w->x + desktop_u(4), w->y + desktop_title_h() + desktop_u(2),
                  w->w - desktop_u(8), w->h - desktop_title_h() - desktop_u(6));
    } else if (w->kind == APP_BROWSER) {
        browser_draw(w->x + desktop_u(4), w->y + desktop_title_h() + desktop_u(2),
                     w->w - desktop_u(8), w->h - desktop_title_h() - desktop_u(6));
    } else if (w->kind == APP_MONITOR) {
        monitor_draw(w->x + desktop_u(4), w->y + desktop_title_h() + desktop_u(2),
                     w->w - desktop_u(8), w->h - desktop_title_h() - desktop_u(6));
    }
}
