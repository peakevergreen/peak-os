#include "desktop_internal.h"
#include "fb.h"
#include "settings.h"
#include "theme.h"
#include "wallpaper.h"
#include "peakdisk.h"
#include "privacy.h"
#include "net.h"
#include "tls.h"
#include "util.h"

static const char *persist_profile_label(int profile) {
    switch (profile) {
    case 0: return "private (RAM only)";
    case 1: return "workspace (/home)";
    case 2: return "full (home + system)";
    default: return "unknown";
    }
}

static const char *general_disk_summary(void) {
    if (!peakdisk_available())
        return "No disk — nothing saved between reboots";
    switch (privacy_persist_profile()) {
    case 0: return "Disk present — nothing written (private mode)";
    case 1: return "Disk present — /home saved between reboots";
    case 2: return "Disk present — home and settings saved";
    default: return "Disk present";
    }
}

void desktop_settings_draw(struct win *w) {
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t pad = desktop_u(12);
    uint32_t tx = w->x + pad;
    uint32_t ty = w->y + th + pad;
    uint32_t row = ch + desktop_u(4);
    uint32_t content_w = w->w > pad * 2 ? w->w - pad * 2 : w->w;
    struct framebuffer *fb = fb_get();

    static const char *tabs[SETTINGS_PAGES] =
        {"Display", "Look", "General", "Privacy", "Network"};
    uint32_t tab_w = content_w / SETTINGS_PAGES;
    if (tab_w < desktop_u(56))
        tab_w = desktop_u(56);
    for (int i = 0; i < SETTINGS_PAGES; i++) {
        uint32_t tabx = tx + (uint32_t)i * tab_w;
        uint32_t bg = (i == settings_page) ? desktop_color_accent() : desktop_color_surface();
        uint32_t fg = (i == settings_page) ? desktop_color_bg() : desktop_color_fg();
        fb_fill_rect(tabx, ty, tab_w - desktop_u(4), ch + desktop_u(6), bg);
        fb_draw_string_fit(tabx + desktop_u(4), ty + desktop_u(3), tab_w - desktop_u(8), tabs[i], fg, bg);
    }

    uint32_t cy = ty + ch + desktop_u(16);
    char line[64];

    if (settings_page == 0) {
        fb_draw_string(tx, cy, "UI scale (click):", desktop_color_fg(), desktop_color_bg());
        cy += row;
        snprintf(line, sizeof(line), "%ux  (recommended %ux)",
                 (unsigned)settings_gui_scale(),
                 (unsigned)fb_recommend_scale());
        fb_draw_string(tx, cy, line, desktop_color_accent(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Click to cycle 1–4. High-res defaults larger.", desktop_color_dim(), desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Framebuffer", desktop_color_dim(), desktop_color_bg());
        cy += row;
        snprintf(line, sizeof(line), "%ux%u  %ubpp",
                 (unsigned)fb->width, (unsigned)fb->height, (unsigned)fb->bpp);
        fb_draw_string(tx, cy, line, desktop_color_fg(), desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Resize tip", desktop_color_dim(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Drag the bottom-right grip on any window.", desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Drag the title bar to move.", desktop_color_fg(), desktop_color_bg());
    } else if (settings_page == 1) {
        fb_draw_string(tx, cy, "Theme (click):", desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, theme_name(), desktop_color_accent(), desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Wallpaper (click):", desktop_color_fg(), desktop_color_bg());
        cy += row;
        const char *wp = wallpaper_enabled() ? wallpaper_path() : "none (solid theme)";
        const char *wp_show = wp;
        for (const char *p = wp; *p; p++)
            if (*p == '/')
                wp_show = p + 1;
        fb_draw_string(tx, cy, wp_show, desktop_color_accent(), desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Desktop brand label (click):", desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, settings_show_brand() ? "on" : "off", desktop_color_accent(), desktop_color_bg());
    } else if (settings_page == 2) {
        fb_draw_string(tx, cy, "Taskbar clock (click):", desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, settings_show_clock() ? "on" : "off", desktop_color_accent(), desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "System", desktop_color_dim(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "PeakOS 0.2 — desktop readiness", desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Ctrl+Alt+Esc leaves desktop.", desktop_color_dim(), desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Storage", desktop_color_dim(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, general_disk_summary(), desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Change profile on Privacy tab.", desktop_color_dim(), desktop_color_bg());
    } else if (settings_page == 3) {
        fb_draw_string(tx, cy, "Persistence profile (click):", desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, persist_profile_label(privacy_persist_profile()),
                       desktop_color_accent(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Cycles private → workspace → full.", desktop_color_dim(),
                       desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Network kill switch (click):", desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, privacy_net_kill_switch() ? "on (blocks outbound/listen)"
                                                         : "off",
                       desktop_color_accent(), desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Clear session (click)", desktop_color_accent(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Revokes net grants, caps, clipboard, toasts.", desktop_color_dim(),
                       desktop_color_bg());
    } else {
        struct net_info ni;
        net_get_info(&ni);
        char ip[32], gw[32], dns[32];
        net_format_ip(ni.ip, ip, sizeof(ip));
        net_format_ip(ni.gw, gw, sizeof(gw));
        net_format_ip(ni.dns, dns, sizeof(dns));
        fb_draw_string(tx, cy, "Network", desktop_color_dim(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, ni.up ? "link: up" : "link: down", desktop_color_accent(), desktop_color_bg());
        cy += row;
        snprintf(line, sizeof(line), "ip %s", ip);
        fb_draw_string(tx, cy, line, desktop_color_fg(), desktop_color_bg());
        cy += row;
        snprintf(line, sizeof(line), "gw %s", gw);
        fb_draw_string(tx, cy, line, desktop_color_fg(), desktop_color_bg());
        cy += row;
        snprintf(line, sizeof(line), "dns %s", dns);
        fb_draw_string(tx, cy, line, desktop_color_fg(), desktop_color_bg());
        cy += row * 2;
        snprintf(line, sizeof(line), "Trust on first use (click): %s",
                 settings_tls_tofu() ? "on" : "off");
        fb_draw_string(tx, cy, line, desktop_color_accent(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Forget saved TLS certificates (click)", desktop_color_accent(),
                       desktop_color_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Clears certificate pins, TOFU cache, and HSTS.", desktop_color_dim(),
                       desktop_color_bg());
    }
    (void)content_w;
}

int desktop_settings_click(struct win *w, int32_t mx, int32_t my) {
    uint32_t ch = fb_cell_h();
    uint32_t pad = desktop_u(12);
    uint32_t row_h = ch + desktop_u(4);
    uint32_t content_w = w->w > pad * 2 ? w->w - pad * 2 : w->w;
    uint32_t tab_w = content_w / SETTINGS_PAGES;
    if (tab_w < desktop_u(56))
        tab_w = desktop_u(56);
    uint32_t tabs_y = w->y + desktop_title_h() + pad;
    uint32_t tabs_h = ch + desktop_u(6);
    uint32_t body_y = tabs_y + ch + desktop_u(16);
    if (desktop_point_in(mx, my, w->x + pad, tabs_y, tab_w * SETTINGS_PAGES, tabs_h)) {
        int tab = (int)((mx - (int32_t)(w->x + pad)) / (int32_t)tab_w);
        if (tab >= 0 && tab < SETTINGS_PAGES)
            settings_page = tab;
    } else if (settings_page == 0) {
        if (my >= (int32_t)body_y && my < (int32_t)(body_y + row_h * 2)) {
            settings_cycle_gui_scale();
            settings_persist();
            desktop_rescale_windows();
        }
    } else if (settings_page == 1) {
        int row = (int)((my - (int32_t)body_y) / (int32_t)row_h);
        if (row <= 1) {
            theme_next();
            theme_persist();
        } else if (row <= 4) {
            wallpaper_next();
            wallpaper_persist();
        } else {
            settings_toggle_brand();
            settings_persist();
        }
    } else if (settings_page == 2) {
        if (my >= (int32_t)body_y && my < (int32_t)(body_y + row_h * 2)) {
            settings_toggle_clock();
            settings_persist();
        }
    } else if (settings_page == 3) {
        int row = (int)((my - (int32_t)body_y) / (int32_t)row_h);
        if (row <= 1) {
            int next = (privacy_persist_profile() + 1) % 3;
            privacy_set_persist_profile(next);
        } else if (row <= 4) {
            privacy_set_net_kill_switch(!privacy_net_kill_switch());
        } else if (row >= 6) {
            privacy_clear_session();
        }
    } else if (settings_page == 4) {
        int row = (int)((my - (int32_t)body_y) / (int32_t)row_h);
        if (row == 6) {
            settings_toggle_tls_tofu();
            settings_persist();
        } else if (row == 7) {
            tls_trust_clear_all();
        }
    }
    dirty_bits |= DIRTY_FULL;
    return 1;
}
