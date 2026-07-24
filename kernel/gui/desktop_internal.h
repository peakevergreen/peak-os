#ifndef PEAK_DESKTOP_INTERNAL_H
#define PEAK_DESKTOP_INTERNAL_H

#include "types.h"
#include "surface.h"

#define TERM_COLS 64
#define TERM_ROWS 200
#define TERM_VIEW 28
#define CURSOR_MAX 64
#define FILES_ROWS 24
#define MAX_WINS 12
#define SETTINGS_PAGES 4
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
};

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
void desktop_menus_open_ctx(int32_t mx, int32_t my);
int desktop_menus_toggle_start(int32_t mx, int32_t my, uint32_t taskbar_y, uint32_t taskbar_h);
int desktop_menus_close_popups(void);

void desktop_overlays_idle_lock(uint64_t last_input_tick);
int desktop_overlays_block_input(int key);
void desktop_alttab_advance(void);
void desktop_alttab_commit_if_open(void);
int desktop_overlays_close_popups(void);
int desktop_help_click_dismiss(void);

void desktop_term_reset_slot(int slot);
void desktop_term_activate(int slot);
int desktop_active_term_index(void);
void desktop_terminal_init(void);
void desktop_terminal_draw(struct win *w);
int desktop_terminal_key(int key);
void desktop_terminal_wheel(int wheel);

void desktop_files_init(void);
void desktop_files_draw(struct win *w);
int desktop_files_key(int key);
void desktop_files_wheel(int wheel);
int desktop_files_click(struct win *w, int32_t mx, int32_t my, int dbl);

void desktop_settings_draw(struct win *w);
int desktop_settings_click(struct win *w, int32_t mx, int32_t my);

void desktop_agent_init(void);
void desktop_app_opened(enum app_kind k);
void desktop_agent_draw(struct win *w);
int desktop_agent_key(int key);
int desktop_agent_click(void);

void desktop_compose_reset_cursor_cache(void);

extern int menu_open;
extern int ctx_menu;
extern int32_t ctx_x, ctx_y;
extern int settings_page;
extern int alttab_open;
extern int alttab_sel;
extern int help_open;
extern int session_lock;
extern int power_confirm;
extern int desktop_should_exit;

#endif
