/*
 * Host tests for UI-scale hit metrics (mirrors desktop/browser helpers without
 * linking full GUI — same approach as test_desktop_titles.c).
 *
 * Covers contracts that exist on main:
 *   desktop_u / desktop_title_h scale consistency
 *   browser_clear_hit_rects zeros widths (fail-closed hits)
 *   shared Files list origin formula (draw + hit must agree)
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t g_scale = 1;

/* Mirror kernel/fb.c */
static uint32_t fb_ui_scale(void) { return g_scale; }
static uint32_t fb_char_h(void) { return 16 * g_scale; }
static uint32_t fb_cell_h(void) { return fb_char_h() + g_scale; }

/* Mirror kernel/gui/desktop.c */
static uint32_t desktop_u(uint32_t v) { return v * fb_ui_scale(); }

static uint32_t desktop_title_h(void) {
    uint32_t h = fb_cell_h() + desktop_u(8);
    return h < 22 ? 22 : h;
}

/* Mirror desktop_point_in */
static int point_in(int32_t px, int32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

/* Mirror browser hit globals + browser_clear_hit_rects (browser.c). */
struct browser_hits {
    uint32_t hit_tab_h, hit_tab_w;
    uint32_t hit_plus_x, hit_go_x, hit_go_w, hit_bar_y, hit_bar_h;
    uint32_t hit_back_x, hit_back_w, hit_fwd_x, hit_fwd_w;
    uint32_t hit_retry_x, hit_retry_y, hit_retry_w, hit_retry_h;
    uint32_t hit_accept_x, hit_accept_y, hit_accept_w, hit_accept_h;
    uint32_t hit_forget_x, hit_forget_y, hit_forget_w, hit_forget_h;
    uint32_t hit_bm_h;
    uint32_t hit_tab_close_w[4];
    uint32_t hit_bm_w[4];
};

static void browser_clear_hit_rects(struct browser_hits *h) {
    h->hit_tab_h = h->hit_tab_w = 0;
    h->hit_plus_x = h->hit_go_x = h->hit_go_w = h->hit_bar_y = h->hit_bar_h = 0;
    h->hit_back_x = h->hit_back_w = h->hit_fwd_x = h->hit_fwd_w = 0;
    h->hit_retry_w = h->hit_retry_h = 0;
    h->hit_accept_w = h->hit_accept_h = 0;
    h->hit_forget_w = h->hit_forget_h = 0;
    h->hit_bm_h = 0;
    memset(h->hit_tab_close_w, 0, sizeof(h->hit_tab_close_w));
    memset(h->hit_bm_w, 0, sizeof(h->hit_bm_w));
}

/* Shared Files list origin (draw + hit); PR6 exports files_list_origin(). */
static void files_list_origin(uint32_t win_x, uint32_t win_y,
                              uint32_t *tx, uint32_t *ty, uint32_t *row_h) {
    uint32_t ch = fb_cell_h();
    if (tx)
        *tx = win_x + desktop_u(12);
    if (ty)
        *ty = win_y + desktop_title_h() + desktop_u(8) + ch * 4 + desktop_u(4);
    if (row_h)
        *row_h = ch;
}

/* Mirror browser_u from browser_internal.h */
static uint32_t browser_u(uint32_t v) { return v * fb_ui_scale(); }

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void test_desktop_u_title_h(void) {
    uint32_t prev_th = 0;
    for (uint32_t s = 1; s <= 4; s++) {
        g_scale = s;
        expect(desktop_u(0) == 0, "desktop_u(0)");
        expect(desktop_u(1) == s, "desktop_u(1) == scale");
        expect(desktop_u(14) == 14 * s, "desktop_u(14) scales");
        expect(desktop_u(22) == 22 * s, "desktop_u(22) scales");

        uint32_t cell = fb_cell_h();
        uint32_t th = desktop_title_h();
        uint32_t raw = cell + desktop_u(8);
        expect(th == (raw < 22 ? 22 : raw), "title_h matches cell+pad floor");
        expect(th >= 22, "title_h min 22");
        expect(th >= prev_th, "title_h non-decreasing across scales");
        prev_th = th;
    }
    g_scale = 1;
}

static void test_browser_clear_hit_rects(void) {
    struct browser_hits h;
    memset(&h, 0xA5, sizeof(h));
    h.hit_retry_x = 40;
    h.hit_retry_y = 80;
    h.hit_retry_w = 64;
    h.hit_retry_h = 20;
    h.hit_accept_w = 48;
    h.hit_accept_h = 18;
    h.hit_forget_w = 48;
    h.hit_forget_h = 18;
    h.hit_go_w = 32;
    h.hit_tab_w = 80;
    h.hit_tab_h = 24;
    h.hit_tab_close_w[0] = 12;
    h.hit_bm_w[0] = 40;
    h.hit_bm_h = 16;

    expect(point_in(50, 90, h.hit_retry_x, h.hit_retry_y, h.hit_retry_w, h.hit_retry_h),
           "retry hit live before clear");

    browser_clear_hit_rects(&h);

    expect(h.hit_retry_w == 0 && h.hit_retry_h == 0, "retry w/h zeroed");
    expect(h.hit_accept_w == 0 && h.hit_accept_h == 0, "accept w/h zeroed");
    expect(h.hit_forget_w == 0 && h.hit_forget_h == 0, "forget w/h zeroed");
    expect(h.hit_go_w == 0, "go width zeroed");
    expect(h.hit_tab_w == 0 && h.hit_tab_h == 0, "tab w/h zeroed");
    expect(h.hit_bm_h == 0, "bookmark height zeroed");
    expect(h.hit_tab_close_w[0] == 0, "tab close width zeroed");
    expect(h.hit_bm_w[0] == 0, "bookmark width zeroed");
    expect(!point_in(50, 90, h.hit_retry_x, h.hit_retry_y, h.hit_retry_w, h.hit_retry_h),
           "retry fail-closed after clear (zero width)");
}

static void test_files_list_origin(void) {
    for (uint32_t s = 1; s <= 4; s++) {
        g_scale = s;
        uint32_t tx, ty, row_h;
        files_list_origin(100, 50, &tx, &ty, &row_h);
        expect(tx == 100 + desktop_u(12), "files list tx");
        expect(row_h == fb_cell_h(), "files row_h is cell_h");
        expect(ty == 50 + desktop_title_h() + desktop_u(8) + fb_cell_h() * 4 + desktop_u(4),
               "files list ty uses ch*4 (shared draw/hit)");

        /* Row 2 center must map to row 2 via shared origin. */
        int32_t my = (int32_t)(ty + 2 * row_h + row_h / 2);
        int row = (int)((my - (int32_t)ty) / (int32_t)row_h);
        expect(row == 2, "files row index from shared origin");
    }
    g_scale = 1;
}

static void test_browser_chrome_pads_scale(void) {
    for (uint32_t s = 1; s <= 4; s++) {
        g_scale = s;
        expect(browser_u(6) == 6 * s, "browser pad scales");
        expect(browser_u(4) == 4 * s, "browser gap scales");
        expect(browser_u(8) == 8 * s, "browser gap_lg scales");
    }
    g_scale = 1;
}

int main(void) {
    test_desktop_u_title_h();
    test_browser_clear_hit_rects();
    test_files_list_origin();
    test_browser_chrome_pads_scale();

    if (fails) {
        fprintf(stderr, "%d hitbox_scale test(s) failed\n", fails);
        return 1;
    }
    printf("test_hitbox_scale: ok\n");
    return 0;
}
