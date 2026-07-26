#include "desktop_internal.h"
#include "fb.h"
#include "gui.h"
#include "keyboard.h"
#include "mouse.h"
#include "notify.h"
#include "clipboard.h"
#include "power.h"
#include "timer.h"
#include "util.h"

int alttab_open;
int alttab_sel;
int help_open;
static char help_filter[24];
int notify_hist_open;
int session_lock;
int power_confirm;

static const char *alttab_app_hint(enum app_kind k) {
    switch (k) {
    case APP_TERM: return "Shell";
    case APP_FILES: return "Browse";
    case APP_SETTINGS: return "Prefs";
    case APP_AGENT: return "Ask";
    case APP_GAME: return "Game";
    case APP_BROWSER: return "Web";
    case APP_MONITOR: return "Stats";
    case APP_NOTEPAD: return "Edit";
    case APP_IMAGES: return "View";
    case APP_DISKS: return "Volume";
    case APP_NETEXP: return "Diag";
    case APP_NETCTL: return "Policy";
    }
    return "";
}

void desktop_draw_session_overlays(void) {
    struct framebuffer *fb = fb_get();
    if (session_lock) {
        fb_fill_rect(0, 0, (uint32_t)fb->width, (uint32_t)fb->height, desktop_color_bg());
        uint32_t mw = desktop_u(340);
        uint32_t mh = desktop_u(140);
        uint32_t mx = ((uint32_t)fb->width - mw) / 2;
        uint32_t my = ((uint32_t)fb->height - mh) / 3;
        fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
        fb_fill_rect(mx, my, mw, desktop_u(3), desktop_color_accent());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(28), "Session locked", desktop_color_fg(), desktop_color_surface());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(28) + fb_cell_h() + desktop_u(4),
                       "Idle privacy cover — not a password login",
                       desktop_color_dim(), desktop_color_surface());
        fb_draw_string(mx + desktop_u(24), my + desktop_u(28) + 2 * (fb_cell_h() + desktop_u(4)),
                       "Press Enter to resume (single-user)", desktop_color_dim(), desktop_color_surface());
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
    uint32_t row_h = fb_cell_h() + desktop_u(8);
    uint32_t footer = fb_cell_h() + desktop_u(10);
    uint32_t mw = desktop_u(360);
    uint32_t mh = desktop_u(44) + (uint32_t)n * row_h + footer;
    uint32_t mx = ((uint32_t)fb->width - mw) / 2;
    uint32_t my = ((uint32_t)fb->height - mh) / 3;
    fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
    fb_fill_rect(mx, my, mw, desktop_u(3), desktop_color_accent());
    fb_draw_string(mx + desktop_u(14), my + desktop_u(10), "Alt+Tab", desktop_color_accent(), desktop_color_surface());
    fb_draw_string(mx + desktop_u(14) + fb_cell_w() * 7, my + desktop_u(10),
                   "switch window", desktop_color_dim(), desktop_color_surface());
    for (int i = 0; i < n; i++) {
        int sel = (i == alttab_sel);
        uint32_t bg = sel ? desktop_color_accent() : desktop_color_bg();
        uint32_t fg = sel ? desktop_color_bg() : desktop_color_fg();
        uint32_t ry = my + desktop_u(36) + (uint32_t)i * row_h;
        fb_fill_rect(mx + desktop_u(10), ry, mw - desktop_u(20), row_h - desktop_u(2), bg);
        if (sel)
            fb_fill_rect(mx + desktop_u(10), ry, desktop_u(3), row_h - desktop_u(2), desktop_color_fg());
        char badge[4];
        badge[0] = (char)('1' + (i < 9 ? i : 9));
        badge[1] = '.';
        badge[2] = '\0';
        fb_draw_string(mx + desktop_u(18), ry + desktop_u(4), badge, fg, bg);
        const char *title = desktop_app_title(wins[order[i]].kind);
        fb_draw_string(mx + desktop_u(36), ry + desktop_u(4), title, fg, bg);
        const char *hint = alttab_app_hint(wins[order[i]].kind);
        if (hint[0]) {
            uint32_t hx = mx + mw - desktop_u(18) - (uint32_t)strlen(hint) * fb_cell_w();
            fb_draw_string(hx, ry + desktop_u(4), hint, sel ? desktop_color_surface() : desktop_color_dim(), bg);
        }
        if (wins[order[i]].minimized) {
            fb_draw_string(mx + desktop_u(36), ry + desktop_u(4) + fb_cell_h(),
                           "minimized", desktop_color_dim(), bg);
        }
    }
    uint32_t fy = my + mh - footer;
    fb_draw_string(mx + desktop_u(14), fy, "Release Alt to switch · Tab next · Esc cancel",
                   desktop_color_dim(), desktop_color_surface());
}


void desktop_draw_notify_history(void) {
    if (!notify_hist_open)
        return;
    struct framebuffer *fb = fb_get();
    int n = notify_history_count();
    int clip_n = clipboard_history_count();
    uint32_t ch = fb_cell_h() + desktop_u(4);
    uint32_t rows = (uint32_t)(n > 0 ? n : 1);
    if (rows > 8)
        rows = 8;
    uint32_t mw = desktop_u(480);
    uint32_t mh = desktop_u(56) + rows * ch + desktop_u(36);
    uint32_t mx = ((uint32_t)fb->width - mw) / 2;
    uint32_t my = desktop_u(48);
    fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
    fb_fill_rect(mx, my, mw, desktop_u(3), desktop_color_accent());
    uint32_t pad = desktop_u(16);
    uint32_t cy = my + pad;
    fb_draw_string(mx + pad, cy, "Notification history", desktop_color_accent(), desktop_color_surface());
    cy += ch + desktop_u(4);
    char line[96 + 8];
    if (n == 0)
        fb_draw_string(mx + pad, cy, "(empty)", desktop_color_dim(), desktop_color_surface());
    else {
        for (int i = 0; i < n && i < 8; i++) {
            char msg[96];
            if (!notify_history_get(i, msg, sizeof(msg)))
                break;
            snprintf(line, sizeof(line), "%d. %s", i + 1, msg);
            fb_draw_string_fit(mx + pad, cy, mw - 2 * pad, line, desktop_color_fg(), desktop_color_surface());
            cy += ch;
        }
    }
    cy += desktop_u(4);
    snprintf(line, sizeof(line), "Clipboard ring: %d entries · Ctrl+Shift+V paste previous",
             clip_n);
    fb_draw_string_fit(mx + pad, cy, mw - 2 * pad, line, desktop_color_dim(), desktop_color_surface());
    cy = my + mh - pad - fb_cell_h();
    fb_draw_string(mx + pad, cy, "Esc / click to close · Start menu → Alerts",
                   desktop_color_dim(), desktop_color_surface());
}


static const char *desktop_help_focus_hint(void) {
    if (focus < 0 || !wins[focus].open)
        return "Focus an app window for context hints.";
    switch (wins[focus].kind) {
    case APP_TERM: return "Terminal: Ctrl+F find, wheel scrollback.";
    case APP_FILES: return "Files: Tab/arrows select; drag row to open.";
    case APP_NOTEPAD: return "Notepad: Ctrl+S save, Ctrl+F find.";
    case APP_BROWSER: return "Browser: Shift+T restore tab; wheel scroll.";
    case APP_AGENT: return "Agent: type to filter transcript; Y/N writes.";
    case APP_MONITOR: return "Monitor: E or Export button -> /tmp/sysmon.txt.";
    default: return "Right-click title bar for window menu.";
    }
}
static int help_substring_match(const char *hay, const char *needle) {
    if (!needle || !needle[0])
        return 1;
    if (!hay)
        return 0;
    for (size_t i = 0; hay[i]; i++) {
        size_t j = 0;
        while (needle[j] && hay[i + j] &&
               (hay[i + j] == needle[j] ||
                (hay[i + j] >= 'A' && hay[i + j] <= 'Z' &&
                 hay[i + j] + 32 == needle[j]) ||
                (needle[j] >= 'A' && needle[j] <= 'Z' &&
                 needle[j] + 32 == hay[i + j])))
            j++;
        if (!needle[j])
            return 1;
    }
    return 0;
}
static int help_line_matches(const char *key, const char *desc) {
    if (!help_filter[0])
        return 1;
    return help_substring_match(key, help_filter) ||
           help_substring_match(desc, help_filter);
}
void desktop_draw_help(void) {
    if (!help_open)
        return;
    struct framebuffer *fb = fb_get();
    uint32_t mw = desktop_u(460);
    uint32_t mh = desktop_u(400);
    uint32_t mx = ((uint32_t)fb->width - mw) / 2;
    uint32_t my = desktop_u(64);
    fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
    fb_fill_rect(mx, my, mw, desktop_u(3), desktop_color_accent());
    fb_fill_rect(mx, my + mh - desktop_u(1), mw, desktop_u(1), desktop_color_dim());
    uint32_t pad = desktop_u(16);
    uint32_t cy = my + pad;
    uint32_t ch = fb_cell_h() + desktop_u(4);
    fb_draw_string(mx + pad, cy, "Keyboard shortcuts", desktop_color_accent(), desktop_color_surface());
    cy += ch + desktop_u(4);
    fb_draw_string(mx + pad, cy, desktop_help_focus_hint(), desktop_color_dim(), desktop_color_surface());
    cy += ch + desktop_u(2);
    { char hf[40]; snprintf(hf, sizeof(hf), "Filter: %s_", help_filter); fb_draw_string(mx + pad, cy, hf, desktop_color_fg(), desktop_color_surface()); }
    cy += ch + desktop_u(4);
    fb_fill_rect(mx + pad, cy, mw - 2 * pad, desktop_u(1), desktop_color_dim());
    cy += desktop_u(8);
    struct help_line { const char *key; const char *desc; };
    static const struct help_line lines[] = {
        { "1-7", "Open apps (Term, Files, Settings, …)" },
        { "Alt+Tab", "Switch windows" },
        { "Ctrl+W", "Close focused window" },
        { "Ctrl+Alt+Esc", "Return to CLI" },
        { "S / T", "Scale / theme" },
        { "Esc", "Close menus & overlays" },
        { "Peak menu", "Type to filter apps; Enter to launch" },
        { "Ctrl+Alt+←/→/↑", "Snap window left/right/maximize" },
        { "Ctrl+Alt+Shift+arrows", "Nudge window 8px" },
        { "Drag title", "Snap left/right/top edges" },
        { "Title _ [] x", "Minimize / maximize / close" },
        { "Ctrl+Shift+H", "Notification history panel" },
        { "Ctrl+Shift+V", "Paste previous clipboard" },
        { "Toast x", "Dismiss notification" },
        { "Wheel", "Scroll Files, Terminal, Browser" },
        { "Right-click", "Context menus" },
    };
    uint32_t key_w = desktop_u(100);
    int shown = 0;
    for (int i = 0; i < 16; i++) {
        if (!help_line_matches(lines[i].key, lines[i].desc)) continue;
        shown++;
        uint32_t kbg = desktop_color_bg();
        fb_fill_rect(mx + pad, cy, key_w, fb_cell_h() + desktop_u(2), kbg);
        fb_draw_string(mx + pad + desktop_u(6), cy + desktop_u(1), lines[i].key,
                       desktop_color_accent(), kbg);
        fb_draw_string(mx + pad + key_w + desktop_u(8), cy + desktop_u(1), lines[i].desc,
                       desktop_color_fg(), desktop_color_surface());
        cy += ch;
    }
    if (!shown) fb_draw_string(mx + pad, cy, "(no match)", desktop_color_dim(), desktop_color_surface());
    cy = my + mh - pad - fb_cell_h();
    fb_draw_string(mx + pad, cy, "Click anywhere or Esc to close",
                   desktop_color_dim(), desktop_color_surface());
}

void desktop_overlays_idle_lock(uint64_t last_input_tick) {
    if (!session_lock && !power_confirm &&
        timer_ticks() - last_input_tick > 30000) {
        session_lock = 1;
        dirty_bits |= DIRTY_FULL;
    }
}

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
    if (!(alttab_open || help_open || notify_hist_open))
        return 0;
    alttab_open = help_open = notify_hist_open = 0;
    dirty_bits |= DIRTY_FULL;
    return 1;
}


int desktop_notify_hist_click_dismiss(void) {
    if (!notify_hist_open)
        return 0;
    notify_hist_open = 0;
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

int desktop_notify_click_dismiss(int32_t mx, int32_t my) {
    struct framebuffer *fb = fb_get();
    if (!notify_click(mx, my, (uint32_t)fb->width))
        return 0;
    dirty_bits |= DIRTY_TOAST;
    return 1;
}

int desktop_help_filter_key(int key) {
    if (!help_open) return 0;
    if (key == 27) { help_filter[0] = 0; return 1; }
    if (key == 8 || key == 127) {
        size_t n = strlen(help_filter);
        if (n) help_filter[n - 1] = 0;
        return 1;
    }
    if (key >= 32 && key < 127) {
        size_t n = strlen(help_filter);
        if (n + 1 < sizeof(help_filter)) { help_filter[n] = (char)key; help_filter[n + 1] = 0; }
        return 1;
    }
    return 0;
}
