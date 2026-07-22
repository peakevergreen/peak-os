#include "wallpaper.h"
#include "fb.h"
#include "heap.h"
#include "util.h"
#include "vfs.h"

#define WP_DEFAULT_PATH "/usr/share/peak/wallpapers/evergreen.ppm"
#define WP_CFG_PATH     "/etc/peak/wallpaper"

extern const uint8_t wallpaper_evergreen_ppm[];
extern const uint64_t wallpaper_evergreen_ppm_len;

static char wp_path[VFS_PATH_MAX];
static const uint8_t *wp_rgb;
static uint32_t wp_w, wp_h;
static size_t wp_rgb_len;

/* Scaled ARGB cache — small BSS fallback; heap for larger resolutions. */
#define WP_CACHE_FALLBACK_W 800
#define WP_CACHE_FALLBACK_H 600
static uint32_t wp_cache_fallback[WP_CACHE_FALLBACK_W * WP_CACHE_FALLBACK_H];
static uint32_t *wp_cache;
static uint32_t wp_cw, wp_ch;
static int wp_cache_ok;
static int wp_cache_heap;

static void wp_cache_invalidate(void) {
    wp_cache_ok = 0;
    wp_cw = wp_ch = 0;
    if (wp_cache_heap && wp_cache) {
        kfree(wp_cache);
        wp_cache = NULL;
        wp_cache_heap = 0;
    }
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int parse_ppm(const uint8_t *data, size_t len,
                     const uint8_t **rgb_out, uint32_t *w_out, uint32_t *h_out) {
    if (!data || len < 16 || data[0] != 'P' || data[1] != '6')
        return -1;
    size_t i = 2;
    while (i < len && is_space((char)data[i]))
        i++;
    /* skip comments */
    while (i < len && data[i] == '#') {
        while (i < len && data[i] != '\n')
            i++;
        while (i < len && is_space((char)data[i]))
            i++;
    }
    uint32_t w = 0, h = 0, maxv = 0;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        w = w * 10 + (uint32_t)(data[i++] - '0');
    while (i < len && is_space((char)data[i]))
        i++;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        h = h * 10 + (uint32_t)(data[i++] - '0');
    while (i < len && is_space((char)data[i]))
        i++;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        maxv = maxv * 10 + (uint32_t)(data[i++] - '0');
    if (i >= len || !is_space((char)data[i]) || w == 0 || h == 0 || maxv != 255)
        return -1;
    i++; /* single whitespace after maxval */
    size_t need = (size_t)w * (size_t)h * 3;
    if (i + need > len)
        return -1;
    *rgb_out = data + i;
    *w_out = w;
    *h_out = h;
    return 0;
}

static void clear_wp(void) {
    wp_path[0] = '\0';
    wp_rgb = NULL;
    wp_w = wp_h = 0;
    wp_rgb_len = 0;
    wp_cache_invalidate();
}

static int load_from_vfs(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n || n->type != VFS_FILE || !n->data || n->size < 16)
        return -1;
    const uint8_t *rgb;
    uint32_t w, h;
    if (parse_ppm(n->data, n->size, &rgb, &w, &h) != 0)
        return -1;
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(wp_path); i++)
        wp_path[i] = path[i];
    wp_path[i] = '\0';
    wp_rgb = rgb;
    wp_w = w;
    wp_h = h;
    wp_rgb_len = (size_t)w * (size_t)h * 3;
    wp_cache_invalidate();
    return 0;
}

static void wp_cache_rebuild(uint32_t w, uint32_t h) {
    if (!wallpaper_enabled() || w == 0 || h == 0)
        return;
    if (w <= WP_CACHE_FALLBACK_W && h <= WP_CACHE_FALLBACK_H) {
        if (wp_cache_heap && wp_cache)
            kfree(wp_cache);
        wp_cache = wp_cache_fallback;
        wp_cache_heap = 0;
    } else {
        size_t bytes = (size_t)w * (size_t)h * 4;
        uint32_t *buf = (uint32_t *)kmalloc(bytes);
        if (!buf)
            return;
        if (wp_cache_heap && wp_cache)
            kfree(wp_cache);
        wp_cache = buf;
        wp_cache_heap = 1;
    }
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t sy = (dy * wp_h) / h;
        if (sy >= wp_h)
            sy = wp_h - 1;
        const uint8_t *row = wp_rgb + (size_t)sy * (size_t)wp_w * 3;
        uint32_t *dst = wp_cache + (size_t)dy * (size_t)w;
        for (uint32_t dx = 0; dx < w; dx++) {
            uint32_t sx = (dx * wp_w) / w;
            if (sx >= wp_w)
                sx = wp_w - 1;
            const uint8_t *p = row + (size_t)sx * 3;
            dst[dx] = fb_rgb(p[0], p[1], p[2]);
        }
    }
    wp_cw = w;
    wp_ch = h;
    wp_cache_ok = 1;
}

void wallpaper_init(void) {
    clear_wp();
    vfs_mkdir("/usr");
    vfs_mkdir("/usr/share");
    vfs_mkdir("/usr/share/peak");
    vfs_mkdir("/usr/share/peak/wallpapers");
    vfs_write_file(WP_DEFAULT_PATH, wallpaper_evergreen_ppm,
                   (size_t)wallpaper_evergreen_ppm_len);

    char cfg[VFS_PATH_MAX];
    size_t n = 0;
    if (vfs_read_file(WP_CFG_PATH, cfg, sizeof(cfg) - 1, &n) == 0 && n > 0) {
        cfg[n] = '\0';
        while (n && (cfg[n - 1] == '\n' || cfg[n - 1] == '\r'))
            cfg[--n] = '\0';
        if (n == 0 || !strcmp(cfg, "none")) {
            clear_wp();
            return;
        }
        if (load_from_vfs(cfg) == 0)
            return;
    }
    /* Default: evergreen forest */
    load_from_vfs(WP_DEFAULT_PATH);
}

const char *wallpaper_path(void) {
    return wp_path;
}

int wallpaper_enabled(void) {
    return wp_rgb != NULL && wp_w > 0 && wp_h > 0;
}

int wallpaper_set(const char *path) {
    if (!path || !path[0] || !strcmp(path, "none")) {
        clear_wp();
        return 0;
    }
    if (load_from_vfs(path) != 0)
        return -1;
    return 0;
}

void wallpaper_next(void) {
    if (wallpaper_enabled())
        clear_wp();
    else
        load_from_vfs(WP_DEFAULT_PATH);
}

void wallpaper_persist(void) {
    const char *p = wallpaper_enabled() ? wp_path : "none";
    char buf[VFS_PATH_MAX + 2];
    size_t i = 0;
    while (p[i] && i + 2 < sizeof(buf)) {
        buf[i] = p[i];
        i++;
    }
    buf[i++] = '\n';
    buf[i] = '\0';
    vfs_write_file(WP_CFG_PATH, buf, i);
}

void wallpaper_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!wallpaper_enabled() || w == 0 || h == 0)
        return;
    struct framebuffer *fb = fb_get();
    uint32_t fw = (uint32_t)fb->width;
    uint32_t fh = (uint32_t)fb->height;
    if (!fw || !fh)
        return;

    /* Always cache full-desktop size; subrect draws sample from that cache.
     * (Previously toast strips rebuilt the cache to toast size and thrashed it.) */
    if (!wp_cache_ok || wp_cw != fw || wp_ch != fh)
        wp_cache_rebuild(fw, fh);

    if (x >= fw || y >= fh)
        return;
    if (x + w > fw)
        w = fw - x;
    if (y + h > fh)
        h = fh - y;

    if (wp_cache_ok && wp_cw == fw && wp_ch == fh) {
        fb_blit_argb(x, y, w, h, wp_cache + (size_t)y * (size_t)fw + x, fw);
        return;
    }
    /* Fallback if resolution exceeds cache */
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t sy = ((y + dy) * wp_h) / fh;
        if (sy >= wp_h)
            sy = wp_h - 1;
        const uint8_t *row = wp_rgb + (size_t)sy * (size_t)wp_w * 3;
        for (uint32_t dx = 0; dx < w; dx++) {
            uint32_t sx = ((x + dx) * wp_w) / fw;
            if (sx >= wp_w)
                sx = wp_w - 1;
            const uint8_t *p = row + (size_t)sx * 3;
            fb_put_pixel(x + dx, y + dy, fb_rgb(p[0], p[1], p[2]));
        }
    }
    (void)wp_rgb_len;
}
