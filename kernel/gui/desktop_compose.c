#include "desktop_internal.h"
#include "gui.h"
#include "fb.h"
#include "display.h"
#include "sysmon.h"
#include "surface.h"
#include "heap.h"
#include "guiproto.h"
#include "wallpaper.h"
#include "notify.h"
#include "util.h"

int dirty_bits;
int scene_ready;
int32_t cursor_mx = -1, cursor_my = -1;
uint64_t last_clock_secs = (uint64_t)-1;

static uint32_t cursor_backup[CURSOR_MAX][CURSOR_MAX];
static int cursor_saved;
static int32_t last_cx, last_cy;
static uint32_t last_csize;
static uint32_t cursor_sprite[CURSOR_MAX * CURSOR_MAX];
static uint32_t cursor_sprite_scale;
static uint32_t cursor_sprite_size;

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

static uint32_t gfx_now_us(void) {
    return (uint32_t)(gfx_rdtsc() / 2000ull);
}

void desktop_mark_focus_surf_dirty(void) {
    if (focus >= 0 && focus < MAX_WINS && wins[focus].open)
        surface_mark_dirty(&wins[focus].surf);
}

void desktop_mark_win_surf_dirty(int idx) {
    if (idx >= 0 && idx < MAX_WINS && wins[idx].open)
        surface_mark_dirty(&wins[idx].surf);
}

void desktop_mark_win_surf_dirty_rect(int idx, uint32_t x, uint32_t y,
                                      uint32_t w, uint32_t h) {
    if (idx >= 0 && idx < MAX_WINS && wins[idx].open)
        surface_mark_dirty_rect(&wins[idx].surf, x, y, w, h);
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

void desktop_cursor_erase_front(void) {
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

void desktop_draw_cursor(int32_t x, int32_t y) {
    uint32_t s = fb_ui_scale();
    uint32_t size = 16 * s;
    if (size > CURSOR_MAX)
        size = CURSOR_MAX;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    if (cursor_saved && last_cx == x && last_cy == y && last_csize == size)
        return;

    desktop_cursor_erase_front();

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

static void present_scene(int full_present) {
    if (!fb_backbuffer_ok())
        return;

    desktop_cursor_erase_front();

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

static int rects_overlap(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah,
                         uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

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
    desktop_draw_win_content(i);
    fb_set_draw_target(NULL, 0, 0, 0);
    w->x = ox;
    w->y = oy;
}

static void compose_win(int i, uint32_t cx, uint32_t cy, uint32_t cw, uint32_t ch) {
    struct win *w = &wins[i];
    if (!w->open || w->minimized)
        return;
    int clip = cw > 0 && ch > 0;
    if (w->surf.px && surface_is_dirty(&w->surf)) {
        paint_win_to_surface(i);
        surface_blit_damage(&w->surf, w->x, w->y, cx, cy, clip ? cw : 0, clip ? ch : 0);
        surface_damage_clear(&w->surf);
    } else if (w->surf.px) {
        surface_blit_damage(&w->surf, w->x, w->y, cx, cy, clip ? cw : 0, clip ? ch : 0);
    } else {
        desktop_draw_win_content(i);
    }
}

static void draw_windows(void) {
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
        compose_win(order[k], 0, 0, 0, 0);
    }
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
    if (c->clip)
        surface_blit_damage(s, x, y, c->dx, c->dy, c->dw, c->dh);
    else
        surface_blit_damage(s, x, y, 0, 0, 0, 0);
}

static void draw_proto_surfaces(uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, int clip) {
    struct proto_blit_ctx c = { dx, dy, dw, dh, clip };
    guiproto_for_each_surface(proto_blit_cb, &c);
}

static void fill_desk_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct framebuffer *fb = fb_get();
    uint32_t tb = desktop_taskbar_h();
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
        fb_fill_rect(x, y, w, h, desktop_color_bg());
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
        compose_win(i, x, y, w, h);
    }
}

void desktop_opaque_move_free(void) {
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

void desktop_opaque_move_begin(int idx) {
    desktop_opaque_move_free();
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
        desktop_opaque_move_free();
        return;
    }
    move_pw = w->w;
    move_ph = w->h;
    fb_copy_from_back(w->x, w->y, w->w, w->h, move_pixmap, w->w);
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

void desktop_opaque_move_end(void) {
    desktop_opaque_move_free();
    dirty_bits |= DIRTY_FULL;
}

static void draw_rubber_band(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t c = desktop_color_accent();
    uint32_t t = desktop_u(2);
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
    uint32_t tb = desktop_taskbar_h();
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
            compose_win(i, x, y, w, h);
        }
        draw_proto_surfaces(x, y, w, h, 1);
        if (rects_overlap(0, tby, (uint32_t)fb->width, tb, x, y, w, h))
            desktop_draw_taskbar();
        if (menu_open)
            desktop_draw_start_menu();
        if (ctx_menu)
            desktop_draw_ctx_menu();
        if (alttab_open)
            desktop_draw_alttab();
        if (help_open)
            desktop_draw_help();
        if (session_lock || power_confirm)
            desktop_draw_session_overlays();
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
        mi = desktop_find_win(APP_MONITOR);
        if (mi < 0 || !win_unobscured(mi))
            return 0;
    }
    if (bits & DIRTY_TERM) {
        ti = (focus >= 0 && wins[focus].kind == APP_TERM) ? focus : desktop_active_term_index();
        if (ti < 0 || ti >= MAX_WINS || !wins[ti].open || wins[ti].kind != APP_TERM ||
            !win_unobscured(ti))
            return 0;
    }
    if (bits & DIRTY_GAME) {
        gi = desktop_find_win(APP_GAME);
        if (gi < 0 || !win_unobscured(gi))
            return 0;
    }
    if (bits & DIRTY_BROWSER) {
        bi = desktop_find_win(APP_BROWSER);
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
        compose_win(mi, 0, 0, 0, 0);
        damage_add_win(mi);
    }
    if (ti >= 0) {
        compose_win(ti, 0, 0, 0, 0);
        damage_add_win(ti);
    }
    if (gi >= 0) {
        compose_win(gi, 0, 0, 0, 0);
        damage_add_win(gi);
    }
    if (bi >= 0) {
        compose_win(bi, 0, 0, 0, 0);
        damage_add_win(bi);
    }
    if (wi >= 0) {
        compose_win(wi, 0, 0, 0, 0);
        damage_add_win(wi);
    }
    if (bits & DIRTY_CLOCK) {
        uint32_t cx, cy, cw, ch;
        desktop_clock_rect(&cx, &cy, &cw, &ch);
        desktop_draw_clock_area();
        damage_add(cx, cy, cw, ch);
    }
    if (bits & DIRTY_TOAST) {
        uint32_t nx, ny, nw, nh;
        notify_bounds((uint32_t)fb_get()->width, &nx, &ny, &nw, &nh);
        if (wallpaper_enabled())
            wallpaper_draw(nx, ny, nw, nh);
        else
            fb_fill_rect(nx, ny, nw, nh, desktop_color_bg());
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

    desktop_draw_desktop_bg();
    draw_windows();
    draw_proto_surfaces(0, 0, (uint32_t)fb_get()->width, (uint32_t)fb_get()->height, 0);
    desktop_draw_taskbar();
    desktop_draw_start_menu();
    desktop_draw_ctx_menu();
    desktop_draw_alttab();
    desktop_draw_help();
    desktop_draw_session_overlays();
    notify_draw((uint32_t)fb_get()->width, (uint32_t)fb_get()->height);

    if (use_bb) {
        fb_cancel_frame();
        sysmon_note_compose_us(gfx_now_us() - t0);
        scene_ready = 1;
        present_scene(1);
    } else {
        scene_ready = 0;
        desktop_cursor_erase_front();
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

void desktop_compose_reset_cursor_cache(void) {
    cursor_saved = 0;
    cursor_mx = cursor_my = -1;
    cursor_sprite_scale = 0;
    cursor_sprite_size = 0;
}
