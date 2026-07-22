#include "gui.h"
#include "console.h"
#include "fb.h"
#include "display.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"
#include "timer.h"
#include "util.h"
#include "theme.h"
#include "vfs.h"
#include "game.h"
#include "browser.h"
#include "monitor.h"
#include "sysmon.h"
#include "wallpaper.h"
#include "settings.h"
#include "sched.h"
#include "clipboard.h"
#include "notify.h"
#include "rtc.h"
#include "sound.h"
#include "power.h"
#include "peakdisk.h"
#include "net.h"
#include "heap.h"
#include "surface.h"
#include "platform.h"
#include "guiproto.h"

#include "desktop_internal.h"

int menu_open;
int ctx_menu;
int32_t ctx_x, ctx_y;
int settings_page;
int alttab_open;
int alttab_sel;
int help_open;
static int login_done;
int session_lock;
int power_confirm;

static int desktop_should_exit;

uint32_t desktop_u(uint32_t v) { return v * fb_ui_scale(); }

uint32_t desktop_taskbar_h(void) { return fb_cell_h() + desktop_u(12); }

uint32_t desktop_title_h(void) {
    uint32_t h = fb_cell_h() + desktop_u(8);
    return h < 22 ? 22 : h;
}

uint32_t desktop_color_bg(void) { return theme_get()->bg; }
uint32_t desktop_color_fg(void) { return theme_get()->fg; }
uint32_t desktop_color_dim(void) { return theme_get()->dim; }
uint32_t desktop_color_accent(void) { return theme_get()->accent; }
uint32_t desktop_color_surface(void) { return theme_get()->surface; }
uint32_t desktop_color_title(void) { return theme_get()->title; }
uint32_t desktop_color_border(void) { return theme_get()->border; }

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

static uint32_t taskbar_btn_w(void) { return desktop_u(88); }

void desktop_draw_taskbar(void) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t y = (uint32_t)fb->height - th;
    fb_fill_rect(0, y, (uint32_t)fb->width, th, desktop_color_surface());
    fb_fill_rect(0, y, (uint32_t)fb->width, desktop_u(2), desktop_color_accent());
    fb_draw_string(desktop_u(12), y + (th - fb_cell_h()) / 2, "Peak", desktop_color_fg(), desktop_color_surface());

    uint32_t bx = desktop_u(70);
    uint32_t bw = taskbar_btn_w();
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

void desktop_draw_session_overlays(void) {
    struct framebuffer *fb = fb_get();
    if (session_lock) {
        fb_fill_rect(0, 0, (uint32_t)fb->width, (uint32_t)fb->height, desktop_color_bg());
        uint32_t mw = desktop_u(340);
        uint32_t mh = desktop_u(120);
        uint32_t mx = ((uint32_t)fb->width - mw) / 2;
        uint32_t my = ((uint32_t)fb->height - mh) / 3;
        fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
        fb_fill_rect(mx, my, mw, desktop_u(3), desktop_color_accent());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(28), "Session locked", desktop_color_fg(), desktop_color_surface());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(28) + fb_cell_h() + desktop_u(8),
                       "Press Enter to unlock (single-user)", desktop_color_dim(), desktop_color_surface());
        return;
    }
    if (power_confirm) {
        uint32_t mw = desktop_u(360);
        uint32_t mh = desktop_u(130);
        uint32_t mx = ((uint32_t)fb->width - mw) / 2;
        uint32_t my = ((uint32_t)fb->height - mh) / 3;
        fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
        fb_fill_rect(mx, my, mw, desktop_u(3), desktop_color_accent());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(24),
                       power_confirm == 1 ? "Power off?" : "Reboot?",
                       desktop_color_fg(), desktop_color_surface());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(24) + fb_cell_h() + desktop_u(10),
                       "Y confirm · N / Esc cancel", desktop_color_dim(), desktop_color_surface());
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

void desktop_draw_alttab(void) {
    if (!alttab_open)
        return;
    struct framebuffer *fb = fb_get();
    int order[MAX_WINS], n = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open)
            order[n++] = i;
    if (n == 0)
        return;
    if (alttab_sel < 0 || alttab_sel >= n)
        alttab_sel = 0;
    uint32_t mw = desktop_u(280);
    uint32_t mh = desktop_u(40) + (uint32_t)n * (fb_cell_h() + desktop_u(6));
    uint32_t mx = ((uint32_t)fb->width - mw) / 2;
    uint32_t my = ((uint32_t)fb->height - mh) / 3;
    fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
    fb_fill_rect(mx, my, mw, desktop_u(2), desktop_color_accent());
    fb_draw_string(mx + desktop_u(12), my + desktop_u(8), "Switch window", desktop_color_dim(), desktop_color_surface());
    for (int i = 0; i < n; i++) {
        uint32_t bg = (i == alttab_sel) ? desktop_color_accent() : desktop_color_surface();
        uint32_t fg = (i == alttab_sel) ? desktop_color_bg() : desktop_color_fg();
        uint32_t ry = my + desktop_u(28) + (uint32_t)i * (fb_cell_h() + desktop_u(6));
        fb_fill_rect(mx + desktop_u(8), ry, mw - desktop_u(16), fb_cell_h() + desktop_u(2), bg);
        fb_draw_string(mx + desktop_u(16), ry, desktop_app_title(wins[order[i]].kind), fg, bg);
    }
}

void desktop_draw_help(void) {
    if (!help_open)
        return;
    struct framebuffer *fb = fb_get();
    uint32_t mw = desktop_u(420);
    uint32_t mh = desktop_u(260);
    uint32_t mx = ((uint32_t)fb->width - mw) / 2;
    uint32_t my = desktop_u(80);
    fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
    fb_fill_rect(mx, my, mw, desktop_u(2), desktop_color_accent());
    uint32_t cy = my + desktop_u(12);
    uint32_t ch = fb_cell_h() + desktop_u(2);
    const char *lines[] = {
        "Peak desktop shortcuts",
        "1-7  open apps",
        "Alt+Tab  switch windows",
        "Ctrl+Alt+Esc  leave desktop",
        "S scale  T theme",
        "Files: n new  d delete  r rename  u up",
        "Wheel scrolls Files/Term/Browser",
        "Peak menu: Save disk / Power off",
        "Esc closes menus (not desktop)",
        "Click title buttons: _ [] x",
    };
    for (int i = 0; i < 10; i++) {
        fb_draw_string(mx + desktop_u(12), cy, lines[i], i == 0 ? desktop_color_accent() : desktop_color_fg(), desktop_color_surface());
        cy += ch;
    }
}

static void handle_menu_click(int32_t mx, int32_t my) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t mw = desktop_u(180);
    uint32_t mh = desktop_u(320);
    uint32_t menux = desktop_u(8);
    uint32_t menuy = (uint32_t)fb->height - th - mh - desktop_u(4);
    if (!desktop_point_in(mx, my, menux, menuy, mw, mh)) {
        menu_open = 0;
        return;
    }
    int row = (int)((my - (int32_t)menuy - (int32_t)desktop_u(12)) / (int32_t)(fb_cell_h() + desktop_u(4)));
    menu_open = 0;
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
        dirty_bits |= DIRTY_FULL;
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

void desktop_init(void) {
    desktop_opaque_move_free();
    surface_init();
    for (int i = 0; i < MAX_WINS; i++)
        surface_free(&wins[i].surf);
    memset(wins, 0, sizeof(wins));
    focus = -1;
    menu_open = 0;
    ctx_menu = 0;
    dragging = 0;
    resizing = 0;
    resize_edge = 0;
    band_live = 0;
    settings_page = 0;
    desktop_compose_reset_cursor_cache();
    desktop_files_init();
    alttab_open = 0;
    help_open = 0;
    desktop_should_exit = 0;
    session_lock = 0;
    power_confirm = 0;
    desktop_terminal_init();
    desktop_agent_init();
    scene_ready = 0;
    clipboard_init();
    notify_init();
    damage_clear();
    dirty_bits = DIRTY_FULL;
}

static void desktop_login(void) {
    if (login_done)
        return;
    struct framebuffer *fb = fb_get();
    for (;;) {
        fb_begin_frame();
        fb_fill_rect(0, 0, (uint32_t)fb->width, (uint32_t)fb->height, desktop_color_bg());
        if (wallpaper_enabled())
            wallpaper_draw(0, 0, (uint32_t)fb->width, (uint32_t)fb->height);
        uint32_t mw = desktop_u(320);
        uint32_t mh = desktop_u(140);
        uint32_t mx = ((uint32_t)fb->width - mw) / 2;
        uint32_t my = ((uint32_t)fb->height - mh) / 3;
        fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
        fb_fill_rect(mx, my, mw, desktop_u(3), desktop_color_accent());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(24), "PeakOS", desktop_color_fg(), desktop_color_surface());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(24) + fb_cell_h() + desktop_u(8),
                       "Press Enter to sign in", desktop_color_dim(), desktop_color_surface());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(24) + 2 * (fb_cell_h() + desktop_u(8)),
                       "(single-user session)", desktop_color_dim(), desktop_color_surface());
        fb_end_frame();
        int key = keyboard_try_getkey();
        if (key == '\n' || key == ' ' || key == 27) {
            login_done = 1;
            sound_ui_notify();
            notify_push("Welcome to PeakOS");
            break;
        }
        hlt();
    }
}

void desktop_run(void) {
    desktop_init();
    desktop_login();
    while (keyboard_try_getkey())
        ;
    net_attempt_stats_reset();
    desktop_draw();

    uint64_t last_drag_tick = 0;
    uint64_t last_game_tick = timer_ticks();
    uint64_t last_mon_tick = timer_ticks();
    uint64_t last_present_tick = 0;
    uint64_t last_click_tick = 0;
    uint64_t last_input_tick = timer_ticks();
    int32_t last_click_x = -1, last_click_y = -1;
    struct framebuffer *fb = fb_get();

    for (;;) {
        if (desktop_should_exit)
            break;

        sound_poll();
        platform_poll();
        browser_tick();
        if (browser_wants_redraw()) {
            dirty_bits |= DIRTY_BROWSER;
            desktop_mark_win_surf_dirty(desktop_find_win(APP_BROWSER));
        }
        for (uint32_t pid = 0; pid < 16; pid++) {
            uint32_t dx, dy, dw, dh;
            if (guiproto_take_damage(pid, &dx, &dy, &dw, &dh)) {
                damage_add(dx, dy, dw, dh);
                dirty_bits |= DIRTY_MOVE;
            }
        }
        int key = keyboard_try_getkey();
        if (key || mouse_buttons_any())
            last_input_tick = timer_ticks();
        if (!session_lock && !power_confirm &&
            timer_ticks() - last_input_tick > 30000) {
            session_lock = 1;
            dirty_bits |= DIRTY_FULL;
        }

        if (session_lock) {
            if (key == '\n' || key == ' ') {
                session_lock = 0;
                dirty_bits |= DIRTY_FULL;
            }
            if (dirty_bits)
                desktop_draw();
            mouse_clear_clicks();
            hlt_if_enabled();
            continue;
        }
        if (power_confirm) {
            if (key == 'y' || key == 'Y') {
                int mode = power_confirm;
                power_confirm = 0;
                notify_push(mode == 1 ? "Shutting down..." : "Rebooting...");
                dirty_bits |= DIRTY_FULL;
                desktop_draw();
                if (mode == 1)
                    power_shutdown();
                else
                    power_reboot();
            } else if (key == 'n' || key == 'N' || key == 27) {
                power_confirm = 0;
                dirty_bits |= DIRTY_FULL;
            }
            if (dirty_bits)
                desktop_draw();
            mouse_clear_clicks();
            hlt_if_enabled();
            continue;
        }

        if (key == 27) {
            if (keyboard_ctrl_down() && keyboard_alt_down())
                break;
            if (menu_open || ctx_menu || alttab_open || help_open) {
                menu_open = ctx_menu = alttab_open = help_open = 0;
                dirty_bits |= DIRTY_FULL;
            }
            key = 0;
        }

        if (key == KEY_TAB && keyboard_alt_down()) {
            int order[MAX_WINS], n = 0;
            for (int i = 0; i < MAX_WINS; i++)
                if (wins[i].open)
                    order[n++] = i;
            if (n > 0) {
                if (!alttab_open) {
                    alttab_open = 1;
                    alttab_sel = 0;
                } else {
                    alttab_sel = (alttab_sel + 1) % n;
                }
                dirty_bits |= DIRTY_FULL;
            }
            key = 0;
        } else if (alttab_open && !keyboard_alt_down()) {
            int order[MAX_WINS], n = 0;
            for (int i = 0; i < MAX_WINS; i++)
                if (wins[i].open)
                    order[n++] = i;
            if (n > 0 && alttab_sel >= 0 && alttab_sel < n) {
                wins[order[alttab_sel]].minimized = 0;
                desktop_raise_win(order[alttab_sel]);
            }
            alttab_open = 0;
            dirty_bits |= DIRTY_FULL;
        }

        if (key == 20) {
            theme_next();
            theme_persist();
            dirty_bits |= DIRTY_FULL;
            key = 0;
        }

        int term_focus = focus >= 0 && wins[focus].open && !wins[focus].minimized &&
                        wins[focus].kind == APP_TERM;
        if (term_focus)
            desktop_term_activate(focus);
        int game_focus = focus >= 0 && wins[focus].open && !wins[focus].minimized &&
                         wins[focus].kind == APP_GAME;
        int br_focus = focus >= 0 && wins[focus].open && !wins[focus].minimized &&
                       wins[focus].kind == APP_BROWSER;
        int mon_focus = focus >= 0 && wins[focus].open && !wins[focus].minimized &&
                        wins[focus].kind == APP_MONITOR;
        int files_focus = focus >= 0 && wins[focus].open && !wins[focus].minimized &&
                          wins[focus].kind == APP_FILES;
        int agent_focus = focus >= 0 && wins[focus].open && !wins[focus].minimized &&
                          wins[focus].kind == APP_AGENT;

        if (key && agent_focus) {
            desktop_agent_key(key);
            key = 0;
        }

        if (key && files_focus) {
            desktop_files_key(key);
            key = 0;
        }

        if (key && term_focus) {
            desktop_terminal_key(key);
        } else if (key && game_focus && key < 128) {
            game_input((char)key);
            dirty_bits |= DIRTY_GAME;
            desktop_mark_focus_surf_dirty();
        } else if (key && br_focus && key < 128) {
            browser_input((char)key);
            dirty_bits |= DIRTY_BROWSER;
            desktop_mark_focus_surf_dirty();
        } else if (key && mon_focus && key < 128) {
            monitor_input((char)key);
            dirty_bits |= DIRTY_MONITOR;
            desktop_mark_focus_surf_dirty();
        } else if (key == '1')
            desktop_open_app(APP_TERM);
        else if (key == '2')
            desktop_open_app(APP_FILES);
        else if (key == '3')
            desktop_open_app(APP_SETTINGS);
        else if (key == '4')
            desktop_open_app(APP_AGENT);
        else if (key == '5')
            desktop_open_app(APP_GAME);
        else if (key == '6')
            desktop_open_app(APP_BROWSER);
        else if (key == '7')
            desktop_open_app(APP_MONITOR);
        else if (key == 't' || key == 'T') {
            theme_next();
            theme_persist();
            dirty_bits |= DIRTY_FULL;
        } else if (key == 's' || key == 'S') {
            settings_cycle_gui_scale();
            settings_persist();
            desktop_rescale_windows();
            dirty_bits |= DIRTY_FULL;
        }

        struct mouse_state m;
        mouse_poll(&m);

        notify_tick();
        if (notify_consume_dirty())
            dirty_bits |= DIRTY_TOAST;

        if (m.wheel) {
            if (term_focus)
                desktop_terminal_wheel(m.wheel);
            else if (files_focus)
                desktop_files_wheel(m.wheel);
            else if (br_focus) {
                browser_input(m.wheel > 0 ? 'k' : 'j');
                dirty_bits |= DIRTY_BROWSER;
                desktop_mark_focus_surf_dirty();
            } else if (mon_focus) {
                monitor_input(m.wheel > 0 ? '[' : ']');
                dirty_bits |= DIRTY_MONITOR;
                desktop_mark_focus_surf_dirty();
            }
        }

        if (m.right_pressed) {
            ctx_menu = 1;
            ctx_x = m.x;
            ctx_y = m.y;
            menu_open = 0;
            dirty_bits |= DIRTY_FULL;
            mouse_clear_clicks();
        }

        if (m.left_pressed) {
            uint64_t now = timer_ticks();
            int dbl = (now - last_click_tick < 30) &&
                      (m.x - last_click_x < 8) && (m.x - last_click_x > -8) &&
                      (m.y - last_click_y < 8) && (m.y - last_click_y > -8);
            last_click_tick = now;
            last_click_x = m.x;
            last_click_y = m.y;

            uint32_t th = desktop_taskbar_h();
            uint32_t ty = (uint32_t)fb->height - th;

            if (ctx_menu) {
                uint32_t mw = desktop_u(140);
                uint32_t mh = desktop_u(90);
                if (desktop_point_in(m.x, m.y, (uint32_t)ctx_x, (uint32_t)ctx_y, mw, mh)) {
                    int row = (int)((m.y - ctx_y - (int32_t)desktop_u(10)) /
                                    (int32_t)(fb_cell_h() + desktop_u(4)));
                    if (row == 0)
                        desktop_open_app(APP_TERM);
                    else if (row == 1)
                        desktop_open_app(APP_FILES);
                    else if (row == 2)
                        desktop_open_app(APP_SETTINGS);
                }
                ctx_menu = 0;
                dirty_bits |= DIRTY_FULL;
                mouse_clear_clicks();
                continue;
            }

            if (help_open) {
                help_open = 0;
                dirty_bits |= DIRTY_FULL;
                mouse_clear_clicks();
                continue;
            }

            if (desktop_point_in(m.x, m.y, desktop_u(8), ty, desktop_u(60), th)) {
                menu_open = !menu_open;
                dirty_bits |= DIRTY_FULL;
            } else if (menu_open) {
                handle_menu_click(m.x, m.y);
                dirty_bits |= DIRTY_FULL;
            } else if (desktop_point_in(m.x, m.y, desktop_u(70), ty, (uint32_t)fb->width - desktop_u(180), th)) {
                uint32_t bx = desktop_u(70);
                uint32_t bw = taskbar_btn_w();
                for (int i = 0; i < MAX_WINS; i++) {
                    if (!wins[i].open)
                        continue;
                    if (desktop_point_in(m.x, m.y, bx, ty, bw - desktop_u(4), th)) {
                        if (wins[i].minimized || focus != i) {
                            wins[i].minimized = 0;
                            desktop_raise_win(i);
                        } else {
                            desktop_minimize_win(i);
                        }
                        sound_ui_click();
                        break;
                    }
                    bx += bw;
                }
                dirty_bits |= DIRTY_FULL;
            } else {
                int order[MAX_WINS], n = 0;
                for (int i = 0; i < MAX_WINS; i++)
                    if (wins[i].open && !wins[i].minimized)
                        order[n++] = i;
                for (int i = 0; i < n; i++)
                    for (int j = i + 1; j < n; j++)
                        if (wins[order[j]].z > wins[order[i]].z) {
                            int t = order[i];
                            order[i] = order[j];
                            order[j] = t;
                        }
                for (int k = 0; k < n; k++) {
                    int i = order[k];
                    struct win *w = &wins[i];
                    uint32_t by = w->y + desktop_u(6);
                    uint32_t bs = desktop_u(14);
                    uint32_t gap = desktop_u(4);
                    uint32_t close_x = w->x + w->w - desktop_u(22);
                    uint32_t max_x = close_x - bs - gap;
                    uint32_t min_x = max_x - bs - gap;
                    if (desktop_point_in(m.x, m.y, close_x, by, bs, bs)) {
                        desktop_close_win(i);
                        break;
                    }
                    if (desktop_point_in(m.x, m.y, max_x, by, bs, bs)) {
                        desktop_raise_win(i);
                        desktop_maximize_win(i);
                        break;
                    }
                    if (desktop_point_in(m.x, m.y, min_x, by, bs, bs)) {
                        desktop_minimize_win(i);
                        break;
                    }
                    if (desktop_point_in(m.x, m.y, w->x, w->y, w->w, w->h)) {
                        desktop_raise_win(i);
                        int edge = w->maximized ? 0 : desktop_hit_resize_edge(w, m.x, m.y);
                        if (edge) {
                            resizing = 1;
                            dragging = 0;
                            band_live = 1;
                            resize_edge = edge;
                            resize_origin_x = m.x;
                            resize_origin_y = m.y;
                            resize_orig_w = w->w;
                            resize_orig_h = w->h;
                            resize_orig_x = w->x;
                            resize_orig_y = w->y;
                            band_x = w->x;
                            band_y = w->y;
                            band_w = w->w;
                            band_h = w->h;
                            move_prev_x = w->x;
                            move_prev_y = w->y;
                            move_prev_w = w->w;
                            move_prev_h = w->h;
                            move_prev_valid = 1;
                        } else if (desktop_point_in(m.x, m.y, w->x, w->y, w->w, desktop_title_h())) {
                            if (dbl) {
                                desktop_maximize_win(i);
                            } else {
                                dragging = 1;
                                resizing = 0;
                                band_live = 0;
                                drag_off_x = m.x - (int32_t)w->x;
                                drag_off_y = m.y - (int32_t)w->y;
                                move_prev_x = w->x;
                                move_prev_y = w->y;
                                move_prev_w = w->w;
                                move_prev_h = w->h;
                                move_prev_valid = 1;
                                desktop_opaque_move_begin(i);
                            }
                        } else if (w->kind == APP_SETTINGS) {
                            desktop_settings_click(w, m.x, m.y);
                        } else if (w->kind == APP_FILES) {
                            desktop_files_click(w, m.x, m.y, dbl);
                        } else if (w->kind == APP_BROWSER) {
                            browser_click(m.x - (int32_t)(w->x + desktop_u(4)),
                                          m.y - (int32_t)(w->y + desktop_title_h() + desktop_u(2)),
                                          w->w - desktop_u(8), w->h - desktop_title_h() - desktop_u(8));
                            dirty_bits |= DIRTY_BROWSER;
                        } else if (w->kind == APP_AGENT) {
                            desktop_agent_click();
                        } else {
                            dirty_bits |= DIRTY_FULL;
                        }
                        if (!(dirty_bits & (DIRTY_WIN | DIRTY_BROWSER)))
                            dirty_bits |= DIRTY_FULL;
                        break;
                    }
                }
            }
            mouse_clear_clicks();
        }
        if (m.left_released) {
            if (dragging) {
                if (focus >= 0 && m.x < (int32_t)desktop_u(8)) {
                    wins[focus].x = 0;
                    wins[focus].y = 0;
                    wins[focus].w = (uint32_t)fb->width / 2;
                    wins[focus].h = (uint32_t)fb->height - desktop_taskbar_h();
                    wins[focus].maximized = 0;
                } else if (focus >= 0 && m.x > (int32_t)fb->width - (int32_t)desktop_u(8)) {
                    wins[focus].x = (uint32_t)fb->width / 2;
                    wins[focus].y = 0;
                    wins[focus].w = (uint32_t)fb->width / 2;
                    wins[focus].h = (uint32_t)fb->height - desktop_taskbar_h();
                    wins[focus].maximized = 0;
                }
                if (move_live)
                    desktop_opaque_move_end();
                else
                    dirty_bits |= DIRTY_FULL;
                if (focus >= 0) {
                    surface_ensure(&wins[focus].surf, wins[focus].w, wins[focus].h);
                    surface_mark_dirty(&wins[focus].surf);
                }
            }
            if (resizing && focus >= 0 && band_live) {
                wins[focus].x = band_x;
                wins[focus].y = band_y;
                wins[focus].w = band_w;
                wins[focus].h = band_h;
                desktop_clamp_win_geom(&wins[focus]);
                surface_ensure(&wins[focus].surf, wins[focus].w, wins[focus].h);
                surface_mark_dirty(&wins[focus].surf);
                dirty_bits |= DIRTY_FULL;
            } else if (resizing) {
                dirty_bits |= DIRTY_FULL;
            }
            dragging = 0;
            resizing = 0;
            resize_edge = 0;
            band_live = 0;
            move_prev_valid = 0;
            mouse_clear_clicks();
        }
        if (dragging && focus >= 0 && (m.buttons & 1)) {
            int32_t nx = m.x - drag_off_x;
            int32_t ny = m.y - drag_off_y;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            wins[focus].x = (uint32_t)nx;
            wins[focus].y = (uint32_t)ny;
            wins[focus].maximized = 0;
            desktop_clamp_win_geom(&wins[focus]);
            uint64_t now = timer_ticks();
            if (now - last_drag_tick >= 1) {
                last_drag_tick = now;
                if (!move_live) {
                    if (move_prev_valid)
                        damage_add(move_prev_x, move_prev_y, move_prev_w, move_prev_h);
                    damage_add(wins[focus].x, wins[focus].y, wins[focus].w, wins[focus].h);
                }
                dirty_bits |= DIRTY_MOVE;
            }
        }
        if (resizing && focus >= 0 && (m.buttons & 1)) {
            int32_t dw = m.x - resize_origin_x;
            int32_t dh = m.y - resize_origin_y;
            int32_t nx = (int32_t)resize_orig_x;
            int32_t ny = (int32_t)resize_orig_y;
            int32_t nw = (int32_t)resize_orig_w;
            int32_t nh = (int32_t)resize_orig_h;
            if (resize_edge & 2)
                nw += dw;
            if (resize_edge & 8)
                nh += dh;
            if (resize_edge & 1) {
                nx += dw;
                nw -= dw;
            }
            if (resize_edge & 4) {
                ny += dh;
                nh -= dh;
            }
            if (nw < (int32_t)desktop_win_min_w())
                nw = (int32_t)desktop_win_min_w();
            if (nh < (int32_t)desktop_win_min_h())
                nh = (int32_t)desktop_win_min_h();
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            band_x = (uint32_t)nx;
            band_y = (uint32_t)ny;
            band_w = (uint32_t)nw;
            band_h = (uint32_t)nh;
            {
                struct win tmp = wins[focus];
                tmp.x = band_x;
                tmp.y = band_y;
                tmp.w = band_w;
                tmp.h = band_h;
                desktop_clamp_win_geom(&tmp);
                band_x = tmp.x;
                band_y = tmp.y;
                band_w = tmp.w;
                band_h = tmp.h;
            }
            band_live = 1;
            uint64_t now = timer_ticks();
            if (now - last_drag_tick >= 1) {
                last_drag_tick = now;
                if (move_prev_valid)
                    damage_add(move_prev_x, move_prev_y, move_prev_w, move_prev_h);
                damage_add(band_x, band_y, band_w, band_h);
                dirty_bits |= DIRTY_MOVE;
            }
        }

        uint64_t secs = timer_uptime_secs();
        if (settings_show_clock() && secs != last_clock_secs)
            dirty_bits |= DIRTY_CLOCK;
        else if (!settings_show_clock())
            last_clock_secs = secs;

        if (desktop_find_win(APP_GAME) >= 0 && timer_ticks() - last_game_tick >= 5) {
            last_game_tick = timer_ticks();
            game_tick();
            dirty_bits |= DIRTY_GAME;
            desktop_mark_win_surf_dirty(desktop_find_win(APP_GAME));
        }

        sysmon_poll();
        if (desktop_find_win(APP_MONITOR) >= 0 && timer_ticks() - last_mon_tick >= 50) {
            last_mon_tick = timer_ticks();
            monitor_tick();
            dirty_bits |= DIRTY_MONITOR;
            desktop_mark_win_surf_dirty(desktop_find_win(APP_MONITOR));
        }

        sched_maybe_preempt();

        if (dirty_bits) {
            uint64_t now = timer_ticks();
            int urgent = dragging || resizing || (dirty_bits & DIRTY_MOVE);
            if (urgent || last_present_tick == 0 || now - last_present_tick >= 2) {
                desktop_draw();
                sysmon_note_frame();
                last_present_tick = now;
                cursor_mx = cursor_my = -1;
            }
        }
        desktop_draw_cursor(m.x, m.y);

        if (!dirty_bits) {
            sysmon_idle_enter();
            hlt();
            sysmon_idle_leave();
        }
    }
}
