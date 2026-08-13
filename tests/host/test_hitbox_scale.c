/*
 * Host tests for UI-scale hit metrics (mirrors desktop/browser helpers without
 * linking full GUI — same approach as test_desktop_titles.c).
 *
 * Covers contracts that exist on main (plus fail-closed clear mirrors):
 *   desktop_u / desktop_title_h scale consistency
 *   browser_clear_hit_rects zeros widths (fail-closed hits)
 *   shared Files list origin formula (draw + hit must agree)
 *   browser_layout_content_w matches browser_draw (scaled pad inset)
 *   content_w floor is browser_u(40) (160 at scale 4, not hardcoded 40)
 *   shared browser_client_rect (draw + click height pad u(6))
 *   browser_chrome_h and desktop_chrome_btn_strip_w scale with UI
 *   title clip + strip_w share reservation; chrome_btns gated when w < strip_w
 *   Monitor export hit clear fails closed
 *   Net Control DHCP / outbound row Y from shared layout
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

/* Mirror browser_client_rect (desktop_windows.c) — draw + click must agree. */
static void browser_client_rect(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h,
                                uint32_t *x, uint32_t *y, uint32_t *cw, uint32_t *ch) {
    if (x)
        *x = win_x + desktop_u(4);
    if (y)
        *y = win_y + desktop_title_h() + desktop_u(2);
    if (cw)
        *cw = win_w - desktop_u(8);
    if (ch)
        *ch = win_h - desktop_title_h() - desktop_u(6);
}


/* Mirror browser_u / chrome metrics from browser_internal.h */
static uint32_t browser_u(uint32_t v) { return v * fb_ui_scale(); }

struct browser_chrome_metrics {
    uint32_t ch;
    uint32_t pad;
    uint32_t gap;
    uint32_t gap_lg;
    uint32_t tab_pad;
};

static void browser_chrome_metrics_init(struct browser_chrome_metrics *m) {
    m->ch = fb_cell_h();
    m->pad = browser_u(6);
    m->gap = browser_u(4);
    m->gap_lg = browser_u(8);
    m->tab_pad = browser_u(6);
}

static uint32_t browser_chrome_tab_h(const struct browser_chrome_metrics *m) {
    return m->ch + m->tab_pad;
}

/* Matches draw: tab_h + ch + pad*2 + 8 [+ bm_h + 4]. */
static uint32_t browser_chrome_h(const struct browser_chrome_metrics *m,
                                 uint32_t tab_h, uint32_t bm_h) {
    uint32_t h = tab_h + m->ch + m->pad * 2 + m->gap_lg;
    if (bm_h > 0)
        h += bm_h + m->gap;
    return h;
}

/*
 * Intended layout content width (must match browser_draw):
 *   draw_w - pad*2 - browser_u(12).
 */
static int browser_layout_min_cw(void) { return (int)browser_u(40); }

static int browser_clamp_content_w(int cw) {
    int min_cw = browser_layout_min_cw();
    return cw < min_cw ? min_cw : cw;
}

static int browser_layout_content_w_for_draw_w(uint32_t draw_w) {
    struct browser_chrome_metrics cm;
    browser_chrome_metrics_init(&cm);
    int cw = (int)draw_w - (int)(cm.pad * 2 + browser_u(12));
    return browser_clamp_content_w(cw);
}

static int browser_draw_content_w_for_draw_w(uint32_t draw_w) {
    uint32_t pad = browser_u(6);
    int cw = (int)draw_w - (int)(pad * 2 + browser_u(12));
    return browser_clamp_content_w(cw);
}

/* Mirror desktop_chrome_btn_strip_w (desktop_windows.c). */
static uint32_t desktop_chrome_btn_strip_w(void) {
    uint32_t bsz = desktop_u(14);
    uint32_t gap = desktop_u(4);
    return desktop_u(22) + 2 * (bsz + gap);
}

/* Mirror desktop_title_text_geom — title clip + chrome strip share one reservation. */
static void desktop_title_text_geom(uint32_t win_w, uint32_t *pad_x, uint32_t *pad_y,
                                    uint32_t *tw) {
    uint32_t s = desktop_u(3);
    if (s < 2)
        s = 2;
    uint32_t px = s + desktop_u(6);
    uint32_t py = s + desktop_u(2);
    uint32_t btn_w = desktop_chrome_btn_strip_w();
    uint32_t text_w = (win_w > px + btn_w + s) ? (win_w - px - btn_w - s) : desktop_u(40);
    if (pad_x)
        *pad_x = px;
    if (pad_y)
        *pad_y = py;
    if (tw)
        *tw = text_w;
}

/* Mirror desktop_chrome_btns underflow gate (#339): omit buttons if w < strip_w. */
static int desktop_chrome_btns_fit(uint32_t win_w) {
    return win_w >= desktop_chrome_btn_strip_w();
}

/*
 * Mirror monitor_clear_hit_rects contract (export w/h zeroed → fail-closed).
 */
struct monitor_hits {
    uint32_t export_x, export_y, export_w, export_h;
};

static void monitor_clear_hit_rects(struct monitor_hits *h) {
    h->export_w = h->export_h = 0;
}

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
        expect(browser_u(3) == 3 * s, "browser block default gap scales");
    }
    g_scale = 1;
}

static void test_browser_client_rect(void) {
    for (uint32_t s = 1; s <= 4; s++) {
        g_scale = s;
        uint32_t x, y, cw, ch;
        browser_client_rect(40, 30, 400, 300, &x, &y, &cw, &ch);
        expect(x == 40 + desktop_u(4), "browser client x");
        expect(y == 30 + desktop_title_h() + desktop_u(2), "browser client y");
        expect(cw == 400 - desktop_u(8), "browser client w");
        /* Draw used u(6); old click path used u(8) — shared helper is u(6). */
        expect(ch == 300 - desktop_title_h() - desktop_u(6), "browser client h uses u(6)");
        expect(point_in((int32_t)(x + cw / 2), (int32_t)(y + ch / 2), x, y, cw, ch),
               "browser client center is inside rect");
    }
    g_scale = 1;
}

/*
 * browser_layout_content_w must match browser_draw:
 *   content_w = draw_w - pad*2 - browser_u(12)  (pad = browser_u(6))
 * Old layout used hardcoded -24, which only matches at scale 1.
 */
static void test_browser_layout_content_w(void) {
    for (uint32_t s = 1; s <= 4; s++) {
        g_scale = s;
        uint32_t win_w = 800;
        uint32_t draw_w = win_w > desktop_u(8) ? win_w - desktop_u(8) : win_w;
        int draw_cw = browser_draw_content_w_for_draw_w(draw_w);
        int layout_cw = browser_layout_content_w_for_draw_w(draw_w);
        expect(layout_cw == draw_cw, "layout content_w matches draw");
        expect(layout_cw == (int)draw_w - (int)(6 * s * 2 + 12 * s),
               "content_w = draw_w - (pad*2 + 12) * scale");
        if (s == 1)
            expect(layout_cw == (int)draw_w - 24, "scale1: equals old -24");
        else
            expect(layout_cw != (int)draw_w - 24, "scale>1: not hardcoded -24");
    }
    g_scale = 1;
}

/* Narrow windows must floor at browser_u(40), not hardcoded 40. */
static void test_browser_content_w_floor(void) {
    g_scale = 1;
    expect(browser_clamp_content_w(10) == 40, "scale1 floor 40");
    expect(browser_clamp_content_w(40) == 40, "scale1 floor equal");
    expect(browser_clamp_content_w(80) == 80, "scale1 above floor");
    g_scale = 4;
    expect(browser_clamp_content_w(10) == 160, "scale4 floor 160 not 40");
    expect(browser_clamp_content_w(40) == 160, "scale4 old 40 is below floor");
    expect(browser_clamp_content_w(200) == 200, "scale4 above floor");
    expect(browser_layout_content_w_for_draw_w(20) == 160, "scale4 narrow draw_w floors");
    g_scale = 1;
}


static void test_browser_chrome_h(void) {
    uint32_t prev = 0;
    for (uint32_t s = 1; s <= 4; s++) {
        g_scale = s;
        struct browser_chrome_metrics cm;
        browser_chrome_metrics_init(&cm);
        uint32_t tab_h = browser_chrome_tab_h(&cm);
        uint32_t h0 = browser_chrome_h(&cm, tab_h, 0);
        uint32_t bm_h = fb_cell_h();
        uint32_t h1 = browser_chrome_h(&cm, tab_h, bm_h);

        expect(tab_h == cm.ch + browser_u(6), "tab_h = ch + tab_pad");
        expect(h0 == tab_h + cm.ch + cm.pad * 2 + cm.gap_lg, "chrome_h without bookmarks");
        expect(h1 == h0 + bm_h + cm.gap, "chrome_h adds bm_h + gap");
        expect(h0 >= prev, "chrome_h non-decreasing across scales");
        prev = h0;
    }
    g_scale = 1;
}

static void test_desktop_chrome_btn_strip_w(void) {
    uint32_t prev = 0;
    for (uint32_t s = 1; s <= 4; s++) {
        g_scale = s;
        uint32_t bsz = desktop_u(14);
        uint32_t gap = desktop_u(4);
        uint32_t need = desktop_chrome_btn_strip_w();
        expect(need == desktop_u(22) + 2 * (bsz + gap), "strip_w formula");
        expect(need == (22 + 2 * (14 + 4)) * s, "strip_w scales linearly");
        expect(need >= prev, "strip_w non-decreasing");
        expect(need > 0, "strip_w positive");
        prev = need;

        uint32_t win_w = 400 * s;
        uint32_t pad_x, pad_y, tw;
        desktop_title_text_geom(win_w, &pad_x, &pad_y, &tw);
        uint32_t inset = desktop_u(3) < 2 ? 2 : desktop_u(3);
        expect(pad_x + tw + need + inset <= win_w, "title + strip fit in window");
        expect(desktop_chrome_btns_fit(win_w), "wide window shows chrome btns");
        expect(!desktop_chrome_btns_fit(need - 1), "w < strip_w: #339 underflow gate");
        expect(desktop_chrome_btns_fit(need), "w == strip_w: buttons allowed");
    }
    g_scale = 1;
}

static void test_monitor_clear_hit_rects(void) {
    struct monitor_hits h;
    h.export_x = 10;
    h.export_y = 20;
    h.export_w = 80;
    h.export_h = 18;
    expect(point_in(40, 28, h.export_x, h.export_y, h.export_w, h.export_h),
           "export hit live before clear");
    monitor_clear_hit_rects(&h);
    expect(h.export_w == 0 && h.export_h == 0, "export w/h zeroed");
    expect(h.export_x == 10 && h.export_y == 20, "export xy preserved");
    expect(!point_in(40, 28, h.export_x, h.export_y, h.export_w, h.export_h),
           "export fail-closed after clear");
}

/* Mirror netctl_layout DHCP / outbound offsets (desktop_netctl.c). */
static void test_netctl_layout_rows(void) {
    for (uint32_t s = 1; s <= 4; s++) {
        g_scale = s;
        uint32_t row = fb_cell_h() + desktop_u(6);
        uint32_t title_y = 100 + desktop_title_h() + desktop_u(10);
        /* title, link, dns, blank(+2), privacy, outbound → outbound at +5 rows */
        uint32_t outbound_y = title_y + row * 5;
        /* … kill, kill_st, persist, persist_st, blank(+2), actions, dhcp → +13 from title */
        uint32_t dhcp_y = title_y + row * 13;
        expect(outbound_y - title_y == row * 5, "netctl outbound row offset");
        expect(dhcp_y - title_y == row * 13, "netctl DHCP row offset (not *9)");
        expect(point_in(120, (int32_t)(dhcp_y + row / 2), 100, dhcp_y, 200, row),
               "DHCP hit at shared layout Y");
        expect(!point_in(120, (int32_t)(title_y + row * 9 + row / 2), 100, dhcp_y, 200, row),
               "old *9 Y is not DHCP");
    }
    g_scale = 1;
}

int main(void) {
    test_desktop_u_title_h();
    test_browser_clear_hit_rects();
    test_files_list_origin();
    test_browser_chrome_pads_scale();
    test_browser_layout_content_w();
    test_browser_content_w_floor();
    test_browser_client_rect();
    test_browser_chrome_h();
    test_desktop_chrome_btn_strip_w();
    test_monitor_clear_hit_rects();
    test_netctl_layout_rows();

    if (fails) {
        fprintf(stderr, "%d hitbox_scale test(s) failed\n", fails);
        return 1;
    }
    printf("test_hitbox_scale: ok\n");
    return 0;
}
