#include "gui.h"
#include "console.h"
#include "fb.h"
#include "display.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"
#include "timer.h"
#include "util.h"
#include "agent.h"
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

#define TERM_COLS 64
#define TERM_ROWS 200
#define TERM_VIEW 28
#define CURSOR_MAX 64
#define FILES_ROWS 24
#define MAX_WINS 12
#define SETTINGS_PAGES 4

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
    uint32_t rx, ry, rw, rh; /* restore geom */
    int z;
    struct win_surface surf;
};

struct term_state {
    char lines[TERM_ROWS][TERM_COLS + 1];
    uint32_t row, col;
    int scroll;
    int sel_a, sel_b;
    uint32_t caret_col;
    int inited;
    int full_redraw;
    int cell_dirty;
    uint32_t dirty_col, dirty_row;
    uint32_t prev_caret_col;
};

static struct win wins[MAX_WINS];
static struct term_state terms[MAX_WINS];
static int active_term; /* win index for shell/console I/O */
static int focus; /* index into wins */
static int dragging;
static int resizing;
static int resize_edge; /* bit0=L bit1=R bit2=T bit3=B */
static int32_t drag_off_x, drag_off_y;
static uint32_t resize_orig_w, resize_orig_h;
static uint32_t resize_orig_x, resize_orig_y;
static int32_t resize_origin_x, resize_origin_y;
/* Last *presented* drag/resize geometry — damage must use this, not the
 * previous mouse sample, or skipped frames leave window ghosts. */
static uint32_t move_prev_x, move_prev_y, move_prev_w, move_prev_h;
static int move_prev_valid;
/* Opaque move (Phase 1): pixmap of window + underlay for restore. */
static uint32_t *move_pixmap, *move_underlay;
static uint32_t move_pw, move_ph;
static int move_live;
#define MOVE_PIX_CAP (1920u * 1200u)
/* Rubber-band resize preview (Phase 4b) — win geom stays until release. */
static uint32_t band_x, band_y, band_w, band_h;
static int band_live;
static uint32_t cursor_backup[CURSOR_MAX][CURSOR_MAX];
static int cursor_saved;
static int32_t last_cx, last_cy;
static uint32_t last_csize;
/* Soft cursor sprite cache (scale → ARGB, 0 = transparent). */
static uint32_t cursor_sprite[CURSOR_MAX * CURSOR_MAX];
static uint32_t cursor_sprite_scale;
static uint32_t cursor_sprite_size;
static int menu_open;
static int ctx_menu; /* 0 off, 1 desktop */
static int32_t ctx_x, ctx_y;
static int settings_page;
static int alttab_open;
static int alttab_sel;
static int help_open;
static int login_done;
static int session_lock;
static int power_confirm; /* 0 off, 1 shutdown, 2 reboot */

static char files_cwd[VFS_PATH_MAX] = "/home/dev/workspace";
static int files_sel;
static int files_scroll;
static char agent_input[96];
static uint32_t agent_input_len;

/* dirty bits: avoid full-screen blink on every keystroke / clock tick */
#define DIRTY_FULL    1
#define DIRTY_TERM    2
#define DIRTY_CLOCK   4
#define DIRTY_MONITOR 8
#define DIRTY_GAME    16
#define DIRTY_TOAST   32
#define DIRTY_BROWSER 64
#define DIRTY_WIN     128 /* files / settings / agent content */
#define DIRTY_MOVE    256 /* drag/resize: present damage only */

#define MAX_DAMAGE 16
struct damage_rect {
    uint32_t x, y, w, h;
};
static struct damage_rect damage_list[MAX_DAMAGE];
static int damage_count;
static int damage_overflow; /* too many rects → full present */

static int dirty_bits;
static int scene_ready; /* back buffer holds a full composed scene */
static int32_t cursor_mx = -1, cursor_my = -1;
static uint64_t last_clock_secs = (uint64_t)-1;

static void damage_clear(void) {
    damage_count = 0;
    damage_overflow = 0;
}

static void damage_add(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct framebuffer *fb = fb_get();
    if (!w || !h || damage_overflow)
        return;
    if (x >= fb->width || y >= fb->height)
        return;
    if (x + w > fb->width)
        w = (uint32_t)fb->width - x;
    if (y + h > fb->height)
        h = (uint32_t)fb->height - y;
    if (!w || !h)
        return;
    /* Merge into an existing rect if heavily overlapping / contained. */
    for (int i = 0; i < damage_count; i++) {
        struct damage_rect *r = &damage_list[i];
        uint32_t x2 = x + w, y2 = y + h;
        uint32_t rx2 = r->x + r->w, ry2 = r->y + r->h;
        if (x >= r->x && y >= r->y && x2 <= rx2 && y2 <= ry2)
            return;
        if (r->x >= x && r->y >= y && rx2 <= x2 && ry2 <= y2) {
            r->x = x;
            r->y = y;
            r->w = w;
            r->h = h;
            return;
        }
    }
    if (damage_count >= MAX_DAMAGE) {
        damage_overflow = 1;
        return;
    }
    damage_list[damage_count].x = x;
    damage_list[damage_count].y = y;
    damage_list[damage_count].w = w;
    damage_list[damage_count].h = h;
    damage_count++;
}

static void damage_add_win(int idx) {
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open || wins[idx].minimized)
        return;
    damage_add(wins[idx].x, wins[idx].y, wins[idx].w, wins[idx].h);
}

static void damage_merge_all(void) {
    if (damage_count <= 1 && !damage_overflow)
        return;
    uint32_t x1 = (uint32_t)-1, y1 = (uint32_t)-1, x2 = 0, y2 = 0;
    int any = 0;
    if (damage_overflow) {
        struct framebuffer *fb = fb_get();
        damage_clear();
        damage_add(0, 0, (uint32_t)fb->width, (uint32_t)fb->height);
        return;
    }
    for (int i = 0; i < damage_count; i++) {
        struct damage_rect *r = &damage_list[i];
        if (r->x < x1) x1 = r->x;
        if (r->y < y1) y1 = r->y;
        if (r->x + r->w > x2) x2 = r->x + r->w;
        if (r->y + r->h > y2) y2 = r->y + r->h;
        any = 1;
    }
    damage_clear();
    if (any && x2 > x1 && y2 > y1)
        damage_add(x1, y1, x2 - x1, y2 - y1);
}

#if defined(__x86_64__)
static uint64_t gfx_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#else
static uint64_t gfx_rdtsc(void) {
    uint64_t t;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(t));
    return t;
}
#endif

/* Approximate µs from cycle counter (coarse; good enough for sysmon). */
static uint32_t gfx_now_us(void) {
    return (uint32_t)(gfx_rdtsc() / 2000ull);
}

static void mark_focus_surf_dirty(void) {
    if (focus >= 0 && focus < MAX_WINS && wins[focus].open)
        surface_mark_dirty(&wins[focus].surf);
}

static void mark_win_surf_dirty(int idx) {
    if (idx >= 0 && idx < MAX_WINS && wins[idx].open)
        surface_mark_dirty(&wins[idx].surf);
}

static uint32_t U(uint32_t v) { return v * fb_ui_scale(); }

static uint32_t taskbar_h(void) { return fb_cell_h() + U(12); }
static uint32_t title_h(void) {
    uint32_t h = fb_cell_h() + U(8);
    return h < 22 ? 22 : h;
}

static uint32_t C_bg(void) { return theme_get()->bg; }
static uint32_t C_fg(void) { return theme_get()->fg; }
static uint32_t C_dim(void) { return theme_get()->dim; }
static uint32_t C_accent(void) { return theme_get()->accent; }
static uint32_t C_surface(void) { return theme_get()->surface; }
static uint32_t C_title(void) { return theme_get()->title; }
static uint32_t C_border(void) { return theme_get()->border; }

static struct term_state *term_active(void) {
    if (active_term >= 0 && active_term < MAX_WINS &&
        wins[active_term].open && wins[active_term].kind == APP_TERM)
        return &terms[active_term];
    for (int i = 0; i < MAX_WINS; i++) {
        if (wins[i].open && wins[i].kind == APP_TERM) {
            active_term = i;
            return &terms[i];
        }
    }
    active_term = 0;
    return &terms[0];
}

static void term_reset_slot(int slot) {
    if (slot < 0 || slot >= MAX_WINS)
        return;
    memset(&terms[slot], 0, sizeof(terms[slot]));
    terms[slot].sel_a = terms[slot].sel_b = -1;
    terms[slot].inited = 1;
    terms[slot].full_redraw = 1;
}

static void term_activate(int slot) {
    if (slot < 0 || slot >= MAX_WINS)
        return;
    if (!wins[slot].open || wins[slot].kind != APP_TERM)
        return;
    active_term = slot;
    if (!terms[slot].inited)
        term_reset_slot(slot);
}

void gui_term_reset(void) {
    struct term_state *t = term_active();
    memset(t->lines, 0, sizeof(t->lines));
    t->row = 0;
    t->col = 0;
    t->scroll = 0;
    t->sel_a = t->sel_b = -1;
    t->caret_col = 0;
    t->inited = 1;
    t->full_redraw = 1;
    t->cell_dirty = 0;
    dirty_bits |= DIRTY_TERM;
    mark_focus_surf_dirty();
}

void gui_term_set_edit(const char *prompt, const char *text, uint32_t caret,
                       int sel_a, int sel_b) {
    struct term_state *t = term_active();
    char buf[TERM_COLS + 1];
    size_t o = 0;
    if (prompt) {
        for (size_t i = 0; prompt[i] && o + 1 < sizeof(buf); i++)
            buf[o++] = prompt[i];
    }
    uint32_t prompt_len = (uint32_t)o;
    if (text) {
        for (size_t i = 0; text[i] && o + 1 < sizeof(buf); i++)
            buf[o++] = text[i];
    }
    buf[o] = '\0';

    uint32_t row = t->row;
    if (row >= TERM_ROWS)
        row = TERM_ROWS - 1;
    memset(t->lines[row], 0, TERM_COLS + 1);
    for (size_t i = 0; buf[i] && i < TERM_COLS; i++)
        t->lines[row][i] = buf[i];

    t->caret_col = prompt_len + caret;
    if (t->caret_col > TERM_COLS)
        t->caret_col = TERM_COLS;
    t->col = t->caret_col;
    t->row = row;

    if (sel_a >= 0 && sel_b >= sel_a) {
        t->sel_a = (int)prompt_len + sel_a;
        t->sel_b = (int)prompt_len + sel_b;
    } else {
        t->sel_a = t->sel_b = -1;
    }
    t->full_redraw = 1;
    dirty_bits |= DIRTY_TERM;
    mark_focus_surf_dirty();
}

static void term_scroll_up_buf(struct term_state *t) {
    for (uint32_t r = 1; r < TERM_ROWS; r++)
        memcpy(t->lines[r - 1], t->lines[r], TERM_COLS + 1);
    memset(t->lines[TERM_ROWS - 1], 0, TERM_COLS + 1);
    if (t->row > 0)
        t->row--;
}

void gui_term_putc(char c) {
    struct term_state *t = term_active();
    t->scroll = 0;
    if (c == '\n') {
        t->col = 0;
        t->row++;
        if (t->row >= TERM_ROWS)
            term_scroll_up_buf(t);
        t->full_redraw = 1;
        dirty_bits |= DIRTY_TERM;
        mark_focus_surf_dirty();
        return;
    }
    if (c == '\b') {
        if (t->col > 0) {
            t->col--;
            t->lines[t->row][t->col] = '\0';
            t->dirty_col = t->col;
            t->dirty_row = t->row;
            t->cell_dirty = 1;
            t->prev_caret_col = t->col + 1;
        }
        dirty_bits |= DIRTY_TERM;
        mark_focus_surf_dirty();
        return;
    }
    if (c < 32)
        return;
    if (t->col >= TERM_COLS) {
        t->col = 0;
        t->row++;
        if (t->row >= TERM_ROWS)
            term_scroll_up_buf(t);
        t->full_redraw = 1;
    }
    t->dirty_col = t->col;
    t->dirty_row = t->row;
    t->prev_caret_col = t->col;
    t->lines[t->row][t->col++] = c;
    t->lines[t->row][t->col] = '\0';
    t->cell_dirty = 1;
    dirty_bits |= DIRTY_TERM;
    mark_focus_surf_dirty();
}

static void cursor_sprite_ensure(uint32_t scale) {
    if (scale < 1)
        scale = 1;
    if (cursor_sprite_scale == scale && cursor_sprite_size)
        return;
    static const uint16_t shape[16] = {
        0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
        0xFF80, 0xFC00, 0xDC00, 0x8E00, 0x0700, 0x0300, 0x0100, 0x0000
    };
    uint32_t size = 16 * scale;
    if (size > CURSOR_MAX)
        size = CURSOR_MAX;
    uint32_t white = fb_rgb(0xFF, 0xFF, 0xFF);
    uint32_t black = fb_rgb(0x00, 0x00, 0x00);
    memset(cursor_sprite, 0, sizeof(cursor_sprite));
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            if (!(shape[row] & (0x8000 >> col)))
                continue;
            for (uint32_t dy = 0; dy < scale; dy++) {
                for (uint32_t dx = 0; dx < scale; dx++) {
                    uint32_t px = (uint32_t)col * scale + dx;
                    uint32_t py = (uint32_t)row * scale + dy;
                    if (px < size && py < size)
                        cursor_sprite[py * CURSOR_MAX + px] = white;
                    px += scale;
                    if (col + 1 < 16 && px < size && py < size)
                        cursor_sprite[py * CURSOR_MAX + px] = black;
                }
            }
        }
    }
    cursor_sprite_scale = scale;
    cursor_sprite_size = size;
}

static void cursor_shape_paint(int32_t x, int32_t y, uint32_t size, int to_back) {
    uint32_t s = fb_ui_scale();
    cursor_sprite_ensure(s);
    uint32_t cs = cursor_sprite_size;
    if (cs > size)
        cs = size;
    for (uint32_t row = 0; row < cs; row++) {
        for (uint32_t col = 0; col < cs; col++) {
            uint32_t c = cursor_sprite[row * CURSOR_MAX + col];
            if (!c)
                continue;
            uint32_t px = (uint32_t)(x + (int32_t)col);
            uint32_t py = (uint32_t)(y + (int32_t)row);
            if (to_back)
                fb_put_pixel(px, py, c);
            else
                fb_front_put_pixel(px, py, c);
        }
    }
}

static void cursor_erase_front(void) {
    if (!cursor_saved)
        return;
    if (fb_backbuffer_ok())
        fb_restore_from_back((uint32_t)last_cx, (uint32_t)last_cy, last_csize, last_csize);
    else {
        for (uint32_t row = 0; row < last_csize; row++)
            for (uint32_t col = 0; col < last_csize; col++)
                fb_front_put_pixel((uint32_t)(last_cx + (int32_t)col),
                                   (uint32_t)(last_cy + (int32_t)row),
                                   cursor_backup[row][col]);
    }
    cursor_saved = 0;
}

/* Draw soft cursor onto the visible front buffer only. Back stays cursor-free. */
static void draw_cursor(int32_t x, int32_t y) {
    uint32_t s = fb_ui_scale();
    uint32_t size = 16 * s;
    if (size > CURSOR_MAX)
        size = CURSOR_MAX;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    if (cursor_saved && last_cx == x && last_cy == y && last_csize == size)
        return;

    cursor_erase_front();

    if (!fb_backbuffer_ok()) {
        for (uint32_t row = 0; row < size; row++)
            for (uint32_t col = 0; col < size; col++)
                cursor_backup[row][col] =
                    fb_front_get_pixel((uint32_t)(x + (int32_t)col),
                                       (uint32_t)(y + (int32_t)row));
    }

    cursor_saved = 1;
    last_cx = x;
    last_cy = y;
    last_csize = size;
    cursor_mx = x;
    cursor_my = y;
    cursor_shape_paint(x, y, size, 0);
}

/* Present composed back buffer; cursor is overlaid on front afterward. */
static void present_scene(int full_present) {
    if (!fb_backbuffer_ok())
        return;

    cursor_erase_front();

    uint32_t t0 = gfx_now_us();
    int use_full = full_present || damage_overflow || damage_count == 0;
    if (!use_full) {
        uint64_t dmg_px = 0;
        uint64_t screen_px = (uint64_t)fb_get()->width * (uint64_t)fb_get()->height;
        for (int i = 0; i < damage_count; i++)
            dmg_px += (uint64_t)damage_list[i].w * (uint64_t)damage_list[i].h;
        if (screen_px && dmg_px * 4 >= screen_px)
            use_full = 1;
    }
    if (use_full) {
        display_frame_begin();
        display_present_full(fb_back_buf());
        display_frame_end();
    } else {
        uint32_t fw = (uint32_t)fb_get()->width;
        for (int i = 0; i < damage_count; i++) {
            uint32_t x = damage_list[i].x, y = damage_list[i].y;
            uint32_t w = damage_list[i].w, h = damage_list[i].h;
            if (x >= fw || y >= (uint32_t)fb_get()->height)
                continue;
            display_present_rect(x, y, w, h, fb_back_buf() + (uint64_t)y * fw + x, fw);
        }
    }
    sysmon_note_present_us(gfx_now_us() - t0);
    sysmon_note_surf_pressure((uint32_t)surface_pressure_pct());
}

static int point_in(int32_t px, int32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

static uint32_t resize_grip(void) {
    uint32_t g = U(14);
    return g < 12 ? 12 : g;
}

static uint32_t win_min_w(void) { return U(180); }
static uint32_t win_min_h(void) { return title_h() + U(100); }

static int hit_resize_grip(struct win *w, int32_t mx, int32_t my) {
    uint32_t g = resize_grip();
    return point_in(mx, my, w->x + w->w - g, w->y + w->h - g, g, g);
}

/* Returns edge mask: 1=L 2=R 4=T 8=B */
static int hit_resize_edge(struct win *w, int32_t mx, int32_t my) {
    uint32_t e = U(6);
    int m = 0;
    if (point_in(mx, my, w->x, w->y, e, w->h))
        m |= 1;
    if (point_in(mx, my, w->x + w->w - e, w->y, e, w->h))
        m |= 2;
    if (point_in(mx, my, w->x, w->y, w->w, e))
        m |= 4;
    if (point_in(mx, my, w->x, w->y + w->h - e, w->w, e))
        m |= 8;
    if (hit_resize_grip(w, mx, my))
        m |= 2 | 8;
    return m;
}

static void clamp_win_geom(struct win *w) {
    struct framebuffer *fb = fb_get();
    uint32_t tb = taskbar_h();
    uint32_t max_w = (uint32_t)fb->width;
    uint32_t max_h = (uint32_t)fb->height > tb ? (uint32_t)fb->height - tb : (uint32_t)fb->height;
    if (w->w < win_min_w())
        w->w = win_min_w();
    if (w->h < win_min_h())
        w->h = win_min_h();
    if (w->w > max_w)
        w->w = max_w;
    if (w->h > max_h)
        w->h = max_h;
    if (w->x + w->w > max_w)
        w->x = max_w > w->w ? max_w - w->w : 0;
    if (w->y + w->h > max_h)
        w->y = max_h > w->h ? max_h - w->h : 0;
}

/* Re-fit windows after a UI scale change: min sizes and the taskbar
 * height depend on the scale, so stale geometry can push content
 * off-screen. */
static void rescale_windows(void) {
    struct framebuffer *fb = fb_get();
    for (int i = 0; i < MAX_WINS; i++) {
        if (!wins[i].open)
            continue;
        if (wins[i].maximized) {
            wins[i].x = 0;
            wins[i].y = 0;
            wins[i].w = (uint32_t)fb->width;
            wins[i].h = (uint32_t)fb->height > taskbar_h()
                            ? (uint32_t)fb->height - taskbar_h()
                            : (uint32_t)fb->height;
        } else {
            clamp_win_geom(&wins[i]);
        }
        surface_ensure(&wins[i].surf, wins[i].w, wins[i].h);
        surface_mark_dirty(&wins[i].surf);
    }
}

static const char *app_title(enum app_kind k) {
    switch (k) {
    case APP_TERM: return "Terminal";
    case APP_FILES: return "Files";
    case APP_SETTINGS: return "Settings";
    case APP_AGENT: return "Agent";
    case APP_GAME: return "Peak Runner";
    case APP_BROWSER: return "Browser";
    case APP_MONITOR: return "Monitor";
    }
    return "Window";
}

static int find_win(enum app_kind k) {
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open && wins[i].kind == k)
            return i;
    return -1;
}

static void raise_win(int idx) {
    int prev_focus = focus;
    int maxz = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open && wins[i].z > maxz)
            maxz = wins[i].z;
    wins[idx].z = maxz + 1;
    focus = idx;
    if (wins[idx].kind == APP_TERM)
        term_activate(idx);
    if (prev_focus >= 0 && prev_focus != idx)
        damage_add_win(prev_focus);
    damage_add_win(idx);
    dirty_bits |= DIRTY_MOVE;
}

static void maximize_win(int idx) {
    struct framebuffer *fb = fb_get();
    struct win *w = &wins[idx];
    uint32_t ox = w->x, oy = w->y, ow = w->w, oh = w->h;
    if (!w->maximized) {
        w->rx = w->x;
        w->ry = w->y;
        w->rw = w->w;
        w->rh = w->h;
        w->x = 0;
        w->y = 0;
        w->w = (uint32_t)fb->width;
        w->h = (uint32_t)fb->height > taskbar_h()
                   ? (uint32_t)fb->height - taskbar_h()
                   : (uint32_t)fb->height;
        w->maximized = 1;
        w->minimized = 0;
    } else {
        w->x = w->rx;
        w->y = w->ry;
        w->w = w->rw;
        w->h = w->rh;
        w->maximized = 0;
        clamp_win_geom(w);
    }
    damage_add(ox, oy, ow, oh);
    damage_add(w->x, w->y, w->w, w->h);
    surface_ensure(&w->surf, w->w, w->h);
    surface_mark_dirty(&w->surf);
    dirty_bits |= DIRTY_MOVE;
}

static void minimize_win(int idx) {
    uint32_t ox = wins[idx].x, oy = wins[idx].y, ow = wins[idx].w, oh = wins[idx].h;
    int prev_focus = focus;
    wins[idx].minimized = 1;
    if (focus == idx) {
        focus = -1;
        int best = -1, bz = -1;
        for (int i = 0; i < MAX_WINS; i++)
            if (wins[i].open && !wins[i].minimized && wins[i].z > bz) {
                bz = wins[i].z;
                best = i;
            }
        focus = best;
    }
    damage_add(ox, oy, ow, oh);
    if (prev_focus >= 0 && prev_focus != idx)
        damage_add_win(prev_focus);
    if (focus >= 0)
        damage_add_win(focus);
    dirty_bits |= DIRTY_MOVE;
}

static int open_app(enum app_kind k) {
    /* Multi-instance for Terminal; others stay singleton */
    if (k != APP_TERM) {
        int existing = find_win(k);
        if (existing >= 0) {
            wins[existing].minimized = 0;
            raise_win(existing);
            dirty_bits |= DIRTY_FULL;
            return existing;
        }
    }
    int slot = -1;
    for (int i = 0; i < MAX_WINS; i++)
        if (!wins[i].open) {
            slot = i;
            break;
        }
    if (slot < 0)
        return -1;
    struct framebuffer *fb = fb_get();
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    memset(&wins[slot], 0, sizeof(wins[slot]));
    wins[slot].kind = k;
    wins[slot].open = 1;
    wins[slot].w = TERM_COLS * cw / 2 + U(40);
    if (wins[slot].w < U(420))
        wins[slot].w = U(420);
    wins[slot].h = title_h() + TERM_VIEW * ch + U(40);
    if (k == APP_TERM) {
        term_reset_slot(slot);
        term_activate(slot);
        shell_redraw_prompt();
    }
    if (k == APP_SETTINGS) {
        wins[slot].w = U(480);
        wins[slot].h = title_h() + U(380);
        settings_page = 0;
    }
    if (k == APP_AGENT) {
        wins[slot].w = U(420);
        wins[slot].h = title_h() + U(260);
        agent_input_len = 0;
        agent_input[0] = '\0';
    }
    if (k == APP_GAME) {
        wins[slot].w = U(420);
        wins[slot].h = title_h() + U(220);
        game_reset();
    }
    if (k == APP_BROWSER) {
        wins[slot].w = U(520);
        wins[slot].h = title_h() + U(320);
        browser_reset();
    }
    if (k == APP_MONITOR) {
        wins[slot].w = U(640);
        wins[slot].h = title_h() + U(460);
        monitor_reset();
    }
    if (k == APP_FILES) {
        wins[slot].w = U(480);
        wins[slot].h = title_h() + U(360);
    }
    if (wins[slot].w > fb->width - U(40))
        wins[slot].w = (uint32_t)fb->width - U(40);
    wins[slot].x = U(40) + (uint32_t)(slot * 24);
    wins[slot].y = U(40) + (uint32_t)(slot * 24);
    clamp_win_geom(&wins[slot]);
    raise_win(slot);
    surface_ensure(&wins[slot].surf, wins[slot].w, wins[slot].h);
    surface_mark_dirty(&wins[slot].surf);
    notify_push(app_title(k));
    dirty_bits |= DIRTY_FULL;
    return slot;
}

static void close_win(int idx) {
    surface_free(&wins[idx].surf);
    wins[idx].open = 0;
    wins[idx].minimized = 0;
    wins[idx].maximized = 0;
    if (focus == idx) {
        focus = -1;
        int best = -1, bz = -1;
        for (int i = 0; i < MAX_WINS; i++)
            if (wins[i].open && !wins[i].minimized && wins[i].z > bz) {
                bz = wins[i].z;
                best = i;
            }
        focus = best;
    }
    dirty_bits |= DIRTY_FULL;
}

static void draw_desktop_bg(void) {
    struct framebuffer *fb = fb_get();
    uint32_t h = (uint32_t)fb->height;
    uint32_t w = (uint32_t)fb->width;
    uint32_t tb = taskbar_h();
    uint32_t desk_h = h - tb;
    if (wallpaper_enabled())
        wallpaper_draw(0, 0, w, desk_h);
    else
        fb_fill_rect(0, 0, w, desk_h, C_bg());
    if (settings_show_brand()) {
        uint32_t lx = U(24), ly = U(24);
        uint32_t ch = fb_cell_h();
        uint32_t scrim = wallpaper_enabled() ? C_surface() : C_bg();
        fb_fill_rect(lx - U(8), ly - U(6), U(120), ch + U(12), scrim);
        fb_draw_string(lx, ly, "PeakOS", C_fg(), scrim);
    }
}

static void format_clock(char *tbuf, size_t tlen) {
    rtc_format_clock(tbuf, tlen);
    if (!tbuf[0]) {
        uint64_t secs = timer_uptime_secs();
        snprintf(tbuf, tlen, "%lum", (unsigned long)(secs / 60));
    }
}

static void clock_rect(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    struct framebuffer *fb = fb_get();
    uint32_t th = taskbar_h();
    *x = (uint32_t)fb->width - U(110);
    *y = (uint32_t)fb->height - th;
    *w = U(110);
    *h = th;
}

static void draw_clock_area(void) {
    if (!settings_show_clock())
        return;
    uint32_t cx, cy, cw, ch;
    clock_rect(&cx, &cy, &cw, &ch);
    fb_fill_rect(cx, cy, cw, ch, C_surface());
    fb_fill_rect(cx, cy, cw, U(2), C_accent());
    char tbuf[16];
    format_clock(tbuf, sizeof(tbuf));
    last_clock_secs = timer_uptime_secs();
    fb_draw_string((uint32_t)fb_get()->width - U(100),
                   cy + (ch - fb_cell_h()) / 2, tbuf, C_fg(), C_surface());
}

static uint32_t taskbar_btn_w(void) { return U(88); }

static void draw_taskbar(void) {
    struct framebuffer *fb = fb_get();
    uint32_t th = taskbar_h();
    uint32_t y = (uint32_t)fb->height - th;
    fb_fill_rect(0, y, (uint32_t)fb->width, th, C_surface());
    fb_fill_rect(0, y, (uint32_t)fb->width, U(2), C_accent());
    fb_draw_string(U(12), y + (th - fb_cell_h()) / 2, "Peak", C_fg(), C_surface());

    /* Open window buttons */
    uint32_t bx = U(70);
    uint32_t bw = taskbar_btn_w();
    uint32_t by = y + U(4);
    uint32_t bh = th > U(8) ? th - U(8) : th;
    for (int i = 0; i < MAX_WINS; i++) {
        if (!wins[i].open)
            continue;
        uint32_t bg = (i == focus && !wins[i].minimized) ? C_accent() : C_bg();
        uint32_t fg = (i == focus && !wins[i].minimized) ? C_bg() : C_fg();
        if (wins[i].minimized)
            bg = C_dim();
        fb_fill_rect(bx, by, bw - U(4), bh, bg);
        fb_draw_string_fit(bx + U(4), by + (bh - fb_cell_h()) / 2, bw - U(12),
                           app_title(wins[i].kind), fg, bg);
        bx += bw;
        if (bx + bw > (uint32_t)fb->width - U(120))
            break;
    }

    /* Net indicator */
    struct net_info ni;
    net_get_info(&ni);
    fb_draw_string_fit((uint32_t)fb->width - U(160), y + (th - fb_cell_h()) / 2,
                       U(50), ni.up ? "net" : "off", ni.up ? C_accent() : C_dim(),
                       C_surface());
    draw_clock_area();
}

static void draw_start_menu(void) {
    if (!menu_open)
        return;
    struct framebuffer *fb = fb_get();
    uint32_t th = taskbar_h();
    uint32_t mw = U(180);
    uint32_t mh = U(320);
    uint32_t mx = U(8);
    uint32_t my = (uint32_t)fb->height - th - mh - U(4);
    fb_fill_rect(mx, my, mw, mh, C_surface());
    fb_fill_rect(mx, my, mw, U(2), C_accent());
    const char *items[] = {
        "Terminal", "Files", "Settings", "Agent", "Peak Runner",
        "Browser", "Monitor", "Theme", "Help", "Save disk",
        "Lock", "Exit desktop", "Reboot", "Power off"
    };
    for (int i = 0; i < 14; i++) {
        fb_draw_string(mx + U(12), my + U(12) + (uint32_t)i * (fb_cell_h() + U(4)),
                       items[i], C_fg(), C_surface());
    }
}

static void draw_session_overlays(void) {
    struct framebuffer *fb = fb_get();
    if (session_lock) {
        fb_fill_rect(0, 0, (uint32_t)fb->width, (uint32_t)fb->height, C_bg());
        uint32_t mw = U(340);
        uint32_t mh = U(120);
        uint32_t mx = ((uint32_t)fb->width - mw) / 2;
        uint32_t my = ((uint32_t)fb->height - mh) / 3;
        fb_fill_rect(mx, my, mw, mh, C_surface());
        fb_fill_rect(mx, my, mw, U(3), C_accent());
        fb_draw_string(mx + U(24), my + U(28), "Session locked", C_fg(), C_surface());
        fb_draw_string(mx + U(24), my + U(28) + fb_cell_h() + U(8),
                       "Press Enter to unlock (single-user)", C_dim(), C_surface());
        return;
    }
    if (power_confirm) {
        uint32_t mw = U(360);
        uint32_t mh = U(130);
        uint32_t mx = ((uint32_t)fb->width - mw) / 2;
        uint32_t my = ((uint32_t)fb->height - mh) / 3;
        fb_fill_rect(mx, my, mw, mh, C_surface());
        fb_fill_rect(mx, my, mw, U(3), C_accent());
        fb_draw_string(mx + U(24), my + U(24),
                       power_confirm == 1 ? "Power off?" : "Reboot?",
                       C_fg(), C_surface());
        fb_draw_string(mx + U(24), my + U(24) + fb_cell_h() + U(10),
                       "Y confirm · N / Esc cancel", C_dim(), C_surface());
    }
}

static void draw_ctx_menu(void) {
    if (!ctx_menu)
        return;
    uint32_t mw = U(140);
    uint32_t mh = U(90);
    fb_fill_rect((uint32_t)ctx_x, (uint32_t)ctx_y, mw, mh, C_surface());
    fb_fill_rect((uint32_t)ctx_x, (uint32_t)ctx_y, mw, U(2), C_accent());
    fb_draw_string((uint32_t)ctx_x + U(8), (uint32_t)ctx_y + U(10), "Terminal", C_fg(), C_surface());
    fb_draw_string((uint32_t)ctx_x + U(8), (uint32_t)ctx_y + U(10) + fb_cell_h() + U(4),
                   "Files", C_fg(), C_surface());
    fb_draw_string((uint32_t)ctx_x + U(8), (uint32_t)ctx_y + U(10) + 2 * (fb_cell_h() + U(4)),
                   "Settings", C_fg(), C_surface());
}

static void draw_alttab(void) {
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
    uint32_t mw = U(280);
    uint32_t mh = U(40) + (uint32_t)n * (fb_cell_h() + U(6));
    uint32_t mx = ((uint32_t)fb->width - mw) / 2;
    uint32_t my = ((uint32_t)fb->height - mh) / 3;
    fb_fill_rect(mx, my, mw, mh, C_surface());
    fb_fill_rect(mx, my, mw, U(2), C_accent());
    fb_draw_string(mx + U(12), my + U(8), "Switch window", C_dim(), C_surface());
    for (int i = 0; i < n; i++) {
        uint32_t bg = (i == alttab_sel) ? C_accent() : C_surface();
        uint32_t fg = (i == alttab_sel) ? C_bg() : C_fg();
        uint32_t ry = my + U(28) + (uint32_t)i * (fb_cell_h() + U(6));
        fb_fill_rect(mx + U(8), ry, mw - U(16), fb_cell_h() + U(2), bg);
        fb_draw_string(mx + U(16), ry, app_title(wins[order[i]].kind), fg, bg);
    }
}

static void draw_help(void) {
    if (!help_open)
        return;
    struct framebuffer *fb = fb_get();
    uint32_t mw = U(420);
    uint32_t mh = U(260);
    uint32_t mx = ((uint32_t)fb->width - mw) / 2;
    uint32_t my = U(80);
    fb_fill_rect(mx, my, mw, mh, C_surface());
    fb_fill_rect(mx, my, mw, U(2), C_accent());
    uint32_t cy = my + U(12);
    uint32_t ch = fb_cell_h() + U(2);
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
        fb_draw_string(mx + U(12), cy, lines[i], i == 0 ? C_accent() : C_fg(), C_surface());
        cy += ch;
    }
}

static void draw_win_chrome(struct win *w, int focused) {
    window_draw_frame(w->x, w->y, w->w, w->h, app_title(w->kind),
                      focused ? C_bg() : C_surface());
    uint32_t by = w->y + U(6);
    uint32_t bs = U(14);
    uint32_t gap = U(4);
    /* close */
    uint32_t bx = w->x + w->w - U(22);
    fb_fill_rect(bx, by, bs, bs, theme_get()->danger);
    /* maximize */
    bx -= bs + gap;
    fb_fill_rect(bx, by, bs, bs, C_accent());
    /* minimize */
    bx -= bs + gap;
    fb_fill_rect(bx, by, bs, bs, C_dim());
    fb_fill_rect(bx + U(2), by + bs / 2, bs - U(4), U(2), C_fg());

    if (!w->maximized) {
        uint32_t g = resize_grip();
        uint32_t gx = w->x + w->w - g;
        uint32_t gy = w->y + w->h - g;
        uint32_t accent = focused ? C_accent() : C_dim();
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t o = U(3) + i * U(3);
            fb_fill_rect(gx + o, gy + g - U(3), g - o - U(2), U(2), accent);
            fb_fill_rect(gx + g - U(3), gy + o, U(2), g - o - U(2), accent);
        }
    }
}

static void draw_term_content(struct win *w) {
    int slot = (int)(w - wins);
    if (slot < 0 || slot >= MAX_WINS)
        return;
    struct term_state *t = &terms[slot];
    if (!t->inited)
        term_reset_slot(slot);
    uint32_t cw = fb_cell_w();
    uint32_t ch = fb_cell_h();
    uint32_t th = title_h();
    uint32_t tx = w->x + U(12);
    uint32_t ty = w->y + th + U(8);
    uint32_t bg = C_bg();
    uint32_t area_h = w->h > th + U(16) ? w->h - th - U(16) : ch;
    uint32_t vis = area_h / ch;
    if (vis > TERM_VIEW)
        vis = TERM_VIEW;
    if (vis < 1)
        vis = 1;
    int start = (int)t->row - (int)vis + 1 - t->scroll;
    if (start < 0)
        start = 0;

    /* Hot path: single-cell update when only one putc dirtied the grid. */
    if (!t->full_redraw && t->cell_dirty && t->scroll == 0 && t->sel_a < 0) {
        int lr = (int)t->dirty_row;
        int caret_row = (int)t->row - start;
        int dirty_vis = lr - start;
        if (dirty_vis >= 0 && dirty_vis < (int)vis) {
            uint32_t c = t->dirty_col;
            char chv = (c < TERM_COLS) ? t->lines[lr][c] : '\0';
            if (chv)
                fb_draw_char(tx + c * cw, ty + (uint32_t)dirty_vis * ch, chv, C_fg(), bg);
            else
                fb_fill_rect(tx + c * cw, ty + (uint32_t)dirty_vis * ch, cw, ch, bg);
            /* Erase previous caret, draw new. */
            if (t->prev_caret_col < TERM_COLS)
                fb_fill_rect(tx + t->prev_caret_col * cw,
                             ty + (uint32_t)caret_row * ch, U(2), ch, bg);
            if (caret_row >= 0 && caret_row < (int)vis) {
                uint32_t cx = t->caret_col < TERM_COLS ? t->caret_col : t->col;
                fb_fill_rect(tx + cx * cw, ty + (uint32_t)caret_row * ch, U(2), ch, C_accent());
            }
            t->cell_dirty = 0;
            return;
        }
        t->full_redraw = 1;
    }

    fb_fill_rect(tx, ty, w->w > U(24) ? w->w - U(24) : w->w, vis * ch + U(4), bg);
    for (uint32_t r = 0; r < vis; r++) {
        int lr = start + (int)r;
        if (lr < 0 || lr >= (int)TERM_ROWS)
            continue;
        const char *s = t->lines[lr];
        for (uint32_t c = 0; s[c] && c < TERM_COLS; c++) {
            uint32_t fg = C_fg();
            uint32_t cell_bg = bg;
            if (lr == (int)t->row && t->sel_a >= 0 &&
                (int)c >= t->sel_a && (int)c <= t->sel_b) {
                cell_bg = C_accent();
                fg = C_bg();
            }
            fb_draw_char(tx + c * cw, ty + r * ch, s[c], fg, cell_bg);
        }
    }
    int caret_row = (int)t->row - start;
    if (t->scroll == 0 && caret_row >= 0 && caret_row < (int)vis) {
        uint32_t cx = t->caret_col < TERM_COLS ? t->caret_col : t->col;
        fb_fill_rect(tx + cx * cw, ty + (uint32_t)caret_row * ch, U(2), ch, C_accent());
    }
    t->full_redraw = 0;
    t->cell_dirty = 0;
}

static void files_clamp_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0) {
        files_sel = 0;
        files_scroll = 0;
        return;
    }
    if (files_sel < 0)
        files_sel = 0;
    if (files_sel >= n)
        files_sel = n - 1;
}

static void draw_files_content(struct win *w) {
    files_clamp_sel();
    uint32_t ch = fb_cell_h();
    uint32_t th = title_h();
    uint32_t tx = w->x + U(12);
    uint32_t ty = w->y + th + U(8);
    uint32_t inner = w->w > U(24) ? w->w - U(24) : w->w;
    fb_draw_string_fit(tx, ty, inner, files_cwd, C_dim(), C_bg());
    fb_draw_string_fit(tx, ty + ch, inner, "[n]ew [d]el [r]ename [u]p  wheel scroll",
                       C_dim(), C_bg());
    uint32_t area_h = w->h > th + ch * 2 + U(24) ? w->h - th - ch * 2 - U(24) : ch;
    int max_rows = (int)(area_h / ch);
    if (max_rows > FILES_ROWS)
        max_rows = FILES_ROWS;
    if (max_rows < 1)
        max_rows = 1;
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n < 0)
        n = 0;
    if (files_scroll > n)
        files_scroll = n > 0 ? n - 1 : 0;
    if (files_sel < files_scroll)
        files_scroll = files_sel;
    if (files_sel >= files_scroll + max_rows)
        files_scroll = files_sel - max_rows + 1;
    for (int i = 0; i < max_rows && files_scroll + i < n; i++) {
        int idx = files_scroll + i;
        uint32_t rowy = ty + ch * 2 + U(4) + (uint32_t)i * ch;
        uint32_t bg = (idx == files_sel) ? C_title() : C_bg();
        if (idx == files_sel)
            fb_fill_rect(tx, rowy, inner, ch, C_title());
        char label[VFS_NAME_MAX + 4];
        snprintf(label, sizeof(label), "%s%s", ents[idx].name,
                 ents[idx].type == VFS_DIR ? "/" : "");
        fb_draw_string_fit(tx, rowy, inner, label,
                           ents[idx].type == VFS_DIR ? C_accent() : C_fg(), bg);
    }
}

static void draw_settings_content(struct win *w) {
    uint32_t ch = fb_cell_h();
    uint32_t th = title_h();
    uint32_t pad = U(12);
    uint32_t tx = w->x + pad;
    uint32_t ty = w->y + th + pad;
    uint32_t row = ch + U(4);
    uint32_t content_w = w->w > pad * 2 ? w->w - pad * 2 : w->w;
    struct framebuffer *fb = fb_get();

    /* Tab strip */
    static const char *tabs[SETTINGS_PAGES] = {"Display", "Look", "General", "Network"};
    uint32_t tab_w = content_w / SETTINGS_PAGES;
    if (tab_w < U(56))
        tab_w = U(56);
    for (int i = 0; i < SETTINGS_PAGES; i++) {
        uint32_t tabx = tx + (uint32_t)i * tab_w;
        uint32_t bg = (i == settings_page) ? C_accent() : C_surface();
        uint32_t fg = (i == settings_page) ? C_bg() : C_fg();
        fb_fill_rect(tabx, ty, tab_w - U(4), ch + U(6), bg);
        fb_draw_string_fit(tabx + U(4), ty + U(3), tab_w - U(8), tabs[i], fg, bg);
    }

    uint32_t cy = ty + ch + U(16);
    char line[64];

    if (settings_page == 0) {
        fb_draw_string(tx, cy, "UI scale (click):", C_fg(), C_bg());
        cy += row;
        snprintf(line, sizeof(line), "%ux  (recommended %ux)",
                 (unsigned)settings_gui_scale(),
                 (unsigned)fb_recommend_scale());
        fb_draw_string(tx, cy, line, C_accent(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, "Click to cycle 1–4. High-res defaults larger.", C_dim(), C_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Framebuffer", C_dim(), C_bg());
        cy += row;
        snprintf(line, sizeof(line), "%ux%u  %ubpp",
                 (unsigned)fb->width, (unsigned)fb->height, (unsigned)fb->bpp);
        fb_draw_string(tx, cy, line, C_fg(), C_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Resize tip", C_dim(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, "Drag the bottom-right grip on any window.", C_fg(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, "Drag the title bar to move.", C_fg(), C_bg());
    } else if (settings_page == 1) {
        fb_draw_string(tx, cy, "Theme (click):", C_fg(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, theme_name(), C_accent(), C_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Wallpaper (click):", C_fg(), C_bg());
        cy += row;
        const char *wp = wallpaper_enabled() ? wallpaper_path() : "none (solid theme)";
        const char *wp_show = wp;
        for (const char *p = wp; *p; p++)
            if (*p == '/')
                wp_show = p + 1;
        fb_draw_string(tx, cy, wp_show, C_accent(), C_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Desktop brand label (click):", C_fg(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, settings_show_brand() ? "on" : "off", C_accent(), C_bg());
    } else if (settings_page == 2) {
        fb_draw_string(tx, cy, "Taskbar clock (click):", C_fg(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, settings_show_clock() ? "on" : "off", C_accent(), C_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "System", C_dim(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, "PeakOS 0.2 — desktop readiness", C_fg(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, "Ctrl+Alt+Esc leaves desktop.", C_dim(), C_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "Disk", C_dim(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, peakdisk_available() ? "Disk persist ready" : "No block disk",
                       C_fg(), C_bg());
    } else {
        struct net_info ni;
        net_get_info(&ni);
        char ip[32], gw[32], dns[32];
        net_format_ip(ni.ip, ip, sizeof(ip));
        net_format_ip(ni.gw, gw, sizeof(gw));
        net_format_ip(ni.dns, dns, sizeof(dns));
        fb_draw_string(tx, cy, "Network", C_dim(), C_bg());
        cy += row;
        fb_draw_string(tx, cy, ni.up ? "link: up" : "link: down", C_accent(), C_bg());
        cy += row;
        snprintf(line, sizeof(line), "ip %s", ip);
        fb_draw_string(tx, cy, line, C_fg(), C_bg());
        cy += row;
        snprintf(line, sizeof(line), "gw %s", gw);
        fb_draw_string(tx, cy, line, C_fg(), C_bg());
        cy += row;
        snprintf(line, sizeof(line), "dns %s", dns);
        fb_draw_string(tx, cy, line, C_fg(), C_bg());
        cy += row * 2;
        fb_draw_string(tx, cy, "IPv4: static QEMU defaults (no DHCP client).", C_dim(), C_bg());
    }
    (void)content_w;
}

static void draw_win_content(int i) {
    struct win *w = &wins[i];
    draw_win_chrome(w, i == focus);
    if (w->kind == APP_TERM)
        draw_term_content(w);
    else if (w->kind == APP_FILES)
        draw_files_content(w);
    else if (w->kind == APP_SETTINGS)
        draw_settings_content(w);
    else if (w->kind == APP_AGENT) {
        uint32_t ax = w->x + U(8);
        uint32_t ay = w->y + title_h() + U(4);
        uint32_t aw = w->w - U(16);
        uint32_t ah = w->h - title_h() - U(12);
        agent_gui_draw(ax, ay, aw, ah > U(80) ? ah - U(40) : ah / 2);
        uint32_t iy = ay + (ah > U(80) ? ah - U(36) : ah / 2 + U(4));
        fb_fill_rect(ax, iy, aw, fb_cell_h() + U(8), C_surface());
        char prompt[112];
        snprintf(prompt, sizeof(prompt), "> %s", agent_input);
        fb_draw_string_fit(ax + U(4), iy + U(4), aw - U(8), prompt, C_fg(), C_surface());
    } else if (w->kind == APP_GAME) {
        game_draw(w->x + U(4), w->y + title_h() + U(2),
                  w->w - U(8), w->h - title_h() - U(6));
    } else if (w->kind == APP_BROWSER) {
        browser_draw(w->x + U(4), w->y + title_h() + U(2),
                     w->w - U(8), w->h - title_h() - U(6));
    } else if (w->kind == APP_MONITOR) {
        monitor_draw(w->x + U(4), w->y + title_h() + U(2),
                     w->w - U(8), w->h - title_h() - U(6));
    }
}

static int rects_overlap(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah,
                         uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/* True if no higher-z window overlaps this one (safe for partial present). */
static int win_unobscured(int idx) {
    if (idx < 0 || !wins[idx].open || wins[idx].minimized)
        return 0;
    for (int i = 0; i < MAX_WINS; i++) {
        if (i == idx || !wins[i].open || wins[i].minimized || wins[i].z <= wins[idx].z)
            continue;
        if (rects_overlap(wins[idx].x, wins[idx].y, wins[idx].w, wins[idx].h,
                          wins[i].x, wins[i].y, wins[i].w, wins[i].h))
            return 0;
    }
    return 1;
}

static void paint_win_to_surface(int i) {
    struct win *w = &wins[i];
    if (surface_ensure(&w->surf, w->w, w->h) != 0)
        return;
    uint32_t ox = w->x, oy = w->y;
    w->x = 0;
    w->y = 0;
    fb_set_draw_target(w->surf.px, w->surf.w, w->surf.h, w->surf.stride);
    draw_win_content(i);
    fb_set_draw_target(NULL, 0, 0, 0);
    w->x = ox;
    w->y = oy;
    w->surf.dirty = 0;
}

static void compose_win(int i) {
    struct win *w = &wins[i];
    if (!w->open || w->minimized)
        return;
    if (w->surf.px && w->surf.dirty)
        paint_win_to_surface(i);
    if (w->surf.px && !w->surf.dirty)
        surface_blit(&w->surf, w->x, w->y);
    else
        draw_win_content(i);
}

static void draw_windows(void) {
    /* sort indices by z ascending */
    int order[MAX_WINS];
    int n = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open)
            order[n++] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (wins[order[j]].z < wins[order[i]].z) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
    for (int k = 0; k < n; k++) {
        if (wins[order[k]].minimized)
            continue;
        compose_win(order[k]);
    }
    (void)C_border;
}

struct proto_blit_ctx {
    uint32_t dx, dy, dw, dh;
    int clip;
};

static void proto_blit_cb(int id, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          const struct win_surface *s, void *ctx) {
    struct proto_blit_ctx *c = (struct proto_blit_ctx *)ctx;
    (void)id;
    (void)w;
    (void)h;
    if (!s || !s->px)
        return;
    if (c->clip && !rects_overlap(x, y, s->w, s->h, c->dx, c->dy, c->dw, c->dh))
        return;
    surface_blit(s, x, y);
}

static void draw_proto_surfaces(uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, int clip) {
    struct proto_blit_ctx c = { dx, dy, dw, dh, clip };
    guiproto_for_each_surface(proto_blit_cb, &c);
}

static void fill_desk_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct framebuffer *fb = fb_get();
    uint32_t tb = taskbar_h();
    uint32_t desk_h = (uint32_t)fb->height > tb ? (uint32_t)fb->height - tb : (uint32_t)fb->height;
    if (y >= desk_h)
        return;
    if (y + h > desk_h)
        h = desk_h - y;
    if (!w || !h)
        return;
    if (wallpaper_enabled())
        wallpaper_draw(x, y, w, h);
    else
        fb_fill_rect(x, y, w, h, C_bg());
}

static void draw_wins_below(int skip_idx, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    int order[MAX_WINS];
    int n = 0;
    int skip_z = (skip_idx >= 0) ? wins[skip_idx].z : 0x7fffffff;
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open && !wins[i].minimized && wins[i].z < skip_z)
            order[n++] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (wins[order[j]].z < wins[order[i]].z) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
    for (int k = 0; k < n; k++) {
        int i = order[k];
        if (!rects_overlap(wins[i].x, wins[i].y, wins[i].w, wins[i].h, x, y, w, h))
            continue;
        compose_win(i);
    }
}

static void opaque_move_free(void) {
    if (move_pixmap) {
        kfree(move_pixmap);
        move_pixmap = 0;
    }
    if (move_underlay) {
        kfree(move_underlay);
        move_underlay = 0;
    }
    move_pw = move_ph = 0;
    move_live = 0;
}

static void opaque_move_begin(int idx) {
    opaque_move_free();
    if (idx < 0 || idx >= MAX_WINS || !wins[idx].open || !fb_backbuffer_ok())
        return;
    struct win *w = &wins[idx];
    uint64_t px = (uint64_t)w->w * (uint64_t)w->h;
    if (!w->w || !w->h || px > MOVE_PIX_CAP)
        return;
    size_t bytes = (size_t)px * 4u;
    move_pixmap = (uint32_t *)kmalloc(bytes);
    move_underlay = (uint32_t *)kmalloc(bytes);
    if (!move_pixmap || !move_underlay) {
        opaque_move_free();
        return;
    }
    move_pw = w->w;
    move_ph = w->h;
    fb_copy_from_back(w->x, w->y, w->w, w->h, move_pixmap, w->w);
    /* Build underlay: paint desk + lower windows into win rect, capture, restore. */
    fb_begin_frame();
    fill_desk_rect(w->x, w->y, w->w, w->h);
    draw_wins_below(idx, w->x, w->y, w->w, w->h);
    draw_proto_surfaces(w->x, w->y, w->w, w->h, 1);
    fb_cancel_frame();
    fb_copy_from_back(w->x, w->y, w->w, w->h, move_underlay, w->w);
    fb_copy_to_back(w->x, w->y, w->w, w->h, move_pixmap, w->w);
    move_live = 1;
}

static void opaque_move_step(uint32_t old_x, uint32_t old_y,
                             uint32_t new_x, uint32_t new_y,
                             uint32_t w, uint32_t h) {
    if (!move_live || !move_pixmap || !move_underlay)
        return;
    if (w != move_pw || h != move_ph)
        return;
    fb_copy_to_back(old_x, old_y, w, h, move_underlay, w);
    fb_copy_from_back(new_x, new_y, w, h, move_underlay, w);
    fb_copy_to_back(new_x, new_y, w, h, move_pixmap, w);
    damage_clear();
    damage_add(old_x, old_y, w, h);
    damage_add(new_x, new_y, w, h);
    present_scene(0);
    move_prev_x = new_x;
    move_prev_y = new_y;
    move_prev_w = w;
    move_prev_h = h;
    move_prev_valid = 1;
}

static void opaque_move_end(void) {
    opaque_move_free();
    dirty_bits |= DIRTY_FULL;
}

static void draw_rubber_band(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t c = C_accent();
    uint32_t t = U(2);
    if (t < 2) t = 2;
    if (w < t * 2 || h < t * 2)
        return;
    fb_fill_rect(x, y, w, t, c);
    fb_fill_rect(x, y + h - t, w, t, c);
    fb_fill_rect(x, y, t, h, c);
    fb_fill_rect(x + w - t, y, t, h, c);
}

static void compose_damage(void) {
    if (damage_count > 8 || damage_overflow)
        damage_merge_all();
    if (!damage_count)
        return;

    uint32_t t0 = gfx_now_us();
    fb_begin_frame();
    struct framebuffer *fb = fb_get();
    uint32_t tb = taskbar_h();
    uint32_t tby = (uint32_t)fb->height - tb;

    int order[MAX_WINS];
    int n = 0;
    for (int i = 0; i < MAX_WINS; i++)
        if (wins[i].open && !wins[i].minimized)
            order[n++] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (wins[order[j]].z < wins[order[i]].z) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }

    for (int di = 0; di < damage_count; di++) {
        uint32_t x = damage_list[di].x, y = damage_list[di].y;
        uint32_t w = damage_list[di].w, h = damage_list[di].h;
        fill_desk_rect(x, y, w, h);
        for (int k = 0; k < n; k++) {
            int i = order[k];
            if (!rects_overlap(wins[i].x, wins[i].y, wins[i].w, wins[i].h, x, y, w, h))
                continue;
            compose_win(i);
        }
        draw_proto_surfaces(x, y, w, h, 1);
        if (rects_overlap(0, tby, (uint32_t)fb->width, tb, x, y, w, h))
            draw_taskbar();
        if (menu_open)
            draw_start_menu();
        if (ctx_menu)
            draw_ctx_menu();
        if (alttab_open)
            draw_alttab();
        if (help_open)
            draw_help();
        if (session_lock || power_confirm)
            draw_session_overlays();
        {
            uint32_t nx, ny, nw, nh;
            notify_bounds((uint32_t)fb->width, &nx, &ny, &nw, &nh);
            if (rects_overlap(nx, ny, nw, nh, x, y, w, h))
                notify_draw((uint32_t)fb->width, (uint32_t)fb->height);
        }
    }
    fb_cancel_frame();
    sysmon_note_compose_us(gfx_now_us() - t0);
}

static int try_partial_present(void) {
    if (!fb_backbuffer_ok() || !scene_ready || menu_open || session_lock || power_confirm ||
        alttab_open || help_open || ctx_menu)
        return 0;
    if (dirty_bits & (DIRTY_FULL | DIRTY_MOVE))
        return 0;

    int bits = dirty_bits;
    int soft = DIRTY_TERM | DIRTY_CLOCK | DIRTY_MONITOR | DIRTY_GAME | DIRTY_TOAST |
               DIRTY_BROWSER | DIRTY_WIN;
    if (!bits || (bits & ~soft))
        return 0;

    int mi = -1, ti = -1, gi = -1, bi = -1, wi = -1;
    if (bits & DIRTY_MONITOR) {
        mi = find_win(APP_MONITOR);
        if (mi < 0 || !win_unobscured(mi))
            return 0;
    }
    if (bits & DIRTY_TERM) {
        ti = (focus >= 0 && wins[focus].kind == APP_TERM) ? focus : active_term;
        if (ti < 0 || ti >= MAX_WINS || !wins[ti].open || wins[ti].kind != APP_TERM ||
            !win_unobscured(ti))
            return 0;
    }
    if (bits & DIRTY_GAME) {
        gi = find_win(APP_GAME);
        if (gi < 0 || !win_unobscured(gi))
            return 0;
    }
    if (bits & DIRTY_BROWSER) {
        bi = find_win(APP_BROWSER);
        if (bi < 0 || !win_unobscured(bi))
            return 0;
    }
    if (bits & DIRTY_WIN) {
        wi = focus;
        if (wi < 0 || wi >= MAX_WINS || !wins[wi].open || wins[wi].minimized)
            return 0;
        if (wins[wi].kind != APP_FILES && wins[wi].kind != APP_SETTINGS &&
            wins[wi].kind != APP_AGENT)
            return 0;
        if (!win_unobscured(wi))
            return 0;
    }

    damage_clear();
    uint32_t t0 = gfx_now_us();
    fb_begin_frame();

    if (mi >= 0) {
        mark_win_surf_dirty(mi);
        compose_win(mi);
        damage_add_win(mi);
    }
    if (ti >= 0) {
        mark_win_surf_dirty(ti);
        compose_win(ti);
        damage_add_win(ti);
    }
    if (gi >= 0) {
        mark_win_surf_dirty(gi);
        compose_win(gi);
        damage_add_win(gi);
    }
    if (bi >= 0) {
        mark_win_surf_dirty(bi);
        compose_win(bi);
        damage_add_win(bi);
    }
    if (wi >= 0) {
        mark_win_surf_dirty(wi);
        compose_win(wi);
        damage_add_win(wi);
    }
    if (bits & DIRTY_CLOCK) {
        uint32_t cx, cy, cw, ch;
        clock_rect(&cx, &cy, &cw, &ch);
        draw_clock_area();
        damage_add(cx, cy, cw, ch);
    }
    if (bits & DIRTY_TOAST) {
        uint32_t nx, ny, nw, nh;
        notify_bounds((uint32_t)fb_get()->width, &nx, &ny, &nw, &nh);
        if (wallpaper_enabled())
            wallpaper_draw(nx, ny, nw, nh);
        else
            fb_fill_rect(nx, ny, nw, nh, C_bg());
        notify_draw((uint32_t)fb_get()->width, (uint32_t)fb_get()->height);
        damage_add(nx, ny, nw, nh);
    }

    fb_cancel_frame();
    sysmon_note_compose_us(gfx_now_us() - t0);
    present_scene(0);
    dirty_bits &= ~soft;
    damage_clear();
    return 1;
}

void desktop_draw(void) {
    /*
     * Compose into the back buffer (cursor-free), present, then overlay cursor
     * on the front. VBlank only on full presents — partial/cursor paths must
     * stay fast or the desktop freezes under mouse motion.
     */
    if ((dirty_bits & DIRTY_MOVE) && move_live && dragging && focus >= 0 &&
        wins[focus].open) {
        opaque_move_step(move_prev_x, move_prev_y,
                         wins[focus].x, wins[focus].y,
                         wins[focus].w, wins[focus].h);
        dirty_bits &= ~DIRTY_MOVE;
        return;
    }

    if (resizing && band_live && !move_live && focus >= 0) {
        damage_clear();
        if (move_prev_valid)
            damage_add(move_prev_x, move_prev_y, move_prev_w, move_prev_h);
        damage_add(band_x, band_y, band_w, band_h);
        compose_damage();
        fb_begin_frame();
        draw_rubber_band(band_x, band_y, band_w, band_h);
        fb_cancel_frame();
        present_scene(0);
        move_prev_x = band_x;
        move_prev_y = band_y;
        move_prev_w = band_w;
        move_prev_h = band_h;
        move_prev_valid = 1;
        dirty_bits &= ~DIRTY_MOVE;
        return;
    }

    if (try_partial_present())
        return;

    int use_bb = fb_backbuffer_ok();
    int do_full = (dirty_bits & DIRTY_FULL) || damage_overflow || !scene_ready || !use_bb;

    if (!do_full && (dirty_bits & DIRTY_MOVE) && damage_count > 0) {
        compose_damage();
        if (band_live && resizing)
            draw_rubber_band(band_x, band_y, band_w, band_h);
        scene_ready = 1;
        present_scene(0);
        if ((dragging || resizing) && focus >= 0 && wins[focus].open) {
            if (resizing && band_live) {
                move_prev_x = band_x;
                move_prev_y = band_y;
                move_prev_w = band_w;
                move_prev_h = band_h;
            } else {
                move_prev_x = wins[focus].x;
                move_prev_y = wins[focus].y;
                move_prev_w = wins[focus].w;
                move_prev_h = wins[focus].h;
            }
            move_prev_valid = 1;
        }
        dirty_bits = 0;
        damage_clear();
        return;
    }

    uint32_t t0 = gfx_now_us();
    if (use_bb)
        fb_begin_frame();

    draw_desktop_bg();
    draw_windows();
    draw_proto_surfaces(0, 0, (uint32_t)fb_get()->width, (uint32_t)fb_get()->height, 0);
    draw_taskbar();
    draw_start_menu();
    draw_ctx_menu();
    draw_alttab();
    draw_help();
    draw_session_overlays();
    notify_draw((uint32_t)fb_get()->width, (uint32_t)fb_get()->height);

    if (use_bb) {
        fb_cancel_frame();
        sysmon_note_compose_us(gfx_now_us() - t0);
        scene_ready = 1;
        present_scene(1);
    } else {
        scene_ready = 0;
        cursor_erase_front();
    }
    if ((dragging || resizing) && focus >= 0 && wins[focus].open) {
        if (resizing && band_live) {
            move_prev_x = band_x;
            move_prev_y = band_y;
            move_prev_w = band_w;
            move_prev_h = band_h;
        } else {
            move_prev_x = wins[focus].x;
            move_prev_y = wins[focus].y;
            move_prev_w = wins[focus].w;
            move_prev_h = wins[focus].h;
        }
        move_prev_valid = 1;
    }
    dirty_bits = 0;
    damage_clear();
}

static int desktop_should_exit;

static void handle_menu_click(int32_t mx, int32_t my) {
    struct framebuffer *fb = fb_get();
    uint32_t th = taskbar_h();
    uint32_t mw = U(180);
    uint32_t mh = U(320);
    uint32_t menux = U(8);
    uint32_t menuy = (uint32_t)fb->height - th - mh - U(4);
    if (!point_in(mx, my, menux, menuy, mw, mh)) {
        menu_open = 0;
        return;
    }
    int row = (int)((my - (int32_t)menuy - (int32_t)U(12)) / (int32_t)(fb_cell_h() + U(4)));
    menu_open = 0;
    if (row == 0)
        open_app(APP_TERM);
    else if (row == 1)
        open_app(APP_FILES);
    else if (row == 2)
        open_app(APP_SETTINGS);
    else if (row == 3)
        open_app(APP_AGENT);
    else if (row == 4)
        open_app(APP_GAME);
    else if (row == 5)
        open_app(APP_BROWSER);
    else if (row == 6)
        open_app(APP_MONITOR);
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

static void files_go_up(void) {
    char *slash = NULL;
    for (char *p = files_cwd; *p; p++)
        if (*p == '/')
            slash = p;
    if (!slash || slash == files_cwd) {
        files_cwd[0] = '/';
        files_cwd[1] = '\0';
    } else {
        *slash = '\0';
        if (!files_cwd[0]) {
            files_cwd[0] = '/';
            files_cwd[1] = '\0';
        }
    }
    files_sel = 0;
    files_scroll = 0;
}

static void files_new_file(void) {
    char path[VFS_PATH_MAX];
    for (int n = 1; n < 100; n++) {
        snprintf(path, sizeof(path), "%s/untitled%d.txt",
                 strcmp(files_cwd, "/") ? files_cwd : "", n);
        if (!strcmp(files_cwd, "/"))
            snprintf(path, sizeof(path), "/untitled%d.txt", n);
        if (!vfs_exists(path)) {
            vfs_write_file(path, "", 0);
            notify_push("Created file");
            dirty_bits |= DIRTY_FULL;
            return;
        }
    }
}

static void files_delete_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n)
        return;
    char path[VFS_PATH_MAX];
    if (!strcmp(files_cwd, "/"))
        snprintf(path, sizeof(path), "/%s", ents[files_sel].name);
    else
        snprintf(path, sizeof(path), "%s/%s", files_cwd, ents[files_sel].name);
    if (ents[files_sel].type == VFS_DIR)
        vfs_rmdir(path);
    else
        vfs_unlink(path);
    if (files_sel > 0)
        files_sel--;
    notify_push("Deleted");
    dirty_bits |= DIRTY_FULL;
}

static void files_rename_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n)
        return;
    char oldp[VFS_PATH_MAX], newp[VFS_PATH_MAX];
    if (!strcmp(files_cwd, "/"))
        snprintf(oldp, sizeof(oldp), "/%s", ents[files_sel].name);
    else
        snprintf(oldp, sizeof(oldp), "%s/%s", files_cwd, ents[files_sel].name);
    if (!strcmp(files_cwd, "/"))
        snprintf(newp, sizeof(newp), "/%s_renamed", ents[files_sel].name);
    else
        snprintf(newp, sizeof(newp), "%s/%s_renamed", files_cwd, ents[files_sel].name);
    vfs_rename(oldp, newp);
    notify_push("Renamed");
    dirty_bits |= DIRTY_FULL;
}

static void files_activate(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n)
        return;
    if (ents[files_sel].type == VFS_DIR) {
        char next[VFS_PATH_MAX];
        if (!strcmp(files_cwd, "/"))
            snprintf(next, sizeof(next), "/%s", ents[files_sel].name);
        else
            snprintf(next, sizeof(next), "%s/%s", files_cwd, ents[files_sel].name);
        size_t i = 0;
        for (; next[i] && i + 1 < sizeof(files_cwd); i++)
            files_cwd[i] = next[i];
        files_cwd[i] = '\0';
        files_sel = 0;
    } else {
        char cmd[VFS_PATH_MAX + 8];
        snprintf(cmd, sizeof(cmd), "cat %s/%s",
                 strcmp(files_cwd, "/") ? files_cwd : "", ents[files_sel].name);
        /* fix double slash for root */
        if (!strcmp(files_cwd, "/"))
            snprintf(cmd, sizeof(cmd), "cat /%s", ents[files_sel].name);
        else
            snprintf(cmd, sizeof(cmd), "cat %s/%s", files_cwd, ents[files_sel].name);
        open_app(APP_TERM);
        shell_execute(cmd);
    }
    dirty_bits |= DIRTY_FULL;
}

void desktop_init(void) {
    opaque_move_free();
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
    cursor_saved = 0;
    cursor_mx = cursor_my = -1;
    cursor_sprite_scale = 0;
    cursor_sprite_size = 0;
    files_sel = 0;
    files_scroll = 0;
    alttab_open = 0;
    help_open = 0;
    desktop_should_exit = 0;
    session_lock = 0;
    power_confirm = 0;
    active_term = -1;
    memset(terms, 0, sizeof(terms));
    agent_input_len = 0;
    agent_input[0] = '\0';
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
        fb_fill_rect(0, 0, (uint32_t)fb->width, (uint32_t)fb->height, C_bg());
        if (wallpaper_enabled())
            wallpaper_draw(0, 0, (uint32_t)fb->width, (uint32_t)fb->height);
        uint32_t mw = U(320);
        uint32_t mh = U(140);
        uint32_t mx = ((uint32_t)fb->width - mw) / 2;
        uint32_t my = ((uint32_t)fb->height - mh) / 3;
        fb_fill_rect(mx, my, mw, mh, C_surface());
        fb_fill_rect(mx, my, mw, U(3), C_accent());
        fb_draw_string(mx + U(24), my + U(24), "PeakOS", C_fg(), C_surface());
        fb_draw_string(mx + U(24), my + U(24) + fb_cell_h() + U(8),
                       "Press Enter to sign in", C_dim(), C_surface());
        fb_draw_string(mx + U(24), my + U(24) + 2 * (fb_cell_h() + U(8)),
                       "(single-user session)", C_dim(), C_surface());
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
    /* Consume the sign-in key so it cannot leak into Browser as Enter/Go. */
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
            mark_win_surf_dirty(find_win(APP_BROWSER));
        }
        /* Pull userspace GUI protocol damage into the compositor. */
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
        /* Idle lock (~5 min at 100Hz) — session convenience, not security. */
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

        /* Ctrl+Alt+Esc leaves desktop; Esc alone closes overlays */
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
                raise_win(order[alttab_sel]);
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
            term_activate(focus);
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
            if (agent_write_pending() && (key == 'y' || key == 'Y' || key == 'n' || key == 'N')) {
                agent_approve_write(key == 'y' || key == 'Y');
                notify_push((key == 'y' || key == 'Y') ? "Write approved" : "Write denied");
            } else if (key == '\n') {
                if (agent_input_len) {
                    agent_ask(agent_input);
                    clipboard_set(agent_input, agent_input_len);
                    agent_input_len = 0;
                    agent_input[0] = '\0';
                    notify_push("Agent running");
                }
            } else if (key == '\b' && agent_input_len) {
                agent_input[--agent_input_len] = '\0';
            } else if (key >= 32 && key < 127 && agent_input_len + 1 < sizeof(agent_input)) {
                agent_input[agent_input_len++] = (char)key;
                agent_input[agent_input_len] = '\0';
            }
            dirty_bits |= DIRTY_WIN;
            mark_focus_surf_dirty();
            key = 0;
        }

        if (key && files_focus) {
            if (key == 'n' || key == 'N')
                files_new_file();
            else if (key == 'd' || key == 'D')
                files_delete_sel();
            else if (key == 'r' || key == 'R')
                files_rename_sel();
            else if (key == 'u' || key == 'U') {
                files_go_up();
                dirty_bits |= DIRTY_FULL;
            } else if (key == '\n')
                files_activate();
            else if (key == 'j' || key == 'J' || key == KEY_DOWN) {
                files_sel++;
                dirty_bits |= DIRTY_WIN;
                mark_focus_surf_dirty();
            } else if ((key == 'k' || key == 'K' || key == KEY_UP) && files_sel > 0) {
                files_sel--;
                dirty_bits |= DIRTY_WIN;
                mark_focus_surf_dirty();
            }
            key = 0;
        }

        if (key && term_focus) {
            struct term_state *tt = term_active();
            if (key == KEY_TAB)
                key = '\t';
            if (key == KEY_UP) {
                tt->scroll++;
                tt->full_redraw = 1;
                dirty_bits |= DIRTY_TERM;
                mark_focus_surf_dirty();
            } else if (key == KEY_DOWN) {
                if (tt->scroll > 0)
                    tt->scroll--;
                tt->full_redraw = 1;
                dirty_bits |= DIRTY_TERM;
                mark_focus_surf_dirty();
            } else {
                shell_feed_key(key);
                dirty_bits |= DIRTY_TERM;
                mark_focus_surf_dirty();
            }
        } else if (key && game_focus && key < 128) {
            game_input((char)key);
            dirty_bits |= DIRTY_GAME;
            mark_focus_surf_dirty();
        } else if (key && br_focus && key < 128) {
            browser_input((char)key);
            dirty_bits |= DIRTY_BROWSER;
            mark_focus_surf_dirty();
        } else if (key && mon_focus && key < 128) {
            monitor_input((char)key);
            dirty_bits |= DIRTY_MONITOR;
            mark_focus_surf_dirty();
        } else if (key == '1')
            open_app(APP_TERM);
        else if (key == '2')
            open_app(APP_FILES);
        else if (key == '3')
            open_app(APP_SETTINGS);
        else if (key == '4')
            open_app(APP_AGENT);
        else if (key == '5')
            open_app(APP_GAME);
        else if (key == '6')
            open_app(APP_BROWSER);
        else if (key == '7')
            open_app(APP_MONITOR);
        else if (key == 't' || key == 'T') {
            theme_next();
            theme_persist();
            dirty_bits |= DIRTY_FULL;
        } else if (key == 's' || key == 'S') {
            settings_cycle_gui_scale();
            settings_persist();
            rescale_windows();
            dirty_bits |= DIRTY_FULL;
        }

        struct mouse_state m;
        mouse_poll(&m);

        notify_tick();
        if (notify_consume_dirty())
            dirty_bits |= DIRTY_TOAST;

        if (m.wheel) {
            if (term_focus) {
                struct term_state *tt = term_active();
                tt->scroll += m.wheel > 0 ? 3 : -3;
                if (tt->scroll < 0)
                    tt->scroll = 0;
                tt->full_redraw = 1;
                dirty_bits |= DIRTY_TERM;
                mark_focus_surf_dirty();
            } else if (files_focus) {
                files_sel += m.wheel > 0 ? -1 : 1;
                if (files_sel < 0)
                    files_sel = 0;
                dirty_bits |= DIRTY_WIN;
                mark_focus_surf_dirty();
            } else if (br_focus) {
                browser_input(m.wheel > 0 ? 'k' : 'j');
                dirty_bits |= DIRTY_BROWSER;
                mark_focus_surf_dirty();
            } else if (mon_focus) {
                monitor_input(m.wheel > 0 ? '[' : ']');
                dirty_bits |= DIRTY_MONITOR;
                mark_focus_surf_dirty();
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

            uint32_t th = taskbar_h();
            uint32_t ty = (uint32_t)fb->height - th;

            if (ctx_menu) {
                uint32_t mw = U(140);
                uint32_t mh = U(90);
                if (point_in(m.x, m.y, (uint32_t)ctx_x, (uint32_t)ctx_y, mw, mh)) {
                    int row = (int)((m.y - ctx_y - (int32_t)U(10)) /
                                    (int32_t)(fb_cell_h() + U(4)));
                    if (row == 0)
                        open_app(APP_TERM);
                    else if (row == 1)
                        open_app(APP_FILES);
                    else if (row == 2)
                        open_app(APP_SETTINGS);
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

            if (point_in(m.x, m.y, U(8), ty, U(60), th)) {
                menu_open = !menu_open;
                dirty_bits |= DIRTY_FULL;
            } else if (menu_open) {
                handle_menu_click(m.x, m.y);
                dirty_bits |= DIRTY_FULL;
            } else if (point_in(m.x, m.y, U(70), ty, (uint32_t)fb->width - U(180), th)) {
                /* Taskbar window buttons */
                uint32_t bx = U(70);
                uint32_t bw = taskbar_btn_w();
                for (int i = 0; i < MAX_WINS; i++) {
                    if (!wins[i].open)
                        continue;
                    if (point_in(m.x, m.y, bx, ty, bw - U(4), th)) {
                        if (wins[i].minimized || focus != i) {
                            wins[i].minimized = 0;
                            raise_win(i);
                        } else {
                            minimize_win(i);
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
                    uint32_t by = w->y + U(6);
                    uint32_t bs = U(14);
                    uint32_t gap = U(4);
                    uint32_t close_x = w->x + w->w - U(22);
                    uint32_t max_x = close_x - bs - gap;
                    uint32_t min_x = max_x - bs - gap;
                    if (point_in(m.x, m.y, close_x, by, bs, bs)) {
                        close_win(i);
                        break;
                    }
                    if (point_in(m.x, m.y, max_x, by, bs, bs)) {
                        raise_win(i);
                        maximize_win(i);
                        break;
                    }
                    if (point_in(m.x, m.y, min_x, by, bs, bs)) {
                        minimize_win(i);
                        break;
                    }
                    if (point_in(m.x, m.y, w->x, w->y, w->w, w->h)) {
                        raise_win(i);
                        int edge = w->maximized ? 0 : hit_resize_edge(w, m.x, m.y);
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
                        } else if (point_in(m.x, m.y, w->x, w->y, w->w, title_h())) {
                            if (dbl) {
                                maximize_win(i);
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
                                opaque_move_begin(i);
                            }
                        } else if (w->kind == APP_SETTINGS) {
                            uint32_t ch = fb_cell_h();
                            uint32_t pad = U(12);
                            uint32_t row_h = ch + U(4);
                            uint32_t content_w = w->w > pad * 2 ? w->w - pad * 2 : w->w;
                            uint32_t tab_w = content_w / SETTINGS_PAGES;
                            if (tab_w < U(56))
                                tab_w = U(56);
                            uint32_t tabs_y = w->y + title_h() + pad;
                            uint32_t tabs_h = ch + U(6);
                            uint32_t body_y = tabs_y + ch + U(16);
                            if (point_in(m.x, m.y, w->x + pad, tabs_y,
                                         tab_w * SETTINGS_PAGES, tabs_h)) {
                                int tab = (int)((m.x - (int32_t)(w->x + pad)) / (int32_t)tab_w);
                                if (tab >= 0 && tab < SETTINGS_PAGES)
                                    settings_page = tab;
                            } else if (settings_page == 0) {
                                /* row 0–1: scale */
                                if (m.y >= (int32_t)body_y &&
                                    m.y < (int32_t)(body_y + row_h * 2)) {
                                    settings_cycle_gui_scale();
                                    settings_persist();
                                    rescale_windows();
                                }
                            } else if (settings_page == 1) {
                                int row = (int)((m.y - (int32_t)body_y) / (int32_t)row_h);
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
                                if (m.y >= (int32_t)body_y &&
                                    m.y < (int32_t)(body_y + row_h * 2)) {
                                    settings_toggle_clock();
                                    settings_persist();
                                }
                            }
                            dirty_bits |= DIRTY_FULL;
                        } else if (w->kind == APP_FILES) {
                            uint32_t ch = fb_cell_h();
                            uint32_t content_y = w->y + title_h() + U(8) + ch * 2 + U(4);
                            int row = (int)((m.y - (int32_t)content_y) / (int32_t)ch);
                            struct vfs_dirent ents[FILES_ROWS];
                            int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
                            if (row >= 0 && n > 0) {
                                files_sel = files_scroll + row;
                                if (files_sel >= n)
                                    files_sel = n - 1;
                                if (dbl)
                                    files_activate();
                                else
                                    dirty_bits |= DIRTY_WIN;
                            }
                        } else if (w->kind == APP_BROWSER) {
                            browser_click(m.x - (int32_t)(w->x + U(4)),
                                          m.y - (int32_t)(w->y + title_h() + U(2)),
                                          w->w - U(8), w->h - title_h() - U(8));
                            dirty_bits |= DIRTY_BROWSER;
                        } else if (w->kind == APP_AGENT) {
                            if (agent_input_len)
                                agent_ask(agent_input);
                            else
                                agent_ask("summarize workspace README");
                            notify_push("Agent running");
                            dirty_bits |= DIRTY_WIN;
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
                /* Snap half-screen */
                if (focus >= 0 && m.x < (int32_t)U(8)) {
                    wins[focus].x = 0;
                    wins[focus].y = 0;
                    wins[focus].w = (uint32_t)fb->width / 2;
                    wins[focus].h = (uint32_t)fb->height - taskbar_h();
                    wins[focus].maximized = 0;
                } else if (focus >= 0 && m.x > (int32_t)fb->width - (int32_t)U(8)) {
                    wins[focus].x = (uint32_t)fb->width / 2;
                    wins[focus].y = 0;
                    wins[focus].w = (uint32_t)fb->width / 2;
                    wins[focus].h = (uint32_t)fb->height - taskbar_h();
                    wins[focus].maximized = 0;
                }
                if (move_live)
                    opaque_move_end();
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
                clamp_win_geom(&wins[focus]);
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
            clamp_win_geom(&wins[focus]);
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
            if (nw < (int32_t)win_min_w())
                nw = (int32_t)win_min_w();
            if (nh < (int32_t)win_min_h())
                nh = (int32_t)win_min_h();
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            /* Preview only — win geom commits on release. */
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
                clamp_win_geom(&tmp);
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

        if (find_win(APP_GAME) >= 0 && timer_ticks() - last_game_tick >= 5) {
            last_game_tick = timer_ticks();
            game_tick();
            dirty_bits |= DIRTY_GAME;
            mark_win_surf_dirty(find_win(APP_GAME));
        }

        sysmon_poll();
        if (find_win(APP_MONITOR) >= 0 && timer_ticks() - last_mon_tick >= 50) {
            last_mon_tick = timer_ticks();
            monitor_tick();
            dirty_bits |= DIRTY_MONITOR;
            mark_win_surf_dirty(find_win(APP_MONITOR));
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
        draw_cursor(m.x, m.y);

        if (!dirty_bits) {
            sysmon_idle_enter();
            hlt();
            sysmon_idle_leave();
        }
    }
}
