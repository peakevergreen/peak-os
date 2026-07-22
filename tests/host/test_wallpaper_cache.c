/*
 * Host tests for wallpaper ARGB cache reuse policy (mirrors wallpaper.c).
 */
#include <stdio.h>
#include <stdint.h>

static int cache_ok;
static uint32_t cache_w, cache_h;
static const uint8_t *cache_rgb;
static uint32_t cache_src_w, cache_src_h;
static int rebuilds;

static const uint8_t src_a[] = {1, 2, 3};
static const uint8_t src_b[] = {4, 5, 6};

static int cache_should_rebuild(uint32_t w, uint32_t h,
                                const uint8_t *rgb, uint32_t src_w, uint32_t src_h) {
    if (cache_ok && cache_w == w && cache_h == h &&
        cache_rgb == rgb && cache_src_w == src_w && cache_src_h == src_h)
        return 0;
    return 1;
}

static void cache_rebuild(uint32_t w, uint32_t h,
                          const uint8_t *rgb, uint32_t src_w, uint32_t src_h) {
    if (!cache_should_rebuild(w, h, rgb, src_w, src_h))
        return;
    rebuilds++;
    cache_w = w;
    cache_h = h;
    cache_rgb = rgb;
    cache_src_w = src_w;
    cache_src_h = src_h;
    cache_ok = 1;
}

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    cache_rebuild(800, 600, src_a, 100, 80);
    expect(rebuilds == 1, "initial rebuild");
    cache_rebuild(800, 600, src_a, 100, 80);
    expect(rebuilds == 1, "same geometry+source skips rebuild");
    cache_rebuild(400, 300, src_a, 100, 80);
    expect(rebuilds == 2, "geometry change rebuilds");
    cache_rebuild(400, 300, src_b, 120, 90);
    expect(rebuilds == 3, "source change rebuilds");
    cache_rebuild(400, 300, src_b, 120, 90);
    expect(rebuilds == 3, "subrect draw reuses cache");

    if (fails) {
        fprintf(stderr, "%d wallpaper_cache test(s) failed\n", fails);
        return 1;
    }
    printf("test_wallpaper_cache: ok\n");
    return 0;
}
