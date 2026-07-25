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
        if (wallpaper_enabled()) {
            uint32_t veil = desktop_color_bg() & 0x00FFFFFF;
            veil |= 0xA8000000;
            fb_fill_rect(0, 0, (uint32_t)fb->width, (uint32_t)fb->height, veil);
        }
        uint32_t mw = desktop_u(380);
        uint32_t mh = desktop_u(196);
        uint32_t mx = ((uint32_t)fb->width - mw) / 2;
        uint32_t my = ((uint32_t)fb->height - mh) / 3;
        fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
        fb_fill_rect(mx, my, mw, desktop_u(4), desktop_color_accent());
        fb_fill_rect(mx, my + desktop_u(4), mw, desktop_u(1), desktop_color_border());
        uint32_t tx = mx + desktop_u(28);
        uint32_t ty = my + desktop_u(28);
        fb_draw_string(tx, ty, "PeakOS", desktop_color_fg(), desktop_color_surface());
        fb_draw_string(tx + fb_cell_w() * 6, ty, "0.2.0-ai", desktop_color_accent(), desktop_color_surface());
        ty += fb_cell_h() + desktop_u(10);
        fb_fill_rect(tx, ty, mw - desktop_u(56), desktop_u(1), desktop_color_border());
        ty += desktop_u(14);
        fb_draw_string(tx, ty, "Press Enter or Space to sign in", desktop_color_fg(), desktop_color_surface());
        ty += fb_cell_h() + desktop_u(8);
        fb_draw_string(tx, ty, "Single-user research workstation", desktop_color_dim(), desktop_color_surface());
        ty += fb_cell_h() + desktop_u(6);
        fb_draw_string(tx, ty, "Esc skips splash", desktop_color_dim(), desktop_color_surface());
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
