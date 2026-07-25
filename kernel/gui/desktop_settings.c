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
#include "notify.h"

static int privacy_kill_arm;

enum settings_hit_act {
    SHIT_NONE = 0,
    SHIT_TAB,
    SHIT_SCALE,
    SHIT_THEME,
    SHIT_WALLPAPER,
    SHIT_BRAND,
    SHIT_CLOCK,
    SHIT_PERSIST,
    SHIT_KILLSW,
    SHIT_CLEAR_SESSION,
    SHIT_TLS_TOFU,
    SHIT_TLS_FORGET,
    SHIT_NETCTL,
};

struct settings_hit {
    uint32_t x, y, w, h;
    enum settings_hit_act act;
    int param;
};

#define SETTINGS_HIT_MAX 40
static struct settings_hit settings_hits[SETTINGS_HIT_MAX];
static int settings_hit_n;

static void settings_hits_reset(void) {
    settings_hit_n = 0;
}

static void settings_hit_add(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             enum settings_hit_act act, int param) {
    if (settings_hit_n >= SETTINGS_HIT_MAX)
        return;
    settings_hits[settings_hit_n].x = x;
    settings_hits[settings_hit_n].y = y;
    settings_hits[settings_hit_n].w = w;
    settings_hits[settings_hit_n].h = h;
    settings_hits[settings_hit_n].act = act;
    settings_hits[settings_hit_n].param = param;
    settings_hit_n++;
}

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

static const char *settings_persist_footer(void) {
    if (!settings_path_survives_reboot("/etc/peak/display"))
        return "Look/Display prefs: session only until full persist";
    if (!peakdisk_available())
        return "Prefs saved in RAM — no block device to flush";
    return "Look/Display prefs survive reboot after disksave";
}

static uint32_t settings_section(uint32_t tx, uint32_t cy, const char *title) {
    fb_draw_string(tx, cy, title, desktop_color_accent(), desktop_color_bg());
    return cy + fb_cell_h() + desktop_u(6);
}

static uint32_t settings_divider(uint32_t tx, uint32_t cy, uint32_t w) {
    fb_fill_rect(tx, cy, w, desktop_u(1), desktop_color_border());
    return cy + desktop_u(8);
}

static void settings_draw_toggle(uint32_t tx, uint32_t cy, const char *label, int on) {
    fb_draw_string(tx, cy, label, desktop_color_fg(), desktop_color_bg());
    uint32_t pill_x = tx + desktop_u(200);
    uint32_t pill_w = desktop_u(36);
    uint32_t pill_h = fb_cell_h() + desktop_u(2);
    fb_fill_rect(pill_x, cy, pill_w, pill_h,
                 on ? desktop_color_accent() : desktop_color_surface());
    fb_draw_string_fit(pill_x + desktop_u(6), cy + desktop_u(1), pill_w,
                       on ? "on" : "off",
                       on ? desktop_color_bg() : desktop_color_dim(),
                       on ? desktop_color_accent() : desktop_color_surface());
}

static void settings_draw_scale_preview(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const struct peak_theme *t = theme_get();
    fb_fill_rect(x, y, w, h, t->surface);
    fb_fill_rect(x, y, w, desktop_u(8), t->title);
    fb_fill_rect(x, y + desktop_u(8), w, desktop_u(1), t->border);
    fb_draw_string_fit(x + desktop_u(4), y + desktop_u(10), w - desktop_u(8),
                       "Preview", t->fg, t->surface);
    fb_fill_rect(x + desktop_u(4), y + h - desktop_u(10),
                 w - desktop_u(8), desktop_u(6), t->accent);
}

static void settings_draw_theme_swatches(uint32_t tx, uint32_t cy, uint32_t content_w) {
    uint32_t sw = desktop_u(40);
    uint32_t gap = desktop_u(6);
    int n = theme_count();
    uint32_t row_h = sw + fb_cell_h() + desktop_u(4);

    for (int i = 0; i < n; i++) {
        const struct peak_theme *t = theme_at(i);
        uint32_t sx = tx + (uint32_t)i * (sw + gap);
        if (sx + sw > tx + content_w)
            break;
        fb_fill_rect(sx, cy, sw, sw, t->bg);
        fb_fill_rect(sx, cy, sw, desktop_u(8), t->title);
        fb_fill_rect(sx + desktop_u(3), cy + desktop_u(10), sw - desktop_u(6),
                     desktop_u(5), t->accent);
        if (i == theme_index()) {
            fb_fill_rect(sx, cy, sw, desktop_u(2), t->accent);
            fb_fill_rect(sx, cy + sw - desktop_u(2), sw, desktop_u(2), t->accent);
            fb_fill_rect(sx, cy, desktop_u(2), sw, t->accent);
            fb_fill_rect(sx + sw - desktop_u(2), cy, desktop_u(2), sw, t->accent);
        }
        fb_draw_string_fit(sx, cy + sw + desktop_u(2), sw, t->name,
                           i == theme_index() ? desktop_color_accent() : desktop_color_dim(),
                           desktop_color_bg());
        settings_hit_add(sx, cy, sw, row_h, SHIT_THEME, i);
    }
}

static void settings_draw_wallpaper_row(uint32_t tx, uint32_t cy, uint32_t content_w) {
    uint32_t tw = desktop_u(56);
    uint32_t th = desktop_u(32);
    uint32_t gap = desktop_u(8);
    int n = wallpaper_option_count();
    uint32_t row_h = th + fb_cell_h() + desktop_u(4);

    for (int i = 0; i < n; i++) {
        uint32_t sx = tx + (uint32_t)i * (tw + gap);
        if (sx + tw > tx + content_w)
            break;
        if (i == 0) {
            fb_fill_rect(sx, cy, tw, th, theme_get()->bg);
            fb_draw_string_fit(sx + desktop_u(4), cy + desktop_u(10), tw - desktop_u(8),
                               "solid", desktop_color_dim(), theme_get()->bg);
        } else {
            wallpaper_draw_option(i, sx, cy, tw, th);
        }
        if (i == wallpaper_option_index()) {
            fb_fill_rect(sx, cy, tw, desktop_u(2), desktop_color_accent());
            fb_fill_rect(sx, cy + th - desktop_u(2), tw, desktop_u(2), desktop_color_accent());
            fb_fill_rect(sx, cy, desktop_u(2), th, desktop_color_accent());
            fb_fill_rect(sx + tw - desktop_u(2), cy, desktop_u(2), th, desktop_color_accent());
        }
        fb_draw_string_fit(sx, cy + th + desktop_u(2), tw, wallpaper_option_label(i),
                           i == wallpaper_option_index() ? desktop_color_accent() : desktop_color_dim(),
                           desktop_color_bg());
        settings_hit_add(sx, cy, tw, row_h, SHIT_WALLPAPER, i);
    }
}

void desktop_settings_draw(struct win *w) {
    settings_hits_reset();

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
        settings_hit_add(tabx, ty, tab_w - desktop_u(4), ch + desktop_u(6), SHIT_TAB, i);
    }

    uint32_t cy = ty + ch + desktop_u(16);
    char line[64];

    if (settings_page == 0) {
        cy = settings_section(tx, cy, "UI scale");
        uint32_t preview_w = desktop_u(72);
        uint32_t preview_h = desktop_u(36);
        settings_draw_scale_preview(tx, cy, preview_w, preview_h);
        uint32_t chip_x = tx + preview_w + desktop_u(12);
        uint32_t chip_w = desktop_u(28);
        uint32_t chip_h = ch + desktop_u(4);
        for (uint32_t s = 1; s <= 4; s++) {
            uint32_t cx = chip_x + (s - 1) * (chip_w + desktop_u(4));
            int sel = (s == settings_gui_scale());
            fb_fill_rect(cx, cy, chip_w, chip_h,
                         sel ? desktop_color_accent() : desktop_color_surface());
            snprintf(line, sizeof(line), "%ux", (unsigned)s);
            fb_draw_string_fit(cx + desktop_u(4), cy + desktop_u(2), chip_w - desktop_u(4), line,
                               sel ? desktop_color_bg() : desktop_color_fg(),
                               sel ? desktop_color_accent() : desktop_color_surface());
            settings_hit_add(cx, cy, chip_w, chip_h, SHIT_SCALE, (int)s);
        }
        cy += preview_h + desktop_u(8);
        snprintf(line, sizeof(line), "Recommended %ux for %ux%u",
                 (unsigned)fb_recommend_scale(), (unsigned)fb->width, (unsigned)fb->height);
        fb_draw_string(tx, cy, line, desktop_color_dim(), desktop_color_bg());
        cy = settings_divider(tx, cy + row, content_w);
        cy = settings_section(tx, cy, "Display");
        snprintf(line, sizeof(line), "%ux%u  %ubpp",
                 (unsigned)fb->width, (unsigned)fb->height, (unsigned)fb->bpp);
        fb_draw_string(tx, cy, line, desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Drag window grip to resize; title bar to move.",
                       desktop_color_dim(), desktop_color_bg());
    } else if (settings_page == 1) {
        cy = settings_section(tx, cy, "Theme");
        settings_draw_theme_swatches(tx, cy, content_w);
        cy += desktop_u(40) + ch + desktop_u(12);
        cy = settings_divider(tx, cy, content_w);
        cy = settings_section(tx, cy, "Wallpaper");
        settings_draw_wallpaper_row(tx, cy, content_w);
        cy += desktop_u(32) + ch + desktop_u(12);
        cy = settings_divider(tx, cy, content_w);
        cy = settings_section(tx, cy, "Desktop chrome");
        settings_draw_toggle(tx, cy, "Brand label on desktop", settings_show_brand());
        settings_hit_add(tx, cy, content_w, row * 2, SHIT_BRAND, 0);
        cy += row * 2;
        fb_draw_string(tx, cy, settings_persist_footer(), desktop_color_dim(), desktop_color_bg());
    } else if (settings_page == 2) {
        cy = settings_section(tx, cy, "Taskbar");
        settings_draw_toggle(tx, cy, "Show clock", settings_show_clock());
        settings_hit_add(tx, cy, content_w, row * 2, SHIT_CLOCK, 0);
        cy += row * 2;
        cy = settings_divider(tx, cy, content_w);
        cy = settings_section(tx, cy, "System");
        fb_draw_string(tx, cy, "PeakOS 0.2 — desktop readiness", desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Ctrl+Alt+Esc — return to CLI", desktop_color_dim(), desktop_color_bg());
        cy += row;
        cy = settings_divider(tx, cy, content_w);
        cy = settings_section(tx, cy, "Storage");
        fb_draw_string(tx, cy, general_disk_summary(), desktop_color_fg(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "Change persist profile on Privacy tab.", desktop_color_dim(),
                       desktop_color_bg());
    } else if (settings_page == 3) {
        cy = settings_section(tx, cy, "Persistence");
        fb_draw_string(tx, cy, "Profile (click to cycle):", desktop_color_fg(), desktop_color_bg());
        settings_hit_add(tx, cy, content_w, row * 3, SHIT_PERSIST, 0);
        cy += row;
        fb_draw_string(tx, cy, persist_profile_label(privacy_persist_profile()),
                       desktop_color_accent(), desktop_color_bg());
        cy += row;
        fb_draw_string(tx, cy, "private → workspace → full", desktop_color_dim(),
                       desktop_color_bg());
        cy += row;
        cy = settings_divider(tx, cy, content_w);
        cy = settings_section(tx, cy, "Network safety");
        fb_draw_string(tx, cy, "Kill switch (click):", desktop_color_fg(), desktop_color_bg());
        settings_hit_add(tx, cy, content_w, row * 2, SHIT_KILLSW, 0);
        cy += row;
        if (privacy_kill_arm && !privacy_net_kill_switch()) {
            fb_draw_string(tx, cy, "click again to ENABLE (blocks all net)",
                           theme_get()->danger, desktop_color_bg());
        } else {
            fb_draw_string(tx, cy, privacy_net_kill_switch() ? "on (blocks outbound/listen)"
                                                             : "off — click twice to enable",
                           desktop_color_accent(), desktop_color_bg());
        }
        cy += row * 2;
        cy = settings_divider(tx, cy, content_w);
        cy = settings_section(tx, cy, "Session");
        fb_draw_string(tx, cy, "Clear session (click)", desktop_color_accent(), desktop_color_bg());
        settings_hit_add(tx, cy, content_w, row * 2, SHIT_CLEAR_SESSION, 0);
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
        cy = settings_section(tx, cy, "Link");
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
        cy += row;
        cy = settings_divider(tx, cy, content_w);
        cy = settings_section(tx, cy, "TLS trust");
        settings_draw_toggle(tx, cy, "Trust on first use", settings_tls_tofu());
        settings_hit_add(tx, cy, content_w, row, SHIT_TLS_TOFU, 0);
        cy += row * 2;
        fb_draw_string(tx, cy, "Forget saved TLS certificates (click)", desktop_color_accent(),
                       desktop_color_bg());
        settings_hit_add(tx, cy, content_w, row, SHIT_TLS_FORGET, 0);
        cy += row;
        fb_draw_string(tx, cy, "Clears certificate pins, TOFU cache, and HSTS.", desktop_color_dim(),
                       desktop_color_bg());
        cy += row;
        cy = settings_divider(tx, cy, content_w);
        cy = settings_section(tx, cy, "Tools");
        fb_draw_string(tx, cy, "Open Net Control (click)", desktop_color_accent(), desktop_color_bg());
        settings_hit_add(tx, cy, content_w, row * 2, SHIT_NETCTL, 0);
        cy += row;
        fb_draw_string(tx, cy, "Privacy, kill switch, DHCP renew, RNG status.", desktop_color_dim(),
                       desktop_color_bg());
    }
}

static void settings_hit_dispatch(enum settings_hit_act act, int param) {
    switch (act) {
    case SHIT_TAB:
        break;
    case SHIT_SCALE:
        if (param >= 1 && param <= 4)
            settings_set_gui_scale((uint32_t)param);
        else
            settings_cycle_gui_scale();
        settings_persist();
        settings_notify_persist("/etc/peak/display", "Display");
        desktop_rescale_windows();
        break;
    case SHIT_THEME:
        if (param >= 0 && param < theme_count())
            theme_set_index(param);
        else
            theme_next();
        theme_persist();
        break;
    case SHIT_WALLPAPER:
        if (param >= 0 && param < wallpaper_option_count())
            wallpaper_select_index(param);
        else
            wallpaper_next();
        wallpaper_persist();
        break;
    case SHIT_BRAND:
        settings_toggle_brand();
        settings_persist();
        settings_notify_persist("/etc/peak/display", "Display");
        break;
    case SHIT_CLOCK:
        settings_toggle_clock();
        settings_persist();
        settings_notify_persist("/etc/peak/display", "Display");
        break;
    case SHIT_PERSIST: {
        int next = (privacy_persist_profile() + 1) % 3;
        privacy_set_persist_profile(next);
        notify_push("Persist profile changed");
        break;
    }
    case SHIT_KILLSW:
        if (privacy_net_kill_switch()) {
            privacy_set_net_kill_switch(0);
            privacy_kill_arm = 0;
            notify_push("Kill switch off");
        } else if (!privacy_kill_arm) {
            privacy_kill_arm = 1;
            notify_push("Click kill switch again to confirm");
        } else {
            privacy_set_net_kill_switch(1);
            privacy_kill_arm = 0;
            notify_push("Kill switch on — network blocked");
        }
        break;
    case SHIT_CLEAR_SESSION:
        privacy_kill_arm = 0;
        privacy_clear_session();
        notify_push("Session cleared");
        break;
    case SHIT_TLS_TOFU:
        settings_toggle_tls_tofu();
        settings_persist();
        settings_notify_persist("/etc/peak/display", "Display");
        break;
    case SHIT_TLS_FORGET:
        tls_trust_clear_all();
        notify_push("TLS trust cache cleared");
        break;
    case SHIT_NETCTL:
        desktop_open_app(APP_NETCTL);
        break;
    default:
        break;
    }
}

int desktop_settings_click(struct win *w, int32_t mx, int32_t my) {
    for (int i = 0; i < settings_hit_n; i++) {
        struct settings_hit *h = &settings_hits[i];
        if (!desktop_point_in(mx, my, h->x, h->y, h->w, h->h))
            continue;
        if (h->act == SHIT_TAB) {
            if (h->param >= 0 && h->param < SETTINGS_PAGES)
                settings_page = h->param;
        } else {
            settings_hit_dispatch(h->act, h->param);
        }
        dirty_bits |= DIRTY_FULL;
        return 1;
    }
    (void)w;
    return 0;
}
