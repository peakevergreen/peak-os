#include "desktop_internal.h"
#include "fb.h"
#include "gui.h"
#include "keyboard.h"
#include "mouse.h"
#include "notify.h"
#include "power.h"
#include "timer.h"
#include "util.h"

int alttab_open;
int alttab_sel;
int help_open;
int session_lock;
int power_confirm;

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

void desktop_overlays_idle_lock(uint64_t last_input_tick) {
    if (!session_lock && !power_confirm &&
        timer_ticks() - last_input_tick > 30000) {
        session_lock = 1;
        dirty_bits |= DIRTY_FULL;
    }
}

/* Returns 1 if input is blocked (caller should continue main loop). */
int desktop_overlays_block_input(int key) {
    if (session_lock) {
        if (key == '\n' || key == ' ') {
            session_lock = 0;
            dirty_bits |= DIRTY_FULL;
        }
        if (dirty_bits)
            desktop_draw();
        mouse_clear_clicks();
        hlt_if_enabled();
        return 1;
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
        return 1;
    }
    return 0;
}

void desktop_alttab_advance(void) {
    int n = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open)
            n++;
    if (n > 0) {
        if (!alttab_open) {
            alttab_open = 1;
            alttab_sel = 0;
        } else {
            alttab_sel = (alttab_sel + 1) % n;
        }
        dirty_bits |= DIRTY_FULL;
    }
}

void desktop_alttab_commit_if_open(void) {
    if (!alttab_open || keyboard_alt_down())
        return;
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

int desktop_overlays_close_popups(void) {
    if (!(alttab_open || help_open))
        return 0;
    alttab_open = help_open = 0;
    dirty_bits |= DIRTY_FULL;
    return 1;
}

int desktop_help_click_dismiss(void) {
    if (!help_open)
        return 0;
    help_open = 0;
    dirty_bits |= DIRTY_FULL;
    return 1;
}
