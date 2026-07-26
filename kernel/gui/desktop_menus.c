#include "desktop_internal.h"
#include "ui_widgets.h"
#include "fb.h"
#include "keyboard.h"
#include "theme.h"
#include "timer.h"
#include "rtc.h"
#include "settings.h"
#include "wallpaper.h"
#include "net.h"
#include "notify.h"
#include "peakdisk.h"
#include "privacy.h"
#include "util.h"
#include "sound.h"
#include "browser.h"
#include "browser_internal.h"
#include "monitor.h"
#include "sysmon.h"
#include "clipboard.h"
#include "shell.h"

int menu_open;
char start_filter[24];
int start_sel;
int ctx_menu;
enum ctx_target ctx_target_kind;
int ctx_win;
struct ctx_menu_item ctx_items[CTX_MENU_MAX_ITEMS];
int ctx_item_count;
struct ctx_menu_spec ctx_spec;

static int taskbar_all[MAX_WINS];
static int taskbar_order[MAX_WINS];
static int taskbar_visible;
static int taskbar_overflow;

static int app_kind_ready(enum app_kind k) {
    switch (k) {
    default:
        return 1;
    }
}

static void taskbar_rebuild(void) {
    taskbar_visible = 0;
    taskbar_overflow = 0;
    int n = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open)
            taskbar_all[n++] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (wins[taskbar_all[j]].z < wins[taskbar_all[i]].z) {
                int t = taskbar_all[i];
                taskbar_all[i] = taskbar_all[j];
                taskbar_all[j] = t;
            }
    struct framebuffer *fb = fb_get();
    uint32_t bx = desktop_u(70);
    uint32_t bw = desktop_taskbar_btn_w();
    uint32_t end = (uint32_t)fb->width - desktop_u(120);
    int max_slots = bx < end ? (int)((end - bx) / bw) : 0;
    if (n > max_slots && max_slots > 0)
        max_slots--;
    if (max_slots > n)
        max_slots = n;
    for (int i = 0; i < max_slots; i++)
        taskbar_order[i] = taskbar_all[i];
    taskbar_visible = max_slots;
    taskbar_overflow = n - max_slots;
}

int desktop_taskbar_visible_slots(void) {
    taskbar_rebuild();
    return taskbar_visible;
}

int desktop_taskbar_map_win(int slot, int *win_idx) {
    taskbar_rebuild();
    if (slot < 0 || slot >= taskbar_visible || !win_idx)
        return 0;
    *win_idx = taskbar_order[slot];
    return 1;
}

int desktop_taskbar_hit_button(int32_t mx, int32_t my, int *win_idx, int *overflow_btn) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t ty = (uint32_t)fb->height - th;
    if (!desktop_point_in(mx, my, desktop_u(70), ty, (uint32_t)fb->width - desktop_u(180), th))
        return 0;
    taskbar_rebuild();
    uint32_t bx = desktop_u(70);
    uint32_t bw = desktop_taskbar_btn_w();
    uint32_t by = ty + desktop_u(4);
    uint32_t bh = th > desktop_u(8) ? th - desktop_u(8) : th;
    for (int s = 0; s < taskbar_visible; s++) {
        if (desktop_point_in(mx, my, bx, by, bw - desktop_u(4), bh)) {
            if (win_idx)
                *win_idx = taskbar_order[s];
            if (overflow_btn)
                *overflow_btn = 0;
            return 1;
        }
        bx += bw;
    }
    if (taskbar_overflow > 0 &&
        desktop_point_in(mx, my, bx, by, bw - desktop_u(4), bh)) {
        if (win_idx)
            *win_idx = -1;
        if (overflow_btn)
            *overflow_btn = 1;
        return 1;
    }
    return 0;
}

int desktop_taskbar_raise_overflow(void) {
    taskbar_rebuild();
    if (taskbar_overflow <= 0)
        return 0;
    int idx = taskbar_all[taskbar_visible];
    wins[idx].minimized = 0;
    desktop_raise_win(idx);
    sound_ui_click();
    return 1;
}

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

    taskbar_rebuild();
    uint32_t bx = desktop_u(70);
    uint32_t bw = desktop_taskbar_btn_w();
    uint32_t by = y + desktop_u(4);
    uint32_t bh = th > desktop_u(8) ? th - desktop_u(8) : th;
    for (int s = 0; s < taskbar_visible; s++) {
        int i = taskbar_order[s];
        uint32_t bg = (i == focus && !wins[i].minimized) ? desktop_color_accent() : desktop_color_bg();
        uint32_t fg = (i == focus && !wins[i].minimized) ? desktop_color_bg() : desktop_color_fg();
        if (wins[i].minimized)
            bg = desktop_color_dim();
        fb_fill_rect(bx, by, bw - desktop_u(4), bh, bg);
        fb_draw_string_fit(bx + desktop_u(4), by + (bh - fb_cell_h()) / 2, bw - desktop_u(12),
                           desktop_app_title(wins[i].kind), fg, bg);
        bx += bw;
    }
    if (taskbar_overflow > 0) {
        fb_fill_rect(bx, by, bw - desktop_u(4), bh, desktop_color_dim());
        char obuf[8];
        snprintf(obuf, sizeof(obuf), "+%d", taskbar_overflow);
        fb_draw_string_fit(bx + desktop_u(4), by + (bh - fb_cell_h()) / 2, bw - desktop_u(12),
                           obuf, desktop_color_fg(), desktop_color_dim());
    }

    struct net_info ni;
    net_get_info(&ni);
    fb_draw_string_fit((uint32_t)fb->width - desktop_u(160), y + (th - fb_cell_h()) / 2,
                       desktop_u(50), ni.up ? "net" : "off", ni.up ? desktop_color_accent() : desktop_color_dim(),
                       desktop_color_surface());
    desktop_draw_clock_area();
}

#define START_APPS 12
#define START_SYS  8
#define START_SEARCH_H (fb_cell_h() + desktop_u(10))

static int str_icontains(const char *hay, const char *needle) {
    if (!needle[0])
        return 1;
    for (const char *p = hay; *p; p++) {
        const char *n = needle;
        const char *h = p;
        while (*n && *h) {
            char a = *h, b = *n;
            if (a >= 'A' && a <= 'Z')
                a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z')
                b = (char)(b + 32);
            if (a != b)
                break;
            h++;
            n++;
        }
        if (!*n)
            return 1;
    }
    return 0;
}

static const char *start_app_labels[START_APPS] = {
    "Terminal", "Files", "Notepad", "Images", "Disks", "Net Explorer",
    "Net Control", "Settings", "Agent", "Peak Runner", "Browser", "Monitor"
};

static const char *start_sys_labels[START_SYS] = {
    "Theme", "Help", "Alerts", "Save disk", "Lock", "Exit desktop", "Reboot", "Power off"
};

static int start_visible_rows;
static int start_visible_map[START_APPS + START_SYS];
static int start_scroll; /* first visible list row when menu is height-clamped */

static void start_rebuild_visible(void) {
    start_visible_rows = 0;
    for (int i = 0; i < START_APPS; i++) {
        if (str_icontains(start_app_labels[i], start_filter))
            start_visible_map[start_visible_rows++] = i;
    }
    for (int i = 0; i < START_SYS; i++) {
        if (str_icontains(start_sys_labels[i], start_filter))
            start_visible_map[start_visible_rows++] = START_APPS + i;
    }
    if (start_sel >= start_visible_rows)
        start_sel = start_visible_rows > 0 ? start_visible_rows - 1 : 0;
    if (start_sel < 0)
        start_sel = 0;
}

/* Layout above the taskbar. At UI scale 3 on 1080p the full list is taller than
 * the screen; without clamping, my underflows as uint32_t, the panel fill is
 * clipped away, row Y wraps into view, and click hit-tests never match. */
static void start_menu_layout(uint32_t *mx, uint32_t *my, uint32_t *mw, uint32_t *mh,
                              uint32_t *row_h, int *first_row, int *view_rows) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t margin = desktop_u(4);
    start_rebuild_visible();
    uint32_t row = fb_cell_h() + desktop_u(4);
    uint32_t rows = (uint32_t)(start_visible_rows > 0 ? start_visible_rows : 1);
    uint32_t want_h = START_SEARCH_H + desktop_u(8) + rows * row + desktop_u(12);
    uint32_t max_h = row * 3;
    if ((uint32_t)fb->height > th + margin + margin)
        max_h = (uint32_t)fb->height - th - margin - margin;
    *mw = desktop_u(220);
    *mh = want_h <= max_h ? want_h : max_h;
    *mx = desktop_u(8);
    *my = (uint32_t)fb->height - th - *mh - margin;
    if (*my >= (uint32_t)fb->height || *my + *mh > (uint32_t)fb->height - th)
        *my = margin;

    uint32_t chrome = START_SEARCH_H + desktop_u(8) + desktop_u(12);
    int fit = 1;
    if (*mh > chrome)
        fit = (int)((*mh - chrome) / row);
    if (fit < 1)
        fit = 1;
    if (start_visible_rows > 0 && fit > start_visible_rows)
        fit = start_visible_rows;

    if (start_sel < start_scroll)
        start_scroll = start_sel;
    if (start_sel >= start_scroll + fit)
        start_scroll = start_sel - fit + 1;
    if (start_scroll < 0)
        start_scroll = 0;
    if (start_visible_rows > 0 && start_scroll > start_visible_rows - fit)
        start_scroll = start_visible_rows - fit;
    if (start_scroll < 0)
        start_scroll = 0;

    *row_h = row;
    *first_row = start_scroll;
    *view_rows = start_visible_rows > 0 ? fit : 0;
}

static void start_menu_rect(uint32_t *mx, uint32_t *my, uint32_t *mw, uint32_t *mh) {
    uint32_t row_h;
    int first_row, view_rows;
    start_menu_layout(mx, my, mw, mh, &row_h, &first_row, &view_rows);
}

void desktop_draw_start_menu(void) {
    if (!menu_open)
        return;
    uint32_t mx, my, mw, mh, row;
    int first_row, view_rows;
    start_menu_layout(&mx, &my, &mw, &mh, &row, &first_row, &view_rows);
    fb_fill_rect(mx, my, mw, mh, desktop_color_surface());
    fb_fill_rect(mx, my, mw, desktop_u(2), desktop_color_accent());
    uint32_t cy = my + desktop_u(6);
    uint32_t search_bg = desktop_color_bg();
    fb_fill_rect(mx + desktop_u(8), cy, mw - desktop_u(16), START_SEARCH_H - desktop_u(2), search_bg);
    fb_fill_rect(mx + desktop_u(8), cy, mw - desktop_u(16), desktop_u(1), desktop_color_dim());
    if (start_filter[0])
        fb_draw_string(mx + desktop_u(14), cy + desktop_u(4), start_filter,
                       desktop_color_fg(), search_bg);
    else
        fb_draw_string(mx + desktop_u(14), cy + desktop_u(4), "Type to search…",
                       desktop_color_dim(), search_bg);
    cy += START_SEARCH_H + desktop_u(4);
    if (start_visible_rows == 0) {
        fb_draw_string(mx + desktop_u(12), cy, "No matches", desktop_color_dim(), desktop_color_surface());
        return;
    }
    for (int i = 0; i < view_rows; i++) {
        int vi = first_row + i;
        int row_idx = start_visible_map[vi];
        const char *label = row_idx < START_APPS ? start_app_labels[row_idx]
                                                 : start_sys_labels[row_idx - START_APPS];
        uint32_t bg = (vi == start_sel) ? desktop_color_accent() : desktop_color_surface();
        uint32_t fg = (vi == start_sel) ? desktop_color_bg() : desktop_color_fg();
        if (vi == start_sel)
            fb_fill_rect(mx + desktop_u(6), cy, mw - desktop_u(12), row - desktop_u(2), bg);
        fb_draw_string(mx + desktop_u(12), cy, label, fg, bg);
        cy += row;
    }
}

static void menus_damage_start(void) {
    uint32_t mx, my, mw, mh;
    start_menu_rect(&mx, &my, &mw, &mh);
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    damage_add(mx, my, mw, mh);
    damage_add(desktop_u(8), (uint32_t)fb->height - th, desktop_u(60), th);
}

static void menus_damage_ctx(void) {
    if (ctx_menu)
        ctx_menu_damage_rect(&ctx_spec);
}

static void ctx_close(void) {
    if (!ctx_menu)
        return;
    menus_damage_ctx();
    ctx_menu = 0;
    ctx_spec.hover_row = -1;
}

static void ctx_add_item(const char *label, int enabled, int separator, int action_id) {
    if (ctx_item_count >= CTX_MENU_MAX_ITEMS)
        return;
    ctx_items[ctx_item_count].label = label;
    ctx_items[ctx_item_count].enabled = enabled;
    ctx_items[ctx_item_count].separator = separator;
    ctx_items[ctx_item_count].action_id = action_id;
    ctx_item_count++;
}

static void ctx_build_desktop(void) {
    ctx_add_item("New Terminal", 1, 0, CTX_ACT_NEW_TERM);
    ctx_add_item("Files", 1, 0, CTX_ACT_NEW_FILES);
    ctx_add_item("Notepad", 1, 0, CTX_ACT_NEW_NOTEPAD);
    ctx_add_item("Images", 1, 0, CTX_ACT_NEW_IMAGES);
    ctx_add_item(NULL, 0, 1, CTX_ACT_NONE);
    ctx_add_item("Change wallpaper", 1, 0, CTX_ACT_CHANGE_WALLPAPER);
    ctx_add_item("Display settings", 1, 0, CTX_ACT_DISPLAY_SETTINGS);
}

static void ctx_build_taskbar(int win_idx) {
    if (win_idx < 0 || win_idx >= MAX_WINS || !wins[win_idx].open)
        return;
    ctx_add_item("Raise", 1, 0, CTX_ACT_RAISE);
    ctx_add_item(wins[win_idx].minimized ? "Restore" : "Minimize", 1, 0, CTX_ACT_MIN_RESTORE);
    ctx_add_item("Close", 1, 0, CTX_ACT_CLOSE);
}

static void ctx_build_chrome(int win_idx) {
    if (win_idx < 0 || win_idx >= MAX_WINS || !wins[win_idx].open)
        return;
    ctx_add_item("Minimize", 1, 0, CTX_ACT_MIN_RESTORE);
    ctx_add_item(wins[win_idx].maximized ? "Restore" : "Maximize", 1, 0, CTX_ACT_MAX_RESTORE);
    ctx_add_item("Close", 1, 0, CTX_ACT_CLOSE);
}

int desktop_app_ctx_menu(enum app_kind kind, struct ctx_menu_item *items, int max_items) {
    switch (kind) {
    case APP_TERM:
        return desktop_terminal_ctx_menu(items, max_items);
    case APP_BROWSER:
        return browser_ctx_menu(items, max_items);
    case APP_SETTINGS:
        if (!items || max_items < 4)
            return 0;
        items[0].label = "Display tab";
        items[0].enabled = 1;
        items[0].separator = 0;
        items[0].action_id = CTX_ACT_SETTINGS_DISPLAY;
        items[1].label = "Network tab";
        items[1].enabled = 1;
        items[1].separator = 0;
        items[1].action_id = CTX_ACT_SETTINGS_NET;
        items[2].label = NULL;
        items[2].enabled = 0;
        items[2].separator = 1;
        items[2].action_id = CTX_ACT_NONE;
        items[3].label = "Close window";
        items[3].enabled = 1;
        items[3].separator = 0;
        items[3].action_id = CTX_ACT_CLOSE;
        return 4;
    case APP_AGENT:
        if (!items || max_items < 3)
            return 0;
        items[0].label = "Agent help";
        items[0].enabled = 1;
        items[0].separator = 0;
        items[0].action_id = CTX_ACT_AGENT_HELP;
        items[1].label = NULL;
        items[1].enabled = 0;
        items[1].separator = 1;
        items[1].action_id = CTX_ACT_NONE;
        items[2].label = "Close window";
        items[2].enabled = 1;
        items[2].separator = 0;
        items[2].action_id = CTX_ACT_CLOSE;
        return 3;
    case APP_MONITOR:
        if (!items || max_items < 4)
            return 0;
        items[0].label = "Pause / resume";
        items[0].enabled = 1;
        items[0].separator = 0;
        items[0].action_id = CTX_ACT_MONITOR_PAUSE;
        items[1].label = "Export snapshot";
        items[1].enabled = 1;
        items[1].separator = 0;
        items[1].action_id = CTX_ACT_MONITOR_EXPORT;
        items[2].label = NULL;
        items[2].enabled = 0;
        items[2].separator = 1;
        items[2].action_id = CTX_ACT_NONE;
        items[3].label = "Close window";
        items[3].enabled = 1;
        items[3].separator = 0;
        items[3].action_id = CTX_ACT_CLOSE;
        return 4;
    case APP_FILES:
        return desktop_files_ctx_menu(items, max_items);
    case APP_NOTEPAD:
        return desktop_notepad_ctx_menu(items, max_items);
    case APP_IMAGES:
        return desktop_images_ctx_menu(items, max_items);
    case APP_DISKS:
        return desktop_disks_ctx_menu(items, max_items);
    case APP_NETEXP:
        return desktop_netexp_ctx_menu(items, max_items);
    case APP_NETCTL:
        return desktop_netctl_ctx_menu(items, max_items);
    default:
        break;
    }
    if (!items || max_items < 2)
        return 0;
    items[0].label = "Close window";
    items[0].enabled = 1;
    items[0].separator = 0;
    items[0].action_id = CTX_ACT_CLOSE;
    items[1].label = "Help";
    items[1].enabled = 1;
    items[1].separator = 0;
    items[1].action_id = CTX_ACT_HELP;
    return 2;
}

int desktop_app_ctx_action(enum app_kind kind, int action_id) {
    switch (kind) {
    case APP_TERM:
        return desktop_terminal_ctx_action(action_id);
    case APP_BROWSER:
        return browser_ctx_action(action_id);
    case APP_SETTINGS:
        if (action_id == CTX_ACT_SETTINGS_DISPLAY) {
            settings_page = 0;
            dirty_bits |= DIRTY_FULL;
            return 1;
        }
        if (action_id == CTX_ACT_SETTINGS_NET) {
            settings_page = 4;
            dirty_bits |= DIRTY_FULL;
            return 1;
        }
        return 0;
    case APP_AGENT:
        if (action_id == CTX_ACT_AGENT_HELP) {
            help_open = 1;
            dirty_bits |= DIRTY_FULL;
            return 1;
        }
        return 0;
    case APP_MONITOR:
        if (action_id == CTX_ACT_MONITOR_PAUSE) {
            monitor_toggle_pause();
            dirty_bits |= DIRTY_MONITOR;
            desktop_mark_win_surf_dirty(desktop_find_win(APP_MONITOR));
            return 1;
        }
        if (action_id == CTX_ACT_MONITOR_EXPORT) {
            if (sysmon_export(SYSMON_EXPORT_PATH) == 0)
                notify_push("Saved /tmp/sysmon.txt");
            else
                notify_push("Export failed");
            dirty_bits |= DIRTY_MONITOR;
            desktop_mark_win_surf_dirty(desktop_find_win(APP_MONITOR));
            return 1;
        }
        return 0;
    case APP_FILES:
        return desktop_files_ctx_action(action_id);
    case APP_NOTEPAD:
        return desktop_notepad_ctx_action(action_id);
    case APP_IMAGES:
        return desktop_images_ctx_action(action_id);
    case APP_DISKS:
        return desktop_disks_ctx_action(action_id);
    case APP_NETEXP:
        return desktop_netexp_ctx_action(action_id);
    case APP_NETCTL:
        return desktop_netctl_ctx_action(action_id);
    default:
        return 0;
    }
}

static void ctx_build_client(int win_idx, int32_t mx, int32_t my) {
    if (win_idx < 0 || win_idx >= MAX_WINS || !wins[win_idx].open)
        return;
    if (wins[win_idx].kind == APP_TERM)
        desktop_term_activate(win_idx);
    if (wins[win_idx].kind == APP_FILES)
        desktop_files_ctx_prepare(&wins[win_idx], mx, my);
    if (wins[win_idx].kind == APP_NETCTL)
        desktop_netctl_ctx_prepare(&wins[win_idx], mx, my);
    int n = desktop_app_ctx_menu(wins[win_idx].kind, ctx_items, CTX_MENU_MAX_ITEMS);
    ctx_item_count = n;
}

static void ctx_build_spec(enum ctx_target target, int win_idx, int32_t mx, int32_t my) {
    ctx_item_count = 0;
    ctx_target_kind = target;
    ctx_win = win_idx;
    switch (target) {
    case CTX_DESKTOP:
        ctx_build_desktop();
        break;
    case CTX_TASKBAR:
        ctx_build_taskbar(win_idx);
        break;
    case CTX_CHROME:
        ctx_build_chrome(win_idx);
        break;
    case CTX_CLIENT:
        ctx_build_client(win_idx, mx, my);
        break;
    }
    ctx_spec.items = ctx_items;
    ctx_spec.count = ctx_item_count;
    ctx_spec.hover_row = -1;
}

static void ctx_open_at(int32_t mx, int32_t my, enum ctx_target target, int win_idx) {
    if (ctx_menu)
        menus_damage_ctx();
    ctx_build_spec(target, win_idx, mx, my);
    ctx_spec.x = mx;
    ctx_spec.y = my;
    struct framebuffer *fb = fb_get();
    ctx_menu_clamp(&ctx_spec, (uint32_t)fb->width, (uint32_t)fb->height, desktop_u);
    ctx_menu = 1;
    if (menu_open) {
        menu_open = 0;
        menus_damage_start();
    }
    menus_damage_ctx();
    dirty_bits |= DIRTY_MOVE;
}

static int hit_win_at(int32_t mx, int32_t my, int *win_idx, int *in_client) {
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
        if (!desktop_point_in(mx, my, w->x, w->y, w->w, w->h))
            continue;
        *win_idx = i;
        if (in_client) {
            uint32_t th = desktop_title_h();
            *in_client = desktop_point_in(mx, my, w->x, w->y + th, w->w, w->h - th) ? 1 : 0;
        }
        return 1;
    }
    return 0;
}

int desktop_menus_ctx_hit_test(int32_t mx, int32_t my, enum ctx_target *target, int *win_idx) {
    struct framebuffer *fb = fb_get();
    uint32_t th = desktop_taskbar_h();
    uint32_t ty = (uint32_t)fb->height - th;
    int wi = -1;
    int overflow = 0;

    if (desktop_taskbar_hit_button(mx, my, &wi, &overflow) && !overflow && wi >= 0) {
        if (target)
            *target = CTX_TASKBAR;
        if (win_idx)
            *win_idx = wi;
        return 1;
    }
    if (desktop_point_in(mx, my, 0, ty, (uint32_t)fb->width, th)) {
        return 0;
    }
    int in_client = 0;
    if (hit_win_at(mx, my, &wi, &in_client)) {
        if (target)
            *target = in_client ? CTX_CLIENT : CTX_CHROME;
        if (win_idx)
            *win_idx = wi;
        return 1;
    }
    if (target)
        *target = CTX_DESKTOP;
    if (win_idx)
        *win_idx = -1;
    return 1;
}

void desktop_menus_open_ctx_target(int32_t mx, int32_t my, enum ctx_target target, int win_idx) {
    ctx_open_at(mx, my, target, win_idx);
}


void desktop_menus_start_hover(int32_t mx, int32_t my) {
    if (!menu_open)
        return;
    uint32_t mx0, my0, mw, mh, row_h;
    int first_row, view_rows;
    start_menu_layout(&mx0, &my0, &mw, &mh, &row_h, &first_row, &view_rows);
    if (!desktop_point_in(mx, my, mx0, my0, mw, mh))
        return;
    uint32_t cy = my0 + desktop_u(6) + START_SEARCH_H + desktop_u(4);
    if ((uint32_t)my < cy || view_rows <= 0 || start_visible_rows <= 0)
        return;
    int i = (int)(((uint32_t)my - cy) / row_h);
    if (i < 0 || i >= view_rows)
        return;
    int vi = first_row + i;
    if (vi < 0 || vi >= start_visible_rows || vi == start_sel)
        return;
    start_sel = vi;
    menus_damage_start();
    dirty_bits |= DIRTY_MOVE;
}

int desktop_menus_start_wheel(int32_t mx, int32_t my, int wheel) {
    if (!menu_open || !wheel)
        return 0;
    uint32_t mx0, my0, mw, mh, row_h;
    int first_row, view_rows;
    start_menu_layout(&mx0, &my0, &mw, &mh, &row_h, &first_row, &view_rows);
    if (!desktop_point_in(mx, my, mx0, my0, mw, mh))
        return 0;
    uint32_t chrome = START_SEARCH_H + desktop_u(8) + desktop_u(12);
    int fit = 1;
    if (mh > chrome)
        fit = (int)((mh - chrome) / row_h);
    if (fit < 1)
        fit = 1;
    if (start_visible_rows > 0 && fit > start_visible_rows)
        fit = start_visible_rows;
    if (start_visible_rows <= fit)
        return 0;
    int old = start_scroll;
    if (wheel > 0) {
        if (start_scroll > 0)
            start_scroll--;
    } else if (start_scroll < start_visible_rows - fit) {
        start_scroll++;
    }
    if (start_scroll == old)
        return 0;
    menus_damage_start();
    dirty_bits |= DIRTY_MOVE;
    return 1;
}

void desktop_menus_ctx_hover(int32_t mx, int32_t my) {
    if (!ctx_menu)
        return;
    ctx_menu_update_hover(&ctx_spec, mx, my, desktop_u);
}

void desktop_draw_ctx_menu(void) {
    if (!ctx_menu)
        return;
    uint32_t bg = desktop_color_surface();
    if (wallpaper_enabled() && !strcmp(theme_name(), "paper"))
        bg = desktop_color_title();
    ctx_menu_draw(&ctx_spec, desktop_u, desktop_color_fg(), bg,
                  desktop_color_accent(), desktop_color_dim());
}

static void ctx_dispatch_action(int action_id) {
    switch (action_id) {
    case CTX_ACT_NEW_TERM:
        desktop_open_app(APP_TERM);
        break;
    case CTX_ACT_NEW_FILES:
        desktop_open_app(APP_FILES);
        break;
    case CTX_ACT_NEW_NOTEPAD:
        desktop_open_app(APP_NOTEPAD);
        break;
    case CTX_ACT_NEW_IMAGES:
        desktop_open_app(APP_IMAGES);
        break;
    case CTX_ACT_CHANGE_WALLPAPER:
        wallpaper_next();
        wallpaper_persist();
        dirty_bits |= DIRTY_FULL;
        break;
    case CTX_ACT_DISPLAY_SETTINGS:
        desktop_open_app(APP_SETTINGS);
        settings_page = 0;
        break;
    case CTX_ACT_RAISE:
        if (ctx_win >= 0) {
            wins[ctx_win].minimized = 0;
            desktop_raise_win(ctx_win);
        }
        break;
    case CTX_ACT_MIN_RESTORE:
        if (ctx_win >= 0) {
            if (ctx_target_kind == CTX_TASKBAR && wins[ctx_win].minimized)
                wins[ctx_win].minimized = 0;
            else if (wins[ctx_win].minimized)
                wins[ctx_win].minimized = 0;
            else
                desktop_minimize_win(ctx_win);
            if (ctx_target_kind == CTX_TASKBAR && !wins[ctx_win].minimized)
                desktop_raise_win(ctx_win);
        }
        break;
    case CTX_ACT_MAX_RESTORE:
        if (ctx_win >= 0)
            desktop_maximize_win(ctx_win);
        break;
    case CTX_ACT_CLOSE:
        if (ctx_win >= 0)
            desktop_close_win(ctx_win);
        break;
    case CTX_ACT_HELP:
        help_open = 1;
        dirty_bits |= DIRTY_FULL;
        break;
    default:
        if (action_id >= CTX_ACT_APP_BASE && ctx_win >= 0 &&
            desktop_app_ctx_action(wins[ctx_win].kind, action_id))
            break;
        break;
    }
}

int desktop_ctx_menu_click(int32_t mx, int32_t my) {
    if (!ctx_menu)
        return 0;
    int item = ctx_menu_hit_row(&ctx_spec, mx, my, desktop_u);
    if (item >= 0 && item < ctx_item_count && ctx_items[item].enabled)
        ctx_dispatch_action(ctx_items[item].action_id);
    ctx_close();
    dirty_bits |= DIRTY_MOVE;
    return 1;
}

static int start_row_action(int row) {
    if (row < 0)
        return -1;
    if (row < START_APPS) {
        enum app_kind kinds[START_APPS] = {
            APP_TERM, APP_FILES, APP_NOTEPAD, APP_IMAGES, APP_DISKS, APP_NETEXP,
            APP_NETCTL, APP_SETTINGS, APP_AGENT, APP_GAME, APP_BROWSER, APP_MONITOR
        };
        enum app_kind k = kinds[row];
        if (!app_kind_ready(k)) {
            notify_push("App not ready yet");
            dirty_bits |= DIRTY_TOAST;
            return 0;
        }
        desktop_open_app(k);
        return 0;
    }
    row -= START_APPS;
    if (row == 0) {
        theme_next();
        theme_persist();
        dirty_bits |= DIRTY_FULL;
    } else if (row == 1) {
        help_open = 1;
        dirty_bits |= DIRTY_FULL;
    } else if (row == 2) {
        notify_hist_open = 1;
        dirty_bits |= DIRTY_FULL;
    } else if (row == 3) {
        if (privacy_persist_profile() <= 0) {
            notify_push("Enable Privacy → workspace persist first");
        } else if (peakdisk_save_async() == 0) {
            notify_push("Saving workspace to disk…");
        } else {
            const char *why = peakdisk_last_error();
            if (why && why[0]) {
                char msg[72];
                snprintf(msg, sizeof(msg), "Save failed: %s", why);
                notify_push(msg);
            } else {
                notify_push("Save failed");
            }
        }
        dirty_bits |= DIRTY_TOAST;
    } else if (row == 4) {
        session_lock = 1;
        dirty_bits |= DIRTY_FULL;
    } else if (row == 5) {
        desktop_should_exit = 1;
    } else if (row == 6) {
        power_confirm = 2;
        dirty_bits |= DIRTY_FULL;
    } else if (row == 7) {
        power_confirm = 1;
        dirty_bits |= DIRTY_FULL;
    }
    return 0;
}

void desktop_menu_click(int32_t mx, int32_t my) {
    uint32_t mx0, my0, mw, mh, row_h;
    int first_row, view_rows;
    start_menu_layout(&mx0, &my0, &mw, &mh, &row_h, &first_row, &view_rows);
    if (!desktop_point_in(mx, my, mx0, my0, mw, mh)) {
        menu_open = 0;
        start_filter[0] = 0;
        start_sel = 0;
        start_scroll = 0;
        menus_damage_start();
        dirty_bits |= DIRTY_MOVE;
        return;
    }
    uint32_t cy = my0 + desktop_u(6) + START_SEARCH_H + desktop_u(4);
    if ((uint32_t)my >= cy && view_rows > 0) {
        int i = (int)(((uint32_t)my - cy) / row_h);
        if (i >= 0 && i < view_rows) {
            int vi = first_row + i;
            int row = start_visible_map[vi];
            menu_open = 0;
            start_filter[0] = 0;
            start_sel = 0;
            start_scroll = 0;
            menus_damage_start();
            start_row_action(row);
            dirty_bits |= DIRTY_MOVE;
        }
    }
}

int desktop_menus_start_key(int key) {
    if (!menu_open)
        return 0;
    if (key == '\n') {
        if (start_visible_rows > 0) {
            int row = start_visible_map[start_sel];
            menu_open = 0;
            start_filter[0] = 0;
            start_sel = 0;
            start_scroll = 0;
            menus_damage_start();
            start_row_action(row);
            dirty_bits |= DIRTY_MOVE;
        }
        return 1;
    }
    if (key == 127 || key == '\b') {
        size_t len = strlen(start_filter);
        if (len > 0) {
            start_filter[len - 1] = 0;
            start_sel = 0;
            start_scroll = 0;
            menus_damage_start();
            dirty_bits |= DIRTY_MOVE;
        }
        return 1;
    }
    if (key == KEY_UP) {
        if (start_sel > 0)
            start_sel--;
        menus_damage_start();
        dirty_bits |= DIRTY_MOVE;
        return 1;
    }
    if (key == KEY_DOWN) {
        start_rebuild_visible();
        if (start_visible_rows > 0 && start_sel < start_visible_rows - 1)
            start_sel++;
        menus_damage_start();
        dirty_bits |= DIRTY_MOVE;
        return 1;
    }
    if (key >= 32 && key < 127) {
        size_t len = strlen(start_filter);
        if (len + 1 < sizeof(start_filter)) {
            start_filter[len] = (char)key;
            start_filter[len + 1] = 0;
            start_sel = 0;
            start_scroll = 0;
            menus_damage_start();
            dirty_bits |= DIRTY_MOVE;
        }
        return 1;
    }
    return 0;
}

int desktop_menus_toggle_start(int32_t mx, int32_t my, uint32_t taskbar_y, uint32_t taskbar_h) {
    if (!desktop_point_in(mx, my, desktop_u(8), taskbar_y, desktop_u(60), taskbar_h))
        return 0;
    menus_damage_start();
    menu_open = !menu_open;
    if (menu_open) {
        start_filter[0] = 0;
        start_sel = 0;
        start_scroll = 0;
    }
    dirty_bits |= DIRTY_MOVE;
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
    start_filter[0] = 0;
    start_sel = 0;
    start_scroll = 0;
    dirty_bits |= DIRTY_MOVE;
    return 1;
}
