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

static int app_kind_ready(enum app_kind k) {
    switch (k) {
    case APP_NOTEPAD:
        return 1;
    case APP_IMAGES:
        return 1;
    case APP_DISKS:
    case APP_NETEXP:
    case APP_NETCTL:
        return 1;
    default:
        return 1;
    }
}

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
int move_win = -1;
uint32_t band_x, band_y, band_w, band_h;
int band_live;
int snap_live;
int snap_hud_mode;

static uint32_t resize_grip(void) {
    uint32_t g = desktop_u(14);
    return g < 12 ? 12 : g;
}

int desktop_point_in(int32_t px, int32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

uint32_t desktop_chrome_btn_strip_w(void) {
    uint32_t bsz = desktop_u(14);
    uint32_t gap = desktop_u(4);
    /* Same formula as desktop_chrome_btns need: close inset + max/min (bs+gap). */
    return desktop_u(22) + 2 * (bsz + gap);
}

void desktop_title_text_geom(uint32_t win_w, uint32_t *pad_x, uint32_t *pad_y,
                             uint32_t *tw) {
    /* Focused inset (desktop_u(3)) so title never runs under chrome buttons. */
    uint32_t s = desktop_u(3);
    if (s < 2)
        s = 2;
    uint32_t px = s + desktop_u(6);
    uint32_t py = s + desktop_u(2);
    uint32_t btn_w = desktop_chrome_btn_strip_w();
    uint32_t text_w = (win_w > px + btn_w + s) ? (win_w - px - btn_w - s) : desktop_u(40);
    if (pad_x)
        *pad_x = px;
    if (pad_y)
        *pad_y = py;
    if (tw)
        *tw = text_w;
}

int desktop_chrome_btns(const struct win *w, uint32_t *close_x, uint32_t *max_x,
                        uint32_t *min_x, uint32_t *by, uint32_t *bs) {
    uint32_t bsz = desktop_u(14);
    uint32_t gap = desktop_u(4);
    /* close at w-22; max/min each subtract (bs+gap) — need full strip or underflow. */
    uint32_t need = desktop_chrome_btn_strip_w();
    if (!w || w->w < need)
        return 0;
    uint32_t cx = w->x + w->w - desktop_u(22);
    uint32_t mx = cx - bsz - gap;
    uint32_t nx = mx - bsz - gap;
    if (close_x) *close_x = cx;
    if (max_x) *max_x = mx;
    if (min_x) *min_x = nx;
    if (by) *by = w->y + desktop_u(6);
    if (bs) *bs = bsz;
    return 1;
}

uint32_t desktop_win_min_w(void) { return desktop_u(180); }
uint32_t desktop_win_min_h(void) { return desktop_title_h() + desktop_u(100); }

void browser_client_rect(const struct win *w, uint32_t *x, uint32_t *y,
                         uint32_t *cw, uint32_t *ch) {
    if (!w)
        return;
    if (x)
        *x = w->x + desktop_u(4);
    if (y)
        *y = w->y + desktop_title_h() + desktop_u(2);
    if (cw)
        *cw = w->w - desktop_u(8);
    if (ch)
        *ch = w->h - desktop_title_h() - desktop_u(6);
}

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

/* Default open size at the current UI scale (shared by open + rescale). */
static void desktop_app_default_size(enum app_kind k, uint32_t *out_w, uint32_t *out_h) {
    struct framebuffer *fb = fb_get();
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    uint32_t w = TERM_COLS * cw + desktop_u(24);
    uint32_t h = desktop_title_h() + TERM_VIEW * ch + desktop_u(40);

    if (w < desktop_u(420))
        w = desktop_u(420);

    switch (k) {
    case APP_SETTINGS:
        w = desktop_u(480);
        h = desktop_title_h() + desktop_u(380);
        break;
    case APP_AGENT:
        w = desktop_u(420);
        h = desktop_title_h() + desktop_u(260);
        break;
    case APP_GAME:
        w = desktop_u(420);
        h = desktop_title_h() + desktop_u(220);
        break;
    case APP_BROWSER:
        w = desktop_u(520);
        h = desktop_title_h() + desktop_u(320);
        break;
    case APP_MONITOR:
        w = desktop_u(640);
        h = desktop_title_h() + desktop_u(460);
        break;
    case APP_FILES:
        w = desktop_u(480);
        h = desktop_title_h() + desktop_u(360);
        break;
    case APP_NOTEPAD:
        w = desktop_u(520);
        h = desktop_title_h() + desktop_u(360);
        break;
    case APP_IMAGES:
        w = desktop_u(560);
        h = desktop_title_h() + desktop_u(420);
        break;
    case APP_DISKS:
        w = desktop_u(480);
        h = desktop_title_h() + desktop_u(340);
        break;
    case APP_NETEXP:
        w = desktop_u(520);
        h = desktop_title_h() + desktop_u(320);
        break;
    case APP_NETCTL:
        w = desktop_u(520);
        h = desktop_title_h() + desktop_u(380);
        break;
    case APP_TERM:
        break;
    }

    if (fb->width > desktop_u(40) && w > (uint32_t)fb->width - desktop_u(40))
        w = (uint32_t)fb->width - desktop_u(40);
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;
}

void desktop_rescale_windows(void) {
    struct framebuffer *fb = fb_get();

    browser_clear_hit_rects();
    /* Settings hits are screen-absolute; drop until next draw rebuilds them. */
    desktop_settings_hits_reset();
    for (int i = 0; i < MAX_WINS; i++) {
        struct win *w = &wins[i];
        uint32_t def_w, def_h;

        if (!w->open)
            continue;

        desktop_app_default_size(w->kind, &def_w, &def_h);

        if (w->maximized) {
            /* Keep maximized fill; refresh restore geom to the new defaults. */
            w->rw = def_w;
            w->rh = def_h;
            w->x = 0;
            w->y = 0;
            w->w = (uint32_t)fb->width;
            w->h = (uint32_t)fb->height > desktop_taskbar_h()
                       ? (uint32_t)fb->height - desktop_taskbar_h()
                       : (uint32_t)fb->height;
        } else {
            /* Terminal: TERM_COLS * cell_w (+ chrome / TERM_VIEW rows).
             * Others: open_app defaults so scale↓ is not huge and scale↑
             * is not min-clamp only. */
            w->w = def_w;
            w->h = def_h;
            desktop_clamp_win_geom(w);
        }

        surface_ensure(&w->surf, w->w, w->h);
        surface_mark_dirty(&w->surf);
    }
    /* Rebuild CSS layouts after geometry + scale are live. */
    browser_on_ui_scale();
}

void desktop_on_ui_scale_changed(void) {
    desktop_menus_close_popups();
    desktop_overlays_close_popups();
    desktop_abort_pointer_gesture();
    desktop_settings_hits_reset();
    desktop_files_clear_crumb_hits();
    browser_clear_hit_rects();
    monitor_clear_hit_rects();
    desktop_rescale_windows();
    cursor_mx = cursor_my = -1;
    dirty_bits |= DIRTY_FULL;
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
    case APP_NOTEPAD: return "Notepad";
    case APP_IMAGES: return "Images";
    case APP_DISKS: return "Disks";
    case APP_NETEXP: return "Net Explorer";
    case APP_NETCTL: return "Net Control";
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
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open)
        return;
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
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open)
        return;
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
    if (w->kind == APP_BROWSER)
        browser_on_ui_scale();
}

void desktop_minimize_win(int idx) {
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open)
        return;
    if (dragging || resizing || move_live || move_win == idx)
        desktop_abort_pointer_gesture();
    if (wins[idx].kind == APP_FILES)
        desktop_files_drag_cancel();
    if (wins[idx].kind == APP_TERM)
        desktop_term_retire(idx);
    uint32_t ox = wins[idx].x, oy = wins[idx].y, ow = wins[idx].w, oh = wins[idx].h;
    int prev_focus = focus;
    wins[idx].minimized = 1;
    /* Pause Browser ticks via desktop loop gate; clear latch so idle can hlt. */
    if (wins[idx].kind == APP_BROWSER)
        browser_clear_wants_redraw();
    /* Soft-budget reclaim may drop pixels; restore path surface_ensure redraws. */
    surface_set_reclaimable(&wins[idx].surf, 1);
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
    if (!app_kind_ready(k)) {
        notify_push("App not ready yet");
        dirty_bits |= DIRTY_TOAST;
        return -1;
    }
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
    memset(&wins[slot], 0, sizeof(wins[slot]));
    wins[slot].kind = k;
    wins[slot].open = 1;
    desktop_app_default_size(k, &wins[slot].w, &wins[slot].h);
    if (k == APP_TERM) {
        desktop_term_reset_slot(slot);
        desktop_term_activate(slot);
        shell_redraw_prompt();
    }
    if (k == APP_SETTINGS) {
        settings_page = 0;
        desktop_settings_init();
    }
    if (k == APP_AGENT)
        desktop_app_opened(k);
    if (k == APP_GAME)
        game_reset();
    if (k == APP_BROWSER)
        browser_reset();
    if (k == APP_MONITOR)
        monitor_reset();
    if (k == APP_NOTEPAD)
        desktop_notepad_init();
    if (k == APP_IMAGES)
        desktop_images_init();
    if (k == APP_DISKS)
        desktop_disks_init();
    if (k == APP_NETEXP)
        desktop_netexp_init();
    if (k == APP_NETCTL)
        desktop_netctl_init();
    wins[slot].x = desktop_u(40) + (uint32_t)(slot * 24);
    wins[slot].y = desktop_u(40) + (uint32_t)(slot * 24);
    desktop_clamp_win_geom(&wins[slot]);
    desktop_raise_win(slot);
    surface_ensure(&wins[slot].surf, wins[slot].w, wins[slot].h);
    surface_mark_dirty(&wins[slot].surf);
    {
        char opened[48];
        snprintf(opened, sizeof(opened), "Opened %s", desktop_app_title(k));
        notify_push(opened);
    }
    dirty_bits |= DIRTY_FULL;
    return slot;
}

void desktop_close_win(int idx) {
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open)
        return;
    if (dragging || resizing || move_live || move_win == idx)
        desktop_abort_pointer_gesture();
    if (wins[idx].kind == APP_FILES)
        desktop_files_drag_cancel();
    if (wins[idx].kind == APP_TERM)
        desktop_term_retire(idx);
    if (ctx_menu && ctx_win == idx)
        desktop_ctx_close();
    if (wins[idx].kind == APP_BROWSER) {
        /* Tear down tabs/JS so timers cannot run after close; reopen resets again. */
        browser_reset();
        browser_clear_wants_redraw();
    }
    surface_free(&wins[idx].surf);
    wins[idx].open = 0;
    wins[idx].minimized = 0;
    wins[idx].maximized = 0;
    wins[idx].kind = APP_TERM; /* slot dead; !open is authoritative */
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

static uint32_t snap_edge(void) {
    uint32_t e = desktop_u(16);
    return e < 8 ? 8 : e;
}

int desktop_snap_hint(int32_t mx, int32_t my) {
    struct framebuffer *fb = fb_get();
    uint32_t edge = snap_edge();
    if (my < (int32_t)edge)
        return 3;
    if (mx < (int32_t)edge)
        return 1;
    if (mx > (int32_t)fb->width - (int32_t)edge)
        return 2;
    return 0;
}

void desktop_snap_zone_rect(int mode, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    struct framebuffer *fb = fb_get();
    uint32_t tb = desktop_taskbar_h();
    uint32_t max_h = (uint32_t)fb->height > tb ? (uint32_t)fb->height - tb : (uint32_t)fb->height;
    switch (mode) {
    case 1: *x = 0; *y = 0; *w = (uint32_t)fb->width / 2; *h = max_h; break;
    case 2: *x = (uint32_t)fb->width / 2; *y = 0; *w = (uint32_t)fb->width - *x; *h = max_h; break;
    case 3: *x = 0; *y = 0; *w = (uint32_t)fb->width; *h = max_h; break;
    default: *x = *y = *w = *h = 0; break;
    }
}

void desktop_snap_apply(int idx, int mode) {
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open || mode <= 0)
        return;
    struct win *w = &wins[idx];
    uint32_t ox = w->x, oy = w->y, ow = w->w, oh = w->h;
    if (mode == 3) {
        desktop_maximize_win(idx);
        return;
    }
    if (!w->maximized) {
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
    }
    w->maximized = 0;
    desktop_snap_zone_rect(mode, &w->x, &w->y, &w->w, &w->h);
    desktop_clamp_win_geom(w);
    damage_add(ox, oy, ow, oh);
    damage_add(w->x, w->y, w->w, w->h);
    surface_ensure(&w->surf, w->w, w->h);
    surface_mark_dirty(&w->surf);
    dirty_bits |= DIRTY_MOVE;
}

void desktop_win_keyboard_nudge(int idx, int dx, int dy) {
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open || wins[idx].maximized)
        return;
    struct win *w = &wins[idx];
    uint32_t step = desktop_u(8);
    if (step < 4)
        step = 4;
    uint32_t ox = w->x, oy = w->y, ow = w->w, oh = w->h;
    int32_t nx = (int32_t)w->x + dx * (int32_t)step;
    int32_t ny = (int32_t)w->y + dy * (int32_t)step;
    if (nx < 0)
        nx = 0;
    if (ny < 0)
        ny = 0;
    w->x = (uint32_t)nx;
    w->y = (uint32_t)ny;
    desktop_clamp_win_geom(w);
    damage_add(ox, oy, ow, oh);
    damage_add(w->x, w->y, w->w, w->h);
    dirty_bits |= DIRTY_MOVE;
}

static void draw_win_chrome(struct win *w, int focused) {
    uint32_t th = desktop_title_h();
    const char *title = desktop_app_title(w->kind);
    window_draw_frame(w->x, w->y, w->w, w->h, title,
                      focused ? desktop_color_bg() : desktop_color_surface());
    if (focused) {
        uint32_t s = desktop_u(3);
        if (s < 2) s = 2;
        uint32_t a = desktop_color_accent();
        /* Outer focus inset — bottom + sides below the title band. */
        fb_fill_rect(w->x + s, w->y + w->h - 2 * s, w->w - 2 * s, s, a);
        fb_fill_rect(w->x + s, w->y + th, s, w->h - th - 2 * s, a);
        fb_fill_rect(w->x + w->w - 2 * s, w->y + th, s, w->h - th - 2 * s, a);
        /* Title underline + short side ticks (not over title glyphs). */
        fb_fill_rect(w->x + s, w->y + th - s, w->w - 2 * s, s, a);
        fb_fill_rect(w->x + s, w->y + s, s, th - 2 * s, a);
        fb_fill_rect(w->x + w->w - 2 * s, w->y + s, s, th - 2 * s, a);
        fb_fill_rect(w->x + 2 * s, w->y + s, w->w - 4 * s, s, a);
    }
    {
        uint32_t close_x, max_x, min_x, by, bs;
        if (desktop_chrome_btns(w, &close_x, &max_x, &min_x, &by, &bs)) {
            fb_fill_rect(close_x, by, bs, bs, theme_get()->danger);
            fb_draw_string(close_x + desktop_u(3), by + desktop_u(1), "x",
                           desktop_color_fg(), theme_get()->danger);
            fb_fill_rect(max_x, by, bs, bs, desktop_color_accent());
            {
                uint32_t m = desktop_u(3);
                uint32_t fg = desktop_color_fg();
                uint32_t bx = max_x;
                if (w->maximized) {
                    fb_fill_rect(bx + m, by + m + desktop_u(2), bs - 2 * m, desktop_u(1), fg);
                    fb_fill_rect(bx + m, by + m + desktop_u(2), desktop_u(1),
                                 bs - 2 * m - desktop_u(2), fg);
                    fb_fill_rect(bx + m + desktop_u(2), by + m,
                                 bs - 2 * m - desktop_u(2), desktop_u(1), fg);
                    fb_fill_rect(bx + bs - m - desktop_u(1), by + m, desktop_u(1),
                                 bs - 2 * m, fg);
                } else {
                    fb_fill_rect(bx + m, by + m, bs - 2 * m, desktop_u(1), fg);
                    fb_fill_rect(bx + m, by + bs - m - desktop_u(1), bs - 2 * m, desktop_u(1), fg);
                    fb_fill_rect(bx + m, by + m, desktop_u(1), bs - 2 * m, fg);
                    fb_fill_rect(bx + bs - m - desktop_u(1), by + m, desktop_u(1), bs - 2 * m, fg);
                }
            }
            fb_fill_rect(min_x, by, bs, bs, desktop_color_dim());
            fb_draw_string(min_x + desktop_u(3), by + desktop_u(1), "_",
                           desktop_color_fg(), desktop_color_dim());
        }
    }

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
    else if (w->kind == APP_NOTEPAD)
        desktop_notepad_draw(w);
    else if (w->kind == APP_IMAGES)
        desktop_images_draw(w);
    else if (w->kind == APP_DISKS)
        desktop_disks_draw(w);
    else if (w->kind == APP_NETEXP)
        desktop_netexp_draw(w);
    else if (w->kind == APP_NETCTL)
        desktop_netctl_draw(w);
    else if (w->kind == APP_SETTINGS)
        desktop_settings_draw(w);
    else if (w->kind == APP_AGENT)
        desktop_agent_draw(w);
    else if (w->kind == APP_GAME) {
        game_draw(w->x + desktop_u(4), w->y + desktop_title_h() + desktop_u(2),
                  w->w - desktop_u(8), w->h - desktop_title_h() - desktop_u(6));
    } else if (w->kind == APP_BROWSER) {
        uint32_t bx, by, bw, bh;
        browser_client_rect(w, &bx, &by, &bw, &bh);
        browser_draw(bx, by, bw, bh);
    } else if (w->kind == APP_MONITOR) {
        monitor_draw(w->x + desktop_u(4), w->y + desktop_title_h() + desktop_u(2),
                     w->w - desktop_u(8), w->h - desktop_title_h() - desktop_u(6));
    } else {
        uint32_t ty = w->y + desktop_title_h() + desktop_u(24);
        fb_draw_string(w->x + desktop_u(16), ty, "Coming soon",
                       desktop_color_dim(), desktop_color_bg());
    }
}
