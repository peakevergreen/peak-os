#include "desktop_internal.h"
#include "fb.h"
#include "keyboard.h"
#include "wallpaper.h"
#include "sound.h"
#include "notify.h"
#include "util.h"

static int login_done;

void desktop_login(void) {
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
