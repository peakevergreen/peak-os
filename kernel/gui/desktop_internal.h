#ifndef PEAK_DESKTOP_INTERNAL_H
#define PEAK_DESKTOP_INTERNAL_H

#include "types.h"
#include "surface.h"
#include "ui_widgets.h"

#define TERM_COLS 64
#define TERM_ROWS 512
#define TERM_VIEW 32
#define TERM_FIND_MAX 31
#define CURSOR_MAX 64
#define FILES_ROWS 24
#define MAX_WINS 16
#define SETTINGS_PAGES 5
#define MOVE_PIX_CAP (1920u * 1200u)
#define MAX_DAMAGE 16

enum app_kind {
    APP_TERM = 0,
    APP_FILES = 1,
    APP_SETTINGS = 2,
    APP_AGENT = 3,
    APP_GAME = 4,
    APP_BROWSER = 5,
    APP_MONITOR = 6,
    APP_NOTEPAD = 7,
    APP_IMAGES = 8,
    APP_DISKS = 9,
    APP_NETEXP = 10,
    APP_NETCTL = 11,
};

enum ctx_target {
    CTX_DESKTOP = 0,
    CTX_TASKBAR = 1,
    CTX_CHROME = 2,
    CTX_CLIENT = 3,
};

#define CTX_MENU_MAX_ITEMS 16

#define CTX_ACT_NONE             0
#define CTX_ACT_NEW_TERM         1
#define CTX_ACT_NEW_FILES        2
#define CTX_ACT_NEW_NOTEPAD      3
#define CTX_ACT_NEW_IMAGES       4
#define CTX_ACT_CHANGE_WALLPAPER 5
#define CTX_ACT_DISPLAY_SETTINGS 6
#define CTX_ACT_RAISE            10
#define CTX_ACT_MIN_RESTORE      11
#define CTX_ACT_CLOSE            12
#define CTX_ACT_MAX_RESTORE      13
#define CTX_ACT_HELP             14
#define CTX_ACT_APP_BASE         100
#define CTX_ACT_FILES_NEW        (CTX_ACT_APP_BASE + 0)
#define CTX_ACT_FILES_GO_UP      (CTX_ACT_APP_BASE + 1)
#define CTX_ACT_FILES_COPY_PATH  (CTX_ACT_APP_BASE + 2)
#define CTX_ACT_FILES_OPEN       (CTX_ACT_APP_BASE + 3)
#define CTX_ACT_FILES_DELETE     (CTX_ACT_APP_BASE + 4)
#define CTX_ACT_FILES_RENAME     (CTX_ACT_APP_BASE + 5)
#define CTX_ACT_FILES_OPEN_NPAD  (CTX_ACT_APP_BASE + 6)
#define CTX_ACT_FILES_OPEN_IMG   (CTX_ACT_APP_BASE + 7)
#define CTX_ACT_FILES_OPEN_DISK  (CTX_ACT_APP_BASE + 8)
#define CTX_ACT_FILES_CUT        (CTX_ACT_APP_BASE + 9)
#define CTX_ACT_FILES_PASTE      (CTX_ACT_APP_BASE + 10)
#define CTX_ACT_NPAD_CUT         (CTX_ACT_APP_BASE + 20)
#define CTX_ACT_NPAD_COPY        (CTX_ACT_APP_BASE + 21)
#define CTX_ACT_NPAD_PASTE       (CTX_ACT_APP_BASE + 22)
#define CTX_ACT_NPAD_SAVE        (CTX_ACT_APP_BASE + 23)
#define CTX_ACT_NPAD_SAVEAS      (CTX_ACT_APP_BASE + 24)
#define CTX_ACT_NPAD_FIND        (CTX_ACT_APP_BASE + 25)
#define CTX_ACT_IMG_FIT          (CTX_ACT_APP_BASE + 40)
#define CTX_ACT_IMG_ACTUAL       (CTX_ACT_APP_BASE + 41)
#define CTX_ACT_IMG_NEXT         (CTX_ACT_APP_BASE + 42)
#define CTX_ACT_IMG_PREV         (CTX_ACT_APP_BASE + 43)
#define CTX_ACT_IMG_OPEN_DIR     (CTX_ACT_APP_BASE + 44)
#define CTX_ACT_IMG_COPY_PATH    (CTX_ACT_APP_BASE + 45)
#define CTX_ACT_DISK_REFRESH     (CTX_ACT_APP_BASE + 60)
#define CTX_ACT_DISK_SAVE        (CTX_ACT_APP_BASE + 61)
#define CTX_ACT_DISK_PROPS       (CTX_ACT_APP_BASE + 62)
#define CTX_ACT_NX_COPY_IP       (CTX_ACT_APP_BASE + 70)
#define CTX_ACT_NX_PING          (CTX_ACT_APP_BASE + 71)
#define CTX_ACT_NX_NSLOOKUP      (CTX_ACT_APP_BASE + 72)
#define CTX_ACT_NX_NETCTL        (CTX_ACT_APP_BASE + 73)
#define CTX_ACT_NX_COPY_RESOLVED (CTX_ACT_APP_BASE + 74)
#define CTX_ACT_NC_ALLOW         (CTX_ACT_APP_BASE + 80)
#define CTX_ACT_NC_REVOKE        (CTX_ACT_APP_BASE + 81)
#define CTX_ACT_NC_KILLSW        (CTX_ACT_APP_BASE + 82)
#define CTX_ACT_NC_PERSIST       (CTX_ACT_APP_BASE + 83)
#define CTX_ACT_NC_DHCP          (CTX_ACT_APP_BASE + 84)
#define CTX_ACT_NC_COPY_IP       (CTX_ACT_APP_BASE + 85)
#define CTX_ACT_NC_SETTINGS      (CTX_ACT_APP_BASE + 86)
#define CTX_ACT_TERM_NEW         (CTX_ACT_APP_BASE + 0)
#define CTX_ACT_TERM_COPY        (CTX_ACT_APP_BASE + 1)
#define CTX_ACT_TERM_PASTE       (CTX_ACT_APP_BASE + 2)
#define CTX_ACT_TERM_CLEAR       (CTX_ACT_APP_BASE + 3)
#define CTX_ACT_TERM_FIND        (CTX_ACT_APP_BASE + 4)
#define CTX_ACT_TERM_COPYSEL     (CTX_ACT_APP_BASE + 5)
#define CTX_ACT_BROWSER_BACK     (CTX_ACT_APP_BASE + 10)
#define CTX_ACT_BROWSER_RELOAD   (CTX_ACT_APP_BASE + 11)
#define CTX_ACT_BROWSER_COPY_URL (CTX_ACT_APP_BASE + 12)
#define CTX_ACT_BROWSER_NEW_TAB  (CTX_ACT_APP_BASE + 13)
#define CTX_ACT_BROWSER_FORWARD  (CTX_ACT_APP_BASE + 14)
#define CTX_ACT_BROWSER_BOOKMARK (CTX_ACT_APP_BASE + 15)
#define CTX_ACT_BROWSER_BM_BASE    (CTX_ACT_APP_BASE + 16)
#define CTX_ACT_BROWSER_RESTORE    (CTX_ACT_APP_BASE + 22)
#define CTX_ACT_SETTINGS_DISPLAY   (CTX_ACT_APP_BASE + 20)
#define CTX_ACT_SETTINGS_NET     (CTX_ACT_APP_BASE + 21)
#define CTX_ACT_AGENT_HELP       (CTX_ACT_APP_BASE + 30)
#define CTX_ACT_MONITOR_PAUSE    (CTX_ACT_APP_BASE + 40)
#define CTX_ACT_MONITOR_EXPORT   (CTX_ACT_APP_BASE + 41)

struct win {
    int open;
    int minimized;
    int maximized;
    enum app_kind kind;
    uint32_t x, y, w, h;
    uint32_t rx, ry, rw, rh;
    int z;
    struct win_surface surf;
};

struct damage_rect {
    uint32_t x, y, w, h;
};

#define DIRTY_FULL    1
#define DIRTY_TERM    2
#define DIRTY_CLOCK   4
#define DIRTY_MONITOR 8
#define DIRTY_GAME    16
#define DIRTY_TOAST   32
#define DIRTY_BROWSER 64
#define DIRTY_WIN     128
#define DIRTY_MOVE    256

extern struct win wins[MAX_WINS];
extern int focus;
extern int dragging;
extern int resizing;
extern int resize_edge;
extern int32_t drag_off_x, drag_off_y;
extern uint32_t resize_orig_w, resize_orig_h;
extern uint32_t resize_orig_x, resize_orig_y;
extern int32_t resize_origin_x, resize_origin_y;
extern uint32_t move_prev_x, move_prev_y, move_prev_w, move_prev_h;
extern int move_prev_valid;
extern uint32_t *move_pixmap, *move_underlay;
extern uint32_t move_pw, move_ph;
extern int move_live;
extern uint32_t band_x, band_y, band_w, band_h;
extern int band_live;
extern int snap_live;

extern struct damage_rect damage_list[MAX_DAMAGE];
extern int damage_count;

extern int dirty_bits;
extern int scene_ready;
extern int32_t cursor_mx, cursor_my;
extern uint64_t last_clock_secs;

void damage_clear(void);
void damage_add(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void damage_add_win(int idx);
void damage_merge_all(void);

uint32_t desktop_u(uint32_t v);
uint32_t desktop_taskbar_h(void);
uint32_t desktop_title_h(void);
uint32_t desktop_color_bg(void);
uint32_t desktop_color_fg(void);
uint32_t desktop_color_dim(void);
uint32_t desktop_color_accent(void);
uint32_t desktop_color_surface(void);
uint32_t desktop_color_title(void);
uint32_t desktop_color_border(void);

int desktop_point_in(int32_t px, int32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
uint32_t desktop_win_min_w(void);
uint32_t desktop_win_min_h(void);
int desktop_hit_resize_edge(struct win *w, int32_t mx, int32_t my);
int desktop_hit_resize_grip(struct win *w, int32_t mx, int32_t my);
void desktop_clamp_win_geom(struct win *w);
void desktop_rescale_windows(void);
const char *desktop_app_title(enum app_kind k);
int desktop_find_win(enum app_kind k);
void desktop_raise_win(int idx);
void desktop_maximize_win(int idx);
void desktop_minimize_win(int idx);
int desktop_open_app(enum app_kind k);
void desktop_close_win(int idx);

void desktop_mark_focus_surf_dirty(void);
void desktop_mark_win_surf_dirty(int idx);
void desktop_mark_win_surf_dirty_rect(int idx, uint32_t x, uint32_t y,
                                      uint32_t w, uint32_t h);
void desktop_cursor_erase_front(void);
void desktop_draw_cursor(int32_t x, int32_t y);
void desktop_opaque_move_free(void);
void desktop_opaque_move_begin(int idx);
void desktop_opaque_move_end(void);

void desktop_draw_win_content(int i);
void desktop_draw_taskbar(void);
void desktop_draw_start_menu(void);
void desktop_draw_ctx_menu(void);
int desktop_app_ctx_menu(enum app_kind kind, struct ctx_menu_item *items, int max_items);
int desktop_app_ctx_action(enum app_kind kind, int action_id);
void desktop_menus_open_ctx_target(int32_t mx, int32_t my, enum ctx_target target, int win_idx);
int desktop_menus_ctx_hit_test(int32_t mx, int32_t my, enum ctx_target *target, int *win_idx);
void desktop_menus_ctx_hover(int32_t mx, int32_t my);
int desktop_taskbar_hit_button(int32_t mx, int32_t my, int *win_idx, int *overflow_btn);
int desktop_taskbar_raise_overflow(void);
int desktop_taskbar_map_win(int slot, int *win_idx);
int desktop_taskbar_visible_slots(void);
void desktop_draw_alttab(void);
void desktop_draw_help(void);
void desktop_draw_session_overlays(void);
void desktop_draw_desktop_bg(void);
void desktop_clock_rect(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
void desktop_draw_clock_area(void);

uint32_t desktop_taskbar_btn_w(void);
void desktop_login(void);
void desktop_menu_click(int32_t mx, int32_t my);
int desktop_ctx_menu_click(int32_t mx, int32_t my);
int desktop_menus_toggle_start(int32_t mx, int32_t my, uint32_t taskbar_y, uint32_t taskbar_h);
int desktop_menus_start_key(int key);
int desktop_menus_close_popups(void);
int desktop_snap_hint(int32_t mx, int32_t my);
void desktop_snap_zone_rect(int mode, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
void desktop_snap_apply(int idx, int mode);

void desktop_overlays_idle_lock(uint64_t last_input_tick);
int desktop_overlays_block_input(int key);
void desktop_alttab_advance(void);
void desktop_alttab_commit_if_open(void);
int desktop_overlays_close_popups(void);
int desktop_help_click_dismiss(void);
int desktop_notify_click_dismiss(int32_t mx, int32_t my);

void desktop_term_reset_slot(int slot);
void desktop_term_activate(int slot);
int desktop_active_term_index(void);
void desktop_terminal_init(void);
void desktop_terminal_draw(struct win *w);
int desktop_terminal_key(int key);
void desktop_terminal_wheel(int wheel);
int desktop_terminal_click(struct win *w, int32_t mx, int32_t my, int drag);
void desktop_terminal_clear(void);
void desktop_terminal_copy(void);
void desktop_terminal_paste(void);
int desktop_terminal_ctx_menu(struct ctx_menu_item *items, int max_items);
int desktop_terminal_ctx_action(int action_id);
int desktop_terminal_find_active(void);
void desktop_terminal_find_close(void);
void desktop_terminal_select_end(void);

void desktop_files_init(void);
void desktop_files_goto(const char *dir, const char *select_name);
void desktop_files_draw(struct win *w);
int desktop_files_key(int key);
void desktop_files_wheel(int wheel);
int desktop_files_click(struct win *w, int32_t mx, int32_t my, int dbl);
void desktop_files_ctx_prepare(struct win *w, int32_t mx, int32_t my);
int desktop_files_ctx_menu(struct ctx_menu_item *items, int max_items);
int desktop_files_ctx_action(int action_id);
void desktop_notepad_init(void);
void desktop_notepad_open(const char *path);
void desktop_notepad_draw(struct win *w);
int desktop_notepad_key(int key);
int desktop_notepad_click(struct win *w, int32_t mx, int32_t my);
int desktop_notepad_ctx_menu(struct ctx_menu_item *items, int max_items);
int desktop_notepad_ctx_action(int action_id);

void desktop_images_init(void);
void desktop_images_open(const char *path);
void desktop_images_draw(struct win *w);
int desktop_images_key(int key);
void desktop_images_wheel(int wheel);
int desktop_images_click(struct win *w, int32_t mx, int32_t my);
void desktop_images_drag(int32_t mx, int32_t my);
void desktop_images_release(void);
int desktop_images_ctx_menu(struct ctx_menu_item *items, int max_items);
int desktop_images_ctx_action(int action_id);

void desktop_disks_init(void);
void desktop_disks_show(void);
void desktop_disks_draw(struct win *w);
void desktop_disks_tick(void);
int desktop_disks_key(int key);
int desktop_disks_ctx_menu(struct ctx_menu_item *items, int max_items);
int desktop_disks_ctx_action(int action_id);

void desktop_netexp_init(void);
void desktop_netexp_draw(struct win *w);
int desktop_netexp_key(int key);
int desktop_netexp_ctx_menu(struct ctx_menu_item *items, int max_items);
int desktop_netexp_ctx_action(int action_id);

void desktop_netctl_init(void);
void desktop_netctl_draw(struct win *w);
int desktop_netctl_key(int key);
int desktop_netctl_click(struct win *w, int32_t mx, int32_t my);
void desktop_netctl_ctx_prepare(struct win *w, int32_t mx, int32_t my);
int desktop_netctl_ctx_menu(struct ctx_menu_item *items, int max_items);
int desktop_netctl_ctx_action(int action_id);

void desktop_settings_init(void);
void desktop_settings_draw(struct win *w);
int desktop_settings_click(struct win *w, int32_t mx, int32_t my);
int desktop_settings_key(int key);

void desktop_agent_init(void);
void desktop_app_opened(enum app_kind k);
void desktop_agent_draw(struct win *w);
int desktop_agent_key(int key);
int desktop_agent_click(void);

void desktop_compose_reset_cursor_cache(void);

extern int menu_open;
extern char start_filter[24];
extern int start_sel;
extern int ctx_menu;
extern enum ctx_target ctx_target_kind;
extern int ctx_win;
extern struct ctx_menu_spec ctx_spec;
extern struct ctx_menu_item ctx_items[CTX_MENU_MAX_ITEMS];
extern int ctx_item_count;
extern int settings_page;
extern int alttab_open;
extern int alttab_sel;
extern int help_open;
extern int session_lock;
extern int power_confirm;
extern int desktop_should_exit;

#endif
