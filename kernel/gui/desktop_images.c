#include "desktop_internal.h"
#include "img_decode.h"
#include "fb.h"
#include "keyboard.h"
#include "vfs.h"
#include "clipboard.h"
#include "notify.h"
#include "util.h"

#define IMG_MODE_FIT    0
#define IMG_MODE_ACTUAL 1
#define IMG_MODE_ZOOM   2
#define IMG_NAME_MAX    64

static char img_path[VFS_PATH_MAX];
static char img_dir[VFS_PATH_MAX];
static struct img_decoded img;
static int img_loaded;
static int img_mode = IMG_MODE_FIT;
static int32_t img_pan_x;
static int32_t img_pan_y;
static int img_zoom = 100; /* percent */
static int img_index;      /* 1-based in sorted dir list */
static int img_total;
static int img_dragging;
static int32_t img_drag_last_x;
static int32_t img_drag_last_y;
static char img_format[8];

static void img_mark_dirty(void) {
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static void img_clear(void) {
    img_decode_free(&img);
    img_loaded = 0;
    img_path[0] = img_dir[0] = '\0';
    img_pan_x = img_pan_y = 0;
    img_zoom = 100;
    img_mode = IMG_MODE_FIT;
    img_index = img_total = 0;
    img_dragging = 0;
    img_format[0] = '\0';
}

void desktop_images_init(void) {
    img_clear();
}

static void img_split_dir(const char *path) {
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(img_dir); i++)
        img_dir[i] = path[i];
    img_dir[i] = '\0';
    char *slash = NULL;
    for (char *p = img_dir; *p; p++)
        if (*p == '/')
            slash = p;
    if (slash)
        *slash = '\0';
    else {
        img_dir[0] = '/';
        img_dir[1] = '\0';
    }
}

static int img_char_eq_ci(char a, char b) {
    if (a >= 'A' && a <= 'Z')
        a = (char)(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z')
        b = (char)(b + ('a' - 'A'));
    return a == b;
}

static int img_name_eq_ci(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    for (; *a && *b; a++, b++)
        if (!img_char_eq_ci(*a, *b))
            return 0;
    return *a == *b;
}

static const char *img_format_label(void) {
    if (img_format[0])
        return img_format;
    if (!img_path[0])
        return "";
    size_t n = strlen(img_path);
    if (n >= 4 && img_name_eq_ci(img_path + n - 4, ".ppm"))
        return "PPM P6";
    if (n >= 4 && img_name_eq_ci(img_path + n - 4, ".bmp"))
        return "BMP 24";
    return "unknown";
}

static void img_set_format_from_path(const char *path) {
    img_format[0] = '\0';
    if (!path)
        return;
    size_t n = strlen(path);
    if (n >= 4 && img_name_eq_ci(path + n - 4, ".ppm"))
        snprintf(img_format, sizeof(img_format), "PPM P6");
    else if (n >= 4 && img_name_eq_ci(path + n - 4, ".bmp"))
        snprintf(img_format, sizeof(img_format), "BMP 24");
}

static int img_is_image_name(const char *name) {
    size_t n = strlen(name);
    if (n < 4)
        return 0;
    return img_name_eq_ci(name + n - 4, ".ppm") || img_name_eq_ci(name + n - 4, ".bmp");
}

static int img_collect_sorted(char names[][IMG_NAME_MAX], int max) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(img_dir[0] ? img_dir : "/", ents, FILES_ROWS);
    if (n <= 0)
        return 0;
    int ni = 0;
    for (int i = 0; i < n && ni < max; i++) {
        if (ents[i].type != VFS_FILE || !img_is_image_name(ents[i].name))
            continue;
        size_t nl = strlen(ents[i].name);
        if (nl >= IMG_NAME_MAX)
            nl = IMG_NAME_MAX - 1;
        memcpy(names[ni], ents[i].name, nl);
        names[ni][nl] = '\0';
        ni++;
    }
    for (int i = 0; i < ni; i++)
        for (int j = i + 1; j < ni; j++)
            if (strcmp(names[i], names[j]) > 0) {
                char tmp[IMG_NAME_MAX];
                memcpy(tmp, names[i], sizeof(tmp));
                memcpy(names[i], names[j], sizeof(tmp));
                memcpy(names[j], tmp, sizeof(tmp));
            }
    return ni;
}

static void img_refresh_index(void) {
    char names[FILES_ROWS][IMG_NAME_MAX];
    img_total = img_collect_sorted(names, FILES_ROWS);
    img_index = 0;
    if (!img_total || !img_path[0])
        return;
    const char *base = img_path;
    for (const char *p = img_path; *p; p++)
        if (*p == '/')
            base = p + 1;
    for (int i = 0; i < img_total; i++)
        if (img_name_eq_ci(names[i], base)) {
            img_index = i + 1;
            return;
        }
}

static int img_load(const char *path) {
    if (!path || !path[0])
        return -1;
    struct img_decoded next;
    memset(&next, 0, sizeof(next));
    if (img_decode_file(path, &next) != 0)
        return -1;
    img_decode_free(&img);
    img = next;
    img_loaded = 1;
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(img_path); i++)
        img_path[i] = path[i];
    img_path[i] = '\0';
    img_set_format_from_path(img_path);
    img_split_dir(img_path);
    img_pan_x = img_pan_y = 0;
    img_zoom = 100;
    img_mode = IMG_MODE_FIT;
    img_refresh_index();
    return 0;
}

void desktop_images_open(const char *path) {
    int slot = desktop_open_app(APP_IMAGES);
    if (slot < 0)
        return;
    if (img_load(path) != 0) {
        notify_push("Could not load image");
        dirty_bits |= DIRTY_TOAST;
        return;
    }
    img_mark_dirty();
}

static int img_nav(int dir) {
    char names[FILES_ROWS][IMG_NAME_MAX];
    int ni = img_collect_sorted(names, FILES_ROWS);
    if (ni <= 1)
        return 0;
    const char *base = img_path;
    for (const char *p = img_path; *p; p++)
        if (*p == '/')
            base = p + 1;
    int cur = -1;
    for (int i = 0; i < ni; i++)
        if (img_name_eq_ci(names[i], base)) {
            cur = i;
            break;
        }
    int next = (cur < 0 ? 0 : cur + dir);
    if (next < 0)
        next = ni - 1;
    if (next >= ni)
        next = 0;
    char p[VFS_PATH_MAX];
    if (!strcmp(img_dir, "/"))
        snprintf(p, sizeof(p), "/%s", names[next]);
    else
        snprintf(p, sizeof(p), "%s/%s", img_dir, names[next]);
    if (img_load(p) != 0)
        return 0;
    img_mark_dirty();
    return 1;
}

static uint32_t img_status_h(void) {
    return fb_cell_h() + desktop_u(4);
}

static void img_viewport_geom(struct win *w, uint32_t *tx, uint32_t *ty, uint32_t *cw, uint32_t *ch) {
    uint32_t th = desktop_title_h();
    if (tx)
        *tx = w->x + desktop_u(8);
    if (ty)
        *ty = w->y + th + desktop_u(6) + fb_cell_h() + desktop_u(4);
    if (cw)
        *cw = w->w > desktop_u(16) ? w->w - desktop_u(16) : w->w;
    if (ch) {
        uint32_t status = img_status_h();
        *ch = w->h > th + desktop_u(20) + status
                  ? w->h - th - desktop_u(20) - status
                  : desktop_u(40);
    }
}

static int img_can_pan(void) {
    return img_loaded && img_mode != IMG_MODE_FIT;
}

static void img_scaled_size(uint32_t *sw, uint32_t *sh) {
    if (!sw || !sh)
        return;
    if (img_mode == IMG_MODE_ACTUAL) {
        *sw = img.w;
        *sh = img.h;
    } else if (img_mode == IMG_MODE_ZOOM) {
        *sw = (uint32_t)((uint64_t)img.w * (uint32_t)img_zoom / 100);
        *sh = (uint32_t)((uint64_t)img.h * (uint32_t)img_zoom / 100);
        if (*sw < 1)
            *sw = 1;
        if (*sh < 1)
            *sh = 1;
    } else {
        *sw = 0;
        *sh = 0;
    }
}

/* Keep scaled image intersecting the viewport (avoids uint32 wrap + huge loops). */
static void img_clamp_pan(uint32_t cw, uint32_t ch) {
    uint32_t sw, sh;
    if (!img_can_pan()) {
        img_pan_x = img_pan_y = 0;
        return;
    }
    img_scaled_size(&sw, &sh);
    if (sw == 0 || sh == 0) {
        img_pan_x = img_pan_y = 0;
        return;
    }
    {
        int32_t min_x = 1 - (int32_t)sw;
        int32_t max_x = (int32_t)cw - 1;
        int32_t min_y = 1 - (int32_t)sh;
        int32_t max_y = (int32_t)ch - 1;
        if (cw == 0)
            min_x = max_x = 0;
        if (ch == 0)
            min_y = max_y = 0;
        if (img_pan_x < min_x)
            img_pan_x = min_x;
        if (img_pan_x > max_x)
            img_pan_x = max_x;
        if (img_pan_y < min_y)
            img_pan_y = min_y;
        if (img_pan_y > max_y)
            img_pan_y = max_y;
    }
}

static void img_pan_by(int32_t dx, int32_t dy) {
    if (!img_can_pan())
        return;
    img_pan_x += dx;
    img_pan_y += dy;
    {
        int wi = desktop_find_win(APP_IMAGES);
        if (wi >= 0) {
            uint32_t tx, ty, cw, ch;
            img_viewport_geom(&wins[wi], &tx, &ty, &cw, &ch);
            img_clamp_pan(cw, ch);
        }
    }
    img_mark_dirty();
}

/* Draw scaled image clipped to viewport [vx,vy)+[vw,vh); x0/y0 may be negative. */
static void img_draw_scaled(struct win *w, int32_t x0, int32_t y0,
                            uint32_t vx, uint32_t vy, uint32_t vw, uint32_t vh,
                            uint32_t sw, uint32_t sh) {
    int32_t x1, y1, x2, y2;
    if (!img.rgb || sw == 0 || sh == 0 || vw == 0 || vh == 0)
        return;
    x1 = x0;
    y1 = y0;
    x2 = x0 + (int32_t)sw;
    y2 = y0 + (int32_t)sh;
    if (x1 < (int32_t)vx)
        x1 = (int32_t)vx;
    if (y1 < (int32_t)vy)
        y1 = (int32_t)vy;
    if (x2 > (int32_t)(vx + vw))
        x2 = (int32_t)(vx + vw);
    if (y2 > (int32_t)(vy + vh))
        y2 = (int32_t)(vy + vh);
    if (x1 >= x2 || y1 >= y2)
        return;
    for (int32_t py = y1; py < y2; py++) {
        uint32_t dy = (uint32_t)(py - y0);
        uint32_t sy = (dy * img.h) / sh;
        if (sy >= img.h)
            sy = img.h - 1;
        const uint8_t *srow = img.rgb + (size_t)sy * (size_t)img.w * 3;
        for (int32_t px = x1; px < x2; px++) {
            uint32_t dx = (uint32_t)(px - x0);
            uint32_t sx = (dx * img.w) / sw;
            if (sx >= img.w)
                sx = img.w - 1;
            const uint8_t *p = srow + (size_t)sx * 3;
            fb_put_pixel((uint32_t)px, (uint32_t)py, fb_rgb(p[0], p[1], p[2]));
        }
    }
    (void)w;
}

void desktop_images_draw(struct win *w) {
    uint32_t th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(8);
    uint32_t ty = w->y + th + desktop_u(6);
    uint32_t cw = w->w > desktop_u(16) ? w->w - desktop_u(16) : w->w;
    const char *show = img_path[0] ? img_path : "No image";
    for (const char *p = img_path; *p; p++)
        if (*p == '/')
            show = p + 1;
    char hdr[96];
    if (img_loaded)
        snprintf(hdr, sizeof(hdr), "%s  %ux%u", show, (unsigned)img.w, (unsigned)img.h);
    else
        snprintf(hdr, sizeof(hdr), "%s", show);
    fb_draw_string_fit(tx, ty, cw, hdr, desktop_color_accent(), desktop_color_bg());

    uint32_t vty, vch;
    img_viewport_geom(w, &tx, &vty, &cw, &vch);
    if (!img_loaded) {
        fb_draw_string(tx, vty, "Open .ppm or .bmp from Files", desktop_color_dim(), desktop_color_bg());
    } else {
        fb_fill_rect(tx, vty, cw, vch, desktop_color_surface());
        if (img_mode == IMG_MODE_FIT) {
            uint32_t sw = cw, sh = vch;
            if (img.w && img.h) {
                if ((uint64_t)img.w * vch > (uint64_t)img.h * cw) {
                    sw = (uint32_t)((uint64_t)img.w * vch / img.h);
                    sh = vch;
                } else {
                    sw = cw;
                    sh = (uint32_t)((uint64_t)img.h * cw / img.w);
                }
            }
            {
                int32_t ox = (int32_t)tx + (cw > sw ? (int32_t)((cw - sw) / 2) : 0);
                int32_t oy = (int32_t)vty + (vch > sh ? (int32_t)((vch - sh) / 2) : 0);
                img_draw_scaled(w, ox, oy, tx, vty, cw, vch, sw, sh);
            }
        } else if (img_mode == IMG_MODE_ACTUAL) {
            /* Offset source by pan so negative pan reveals lower-right of image. */
            img_clamp_pan(cw, vch);
            {
                int32_t ox = (int32_t)tx + img_pan_x;
                int32_t oy = (int32_t)vty + img_pan_y;
                uint32_t src_x0 = 0, src_y0 = 0;
                int32_t dx0 = ox, dy0 = oy;
                uint32_t draw_w, draw_h;
                if (dx0 < (int32_t)tx) {
                    src_x0 = (uint32_t)((int32_t)tx - dx0);
                    dx0 = (int32_t)tx;
                }
                if (dy0 < (int32_t)vty) {
                    src_y0 = (uint32_t)((int32_t)vty - dy0);
                    dy0 = (int32_t)vty;
                }
                draw_w = img.w > src_x0 ? img.w - src_x0 : 0;
                draw_h = img.h > src_y0 ? img.h - src_y0 : 0;
                if (dx0 + (int32_t)draw_w > (int32_t)(tx + cw))
                    draw_w = (uint32_t)((int32_t)(tx + cw) - dx0);
                if (dy0 + (int32_t)draw_h > (int32_t)(vty + vch))
                    draw_h = (uint32_t)((int32_t)(vty + vch) - dy0);
                for (uint32_t dy = 0; dy < draw_h; dy++) {
                    for (uint32_t dx = 0; dx < draw_w; dx++) {
                        const uint8_t *p =
                            img.rgb + ((size_t)(src_y0 + dy) * img.w + (src_x0 + dx)) * 3;
                        fb_put_pixel((uint32_t)dx0 + dx, (uint32_t)dy0 + dy,
                                     fb_rgb(p[0], p[1], p[2]));
                    }
                }
            }
        } else {
            uint32_t sw, sh;
            img_clamp_pan(cw, vch);
            img_scaled_size(&sw, &sh);
            img_draw_scaled(w, (int32_t)tx + img_pan_x, (int32_t)vty + img_pan_y,
                            tx, vty, cw, vch, sw, sh);
        }
    }

    uint32_t sy = vty + vch + desktop_u(2);
    uint32_t sh = img_status_h();
    fb_fill_rect(tx, sy, cw, sh, desktop_color_surface());
    char status[128];
    if (img_loaded) {
        const char *mode = img_mode == IMG_MODE_FIT ? "fit"
                         : img_mode == IMG_MODE_ACTUAL ? "1:1" : "zoom";
        const char *fmt = img_format_label();
        if (img_total > 1)
            snprintf(status, sizeof(status), "%d/%d  %s  %s %d%%  [/] nav  0 fit  1 actual",
                     img_index, img_total, fmt, mode, img_zoom);
        else
            snprintf(status, sizeof(status), "%s  %s %d%%  +/- zoom  0 fit  1 actual",
                     fmt, mode, img_zoom);
    } else {
        snprintf(status, sizeof(status), "PPM P6 / BMP 24 · open from Files · [/] browse folder");
    }
    fb_draw_string_fit(tx + desktop_u(4), sy + desktop_u(2), cw - desktop_u(8), status,
                       desktop_color_dim(), desktop_color_surface());
}

int desktop_images_key(int key) {
    if (key == '[' || key == 'p' || key == 'P') {
        if (img_nav(-1))
            return 1;
        return img_total > 1;
    }
    if (key == ']' || key == 'n' || key == 'N') {
        if (img_nav(1))
            return 1;
        return img_total > 1;
    }
    if (key == '0' || key == 'f' || key == 'F') {
        img_mode = IMG_MODE_FIT;
        img_pan_x = img_pan_y = 0;
        img_mark_dirty();
        return 1;
    }
    if (key == '1') {
        img_mode = IMG_MODE_ACTUAL;
        img_pan_x = img_pan_y = 0;
        img_mark_dirty();
        return 1;
    }
    if (key == '+' || key == '=') {
        img_mode = IMG_MODE_ZOOM;
        img_zoom += 10;
        if (img_zoom > 400)
            img_zoom = 400;
        {
            int wi = desktop_find_win(APP_IMAGES);
            if (wi >= 0) {
                uint32_t tx, ty, cw, ch;
                img_viewport_geom(&wins[wi], &tx, &ty, &cw, &ch);
                img_clamp_pan(cw, ch);
            }
        }
        img_mark_dirty();
        return 1;
    }
    if (key == '-') {
        img_mode = IMG_MODE_ZOOM;
        img_zoom -= 10;
        if (img_zoom < 25)
            img_zoom = 25;
        {
            int wi = desktop_find_win(APP_IMAGES);
            if (wi >= 0) {
                uint32_t tx, ty, cw, ch;
                img_viewport_geom(&wins[wi], &tx, &ty, &cw, &ch);
                img_clamp_pan(cw, ch);
            }
        }
        img_mark_dirty();
        return 1;
    }
    if (!img_can_pan())
        return 0;
    int32_t step = (int32_t)desktop_u(16);
    if (keyboard_shift_down())
        step *= 4;
    if (key == KEY_LEFT) {
        img_pan_by(-step, 0);
        return 1;
    }
    if (key == KEY_RIGHT) {
        img_pan_by(step, 0);
        return 1;
    }
    if (key == KEY_UP) {
        img_pan_by(0, -step);
        return 1;
    }
    if (key == KEY_DOWN) {
        img_pan_by(0, step);
        return 1;
    }
    return 0;
}

void desktop_images_wheel(int wheel) {
    if (wheel > 0)
        img_nav(-1);
    else
        img_nav(1);
}

int desktop_images_click(struct win *w, int32_t mx, int32_t my) {
    if (!img_can_pan())
        return 0;
    uint32_t tx, vty, cw, vch;
    img_viewport_geom(w, &tx, &vty, &cw, &vch);
    if (mx < (int32_t)tx || mx >= (int32_t)(tx + cw) ||
        my < (int32_t)vty || my >= (int32_t)(vty + vch))
        return 0;
    img_dragging = 1;
    img_drag_last_x = mx;
    img_drag_last_y = my;
    return 1;
}

void desktop_images_drag(int32_t mx, int32_t my) {
    if (!img_dragging)
        return;
    img_pan_by(mx - img_drag_last_x, my - img_drag_last_y);
    img_drag_last_x = mx;
    img_drag_last_y = my;
}

void desktop_images_release(void) {
    img_dragging = 0;
}

int desktop_images_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 2)
        return 0;
    int n = 0;
#define IADD(lbl, en, sep, act) do { \
    if (n >= max_items) return n; \
    items[n].label = (lbl); items[n].enabled = (en); items[n].separator = (sep); \
    items[n].action_id = (act); n++; } while (0)
    IADD("Fit to window", img_loaded, 0, CTX_ACT_IMG_FIT);
    IADD("Actual size", img_loaded, 0, CTX_ACT_IMG_ACTUAL);
    IADD("Next image", img_loaded && img_total > 1, 0, CTX_ACT_IMG_NEXT);
    IADD("Previous image", img_loaded && img_total > 1, 0, CTX_ACT_IMG_PREV);
    IADD(NULL, 0, 1, CTX_ACT_NONE);
    IADD("Open folder in Files", img_dir[0], 0, CTX_ACT_IMG_OPEN_DIR);
    IADD("Copy path", img_path[0], 0, CTX_ACT_IMG_COPY_PATH);
    IADD(NULL, 0, 1, CTX_ACT_NONE);
    IADD("Close window", 1, 0, CTX_ACT_CLOSE);
    return n;
#undef IADD
}

int desktop_images_ctx_action(int action_id) {
    switch (action_id) {
    case CTX_ACT_IMG_FIT:
        img_mode = IMG_MODE_FIT;
        img_pan_x = img_pan_y = 0;
        img_mark_dirty();
        return 1;
    case CTX_ACT_IMG_ACTUAL:
        img_mode = IMG_MODE_ACTUAL;
        img_pan_x = img_pan_y = 0;
        img_mark_dirty();
        return 1;
    case CTX_ACT_IMG_NEXT:
        return img_nav(1);
    case CTX_ACT_IMG_PREV:
        return img_nav(-1);
    case CTX_ACT_IMG_OPEN_DIR: {
        const char *base = img_path;
        for (const char *p = img_path; *p; p++)
            if (*p == '/')
                base = p + 1;
        desktop_files_goto(img_dir, base[0] ? base : NULL);
        return 1;
    }
    case CTX_ACT_IMG_COPY_PATH:
        if (img_path[0]) {
            clipboard_set(img_path, strlen(img_path));
            notify_push("Path copied");
            dirty_bits |= DIRTY_TOAST;
        }
        return 1;
    default:
        return 0;
    }
}
