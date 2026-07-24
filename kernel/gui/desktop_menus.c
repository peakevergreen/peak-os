#include "desktop_internal.h"
#include "fb.h"
#include "theme.h"
#include "timer.h"
#include "rtc.h"
#include "settings.h"
#include "wallpaper.h"
#include "net.h"
#include "notify.h"
#include "peakdisk.h"
#include "util.h"

int menu_open;
int ctx_menu;
int32_t ctx_x, ctx_y;

void desktop_draw_desktop_bg(void) {
    struct framebuffer *fb = fb_get();
    uint32_t h = (uint32_t)fb->height;
    uint32_t w = (uint32_t)fb->width;
    uint32_t tb = desktop_taskbar_h();
    uint32_t desk_h = h - tb;
    if (wallpaper_enabled())
        wallpaper_draw(0, 0, w, desk_h);
    else
        fb_fill_rect(0, 0, w, desk_h, desktop_color_bg());
    if (settings_show_brand()) {
        uint32_t lx = desktop_u(24), ly = desktop_u(24);
        uint32_t ch = fb_cell_h();
        uint32_t scrim = wallpaper_enabled() ? desktop_color_surface() : desktop_color_bg();
        fb_fill_rect(lx - desktop_u(8), ly - desktop_u(6), desktop_u(120), ch + desktop_u(12), scrim);
        fb_draw_string(lx, ly, "PeakOS", desktop_color_fg(), scrim);
    }
}

static void format_clock(char *tbuf, size_t tlen) {
    rtc_format_clock(tbuf, tlen);
    if (!tbuf[0]) {
        uint64_t secs = timer_uptime_secs();
        snprintf(tbuf, tlen, "%lum", (unsigned long)(secs / 60));
    }
}

void desktop_clock_rect(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    *x = (uint32_t)fb->width - desktop_u(110);
    *y = (uint32_t)fb->height - th;
    *w = desktop_u(110);
    *h = th;
}

void desktop_draw_clock_area(void) {
    if (!settings_show_clock())
        return;
    uint32_t cx, cy, cw, ch;
    desktop_clock_rect(&cx, &cy, &cw, &ch);
    fb_fill_rect(cx, cy, cw, ch, desktop_color_surface());
    fb_fill_rect(cx, cy, cw, desktop_u(2), desktop_color_accent());
    char tbuf[16];
    format_clock(tbuf, sizeof(tbuf));
    last_clock_secs = timer_uptime_secs();
    fb_draw_string((uint32_t)fb_get()->width - desktop_u(100),
                   cy + (ch - fb_cell_h()) / 2, tbuf, desktop_color_fg(), desktop_color_surface());
}

uint32_t desktop_taskbar_btn_w(void) { return desktop_u(88); }

void desktop_draw_taskbar(void) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t y = (uint32_t)fb->height - th;
    fb_fill_rect(0, y, (uint32_t)fb->width, th, desktop_color_surface());
    fb_fill_rect(0, y, (uint32_t)fb->width, desktop_u(2), desktop_color_accent());
    fb_draw_string(desktop_u(12), y + (th - fb_cell_h()) / 2, "Peak", desktop_color_fg(), desktop_color_surface());

    uint32_t bx = desktop_u(70);
    uint32_t bw = desktop_taskbar_btn_w();
    uint32_t by = y + desktop_u(4);
    uint32_t bh = th > desktop_u(8) ? th - desktop_u(8) : th;
    for (int i = 0; i < MAX_WINS; i++) {
        if (!wins[i].open)
            continue;
        uint32_t bg = (i == focus && !wins[i].minimized) ? desktop_color_accent() : desktop_color_bg();
        uint32_t fg = (i == focus && !wins[i].minimized) ? desktop_color_bg() : desktop_color_fg();
        if (wins[i].minimized)
            bg = desktop_color_dim();
        fb_fill_rect(bx, by, bw - desktop_u(4), bh, bg);
        fb_draw_string_fit(bx + desktop_u(4), by + (bh - fb_cell_h()) / 2, bw - desktop_u(12),
                           desktop_app_title(wins[i].kind), fg, bg);
        bx += bw;
        if (bx + bw > (uint32_t)fb->width - desktop_u(120))
            break;
    }

    struct net_info ni;
    net_get_info(&ni);
    fb_draw_string_fit((uint32_t)fb->width - desktop_u(160), y + (th - fb_cell_h()) / 2,
                       desktop_u(50), ni.up ? "net" : "off", ni.up ? desktop_color_accent() : desktop_color_dim(),
                       desktop_color_surface());
    desktop_draw_clock_area();
}

void desktop_draw_start_menu(void) {
    if (!menu_open)
        return;
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t mw = desktop_u(180);
    uint32_t mh = desktop_u(320);
    uint32_t mx = desktop_u(8);
    uint32_t my = (uint32_t)fb->height - th - mh - desktop_u(4);
    fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
    fb_fill_rect(mx, my, mw, desktop_u(2), desktop_color_accent());
    const char *items[] = {
        "Terminal", "Files", "Settings", "Agent", "Peak Runner",
        "Browser", "Monitor", "Theme", "Help", "Save disk",
        "Lock", "Exit desktop", "Reboot", "Power off"
    };
    for (int i = 0; i < 14; i++) {
        fb_draw_string(mx + desktop_u(12), my + desktop_u(12) + (uint32_t)i * (fb_cell_h() + desktop_u(4)),
                       items[i], desktop_color_fg(), desktop_color_surface());
    }
}

void desktop_draw_ctx_menu(void) {
    if (!ctx_menu)
        return;
    uint32_t mw = desktop_u(140);
    uint32_t mh = desktop_u(90);
    fb_fill_rect((uint32_t)ctx_x, (uint32_t)ctx_y, mw, mh, desktop_color_surface());
    fb_fill_rect((uint32_t)ctx_x, (uint32_t)ctx_y, mw, desktop_u(2), desktop_color_accent());
    fb_draw_string((uint32_t)ctx_x + desktop_u(8), (uint32_t)ctx_y + desktop_u(10), "Terminal", desktop_color_fg(), desktop_color_surface());
    fb_draw_string((uint32_t)ctx_x + desktop_u(8), (uint32_t)ctx_y + desktop_u(10) + fb_cell_h() + desktop_u(4),
                   "Files", desktop_color_fg(), desktop_color_surface());
    fb_draw_string((uint32_t)ctx_x + desktop_u(8), (uint32_t)ctx_y + desktop_u(10) + 2 * (fb_cell_h() + desktop_u(4)),
                   "Settings", desktop_color_fg(), desktop_color_surface());
}

/* Damage start-menu / Peak button region so open/close avoid DIRTY_FULL. */
static void menus_damage_start(void) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t mw = desktop_u(180);
    uint32_t mh = desktop_u(320);
    uint32_t mx = desktop_u(8);
    uint32_t my = (uint32_t)fb->height - th - mh - desktop_u(4);
    damage_add(mx, my, mw, mh);
    damage_add(desktop_u(8), (uint32_t)fb->height - th, desktop_u(60), th);
}

static void menus_damage_ctx(void) {
    damage_add((uint32_t)ctx_x, (uint32_t)ctx_y, desktop_u(140), desktop_u(90));
}

void desktop_menu_click(int32_t mx, int32_t my) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t mw = desktop_u(180);
    uint32_t mh = desktop_u(320);
    uint32_t menux = desktop_u(8);
    uint32_t menuy = (uint32_t)fb->height - th - mh - desktop_u(4);
    if (!desktop_point_in(mx, my, menux, menuy, mw, mh)) {
        menu_open = 0;
        menus_damage_start();
        return;
    }
    int row = (int)((my - (int32_t)menuy - (int32_t)desktop_u(12)) / (int32_t)(fb_cell_h() + desktop_u(4)));
    menu_open = 0;
    menus_damage_start();
    if (row == 0)
        desktop_open_app(APP_TERM);
    else if (row == 1)
        desktop_open_app(APP_FILES);
    else if (row == 2)
        desktop_open_app(APP_SETTINGS);
    else if (row == 3)
        desktop_open_app(APP_AGENT);
    else if (row == 4)
        desktop_open_app(APP_GAME);
    else if (row == 5)
        desktop_open_app(APP_BROWSER);
    else if (row == 6)
        desktop_open_app(APP_MONITOR);
    else if (row == 7) {
        theme_next();
        theme_persist();
        dirty_bits |= DIRTY_FULL;
    } else if (row == 8) {
        help_open = 1;
        dirty_bits |= DIRTY_FULL;
    } else if (row == 9) {
        if (peakdisk_save_async() == 0)
            notify_push("Saving to disk…");
        else
            notify_push("Save failed");
        dirty_bits |= DIRTY_TOAST;
    } else if (row == 10) {
        session_lock = 1;
        dirty_bits |= DIRTY_FULL;
    } else if (row == 11) {
        desktop_should_exit = 1;
    } else if (row == 12) {
        power_confirm = 2;
        dirty_bits |= DIRTY_FULL;
    } else if (row == 13) {
        power_confirm = 1;
        dirty_bits |= DIRTY_FULL;
    }
}

/* Returns 1 if the click was consumed by the context menu. */
int desktop_ctx_menu_click(int32_t mx, int32_t my) {
    if (!ctx_menu)
        return 0;
    uint32_t mw = desktop_u(140);
    uint32_t mh = desktop_u(90);
    if (desktop_point_in(mx, my, (uint32_t)ctx_x, (uint32_t)ctx_y, mw, mh)) {
        int row = (int)((my - ctx_y - (int32_t)desktop_u(10)) /
                        (int32_t)(fb_cell_h() + desktop_u(4)));
        if (row == 0)
            desktop_open_app(APP_TERM);
        else if (row == 1)
            desktop_open_app(APP_FILES);
        else if (row == 2)
            desktop_open_app(APP_SETTINGS);
    }
    menus_damage_ctx();
    ctx_menu = 0;
    return 1;
}

void desktop_menus_open_ctx(int32_t mx, int32_t my) {
    if (ctx_menu)
        menus_damage_ctx();
    ctx_menu = 1;
    ctx_x = mx;
    ctx_y = my;
    if (menu_open) {
        menu_open = 0;
        menus_damage_start();
    }
    menus_damage_ctx();
}

int desktop_menus_toggle_start(int32_t mx, int32_t my, uint32_t taskbar_y, uint32_t taskbar_h) {
    if (!desktop_point_in(mx, my, desktop_u(8), taskbar_y, desktop_u(60), taskbar_h))
        return 0;
    menus_damage_start();
    menu_open = !menu_open;
    return 1;
}

int desktop_menus_close_popups(void) {
    if (!(menu_open || ctx_menu))
        return 0;
    if (menu_open)
        menus_damage_start();
    if (ctx_menu)
        menus_damage_ctx();
    menu_open = ctx_menu = 0;
    return 1;
}
