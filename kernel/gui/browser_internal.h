#ifndef PEAK_BROWSER_INTERNAL_H
#define PEAK_BROWSER_INTERNAL_H

#include "types.h"
#include "dom.h"
#include "css.h"
#include "browser_js.h"
#include "ui_widgets.h"

#define BR_URL_MAX    160
#define BR_BODY_MAX   (256 * 1024)
#define BR_MAX_BLOCKS 256
#define BR_TEXT_MAX   160
#define BR_TITLE_MAX  48
#define BR_MAX_TABS   4
#define BR_MAX_BOXES  256
#define BR_MAX_STYLESHEETS 8
#define BR_STYLESHEET_MAX  (24 * 1024)
#define BR_MAX_IMAGES 16
#define BR_IMG_BYTES_MAX (48 * 1024)
#define BR_HIST_MAX 16

struct br_img {
    int used;
    int node_id;
    int x, y, w, h;
    uint32_t pw, ph; /* pixel size */
    uint8_t *rgb;    /* RGB888 or NULL if placeholder */
    char alt[48];
};

enum br_kind {
    BR_H1 = 1,
    BR_H2,
    BR_H3,
    BR_P,
    BR_LI,
    BR_LINK,
    BR_CODE,
    BR_QUOTE,
    BR_HR,
    BR_SPACER
};

struct br_block {
    uint8_t kind;
    char text[BR_TEXT_MAX];
};

struct br_tab {
    int used;
    char url[BR_URL_MAX];
    char title[BR_TITLE_MAX];
    char status[96];
    struct br_block blocks[BR_MAX_BLOCKS];
    int nblocks;
    int scroll_y;
    int http_status;
    int tls_secure;   /* page loaded over HTTPS */
    int tls_verified; /* cert verified + hostname matched */
    uint32_t page_bg, page_fg, page_muted, page_accent, page_link, page_surface;
    int colors_set;
    struct dom_document doc;
    struct css_sheet sheet;
    struct css_box boxes[BR_MAX_BOXES];
    int nboxes;
    struct js_runtime *js;
    struct browser_js_host jsh;
    int js_ok;
    int use_layout;
    int dom_dirty;
    int fetching;
    size_t last_body_len;
    uint64_t fetch_start;
    int show_retry;
    int show_tls_accept; /* TLS untrusted: Accept / Forget TOFU buttons */
    char prev_url[BR_URL_MAX];
    char forward_url[BR_URL_MAX];
    int show_console;
    int body_truncated;
    size_t body_total;
    int http2;
    char layout_mode[16]; /* "css" | "reader" | "error" */
    char reader_reason[80];
    struct br_img images[BR_MAX_IMAGES];
    int nimages;
    char hist_back[BR_HIST_MAX][BR_URL_MAX];
    int nhist_back;
    char hist_fwd[BR_HIST_MAX][BR_URL_MAX];
    int nhist_fwd;
    int focus_node;
    char focus_value[96];
    int focus_kind; /* 0 none, 1 input, 2 textarea, 3 button */
};

/* Shared session state (defined in browser.c). */
extern struct br_tab tabs[BR_MAX_TABS];
extern int ntabs;
extern int active;
extern int editing;
extern int needs_redraw;
extern uint32_t hit_tab_y, hit_tab_h, hit_tab_w;
extern uint32_t hit_tab_close_x[BR_MAX_TABS], hit_tab_close_w[BR_MAX_TABS];
extern uint32_t hit_plus_x, hit_go_x, hit_go_w, hit_bar_y, hit_bar_h;
extern uint32_t hit_retry_x, hit_retry_y, hit_retry_w, hit_retry_h;
extern uint32_t hit_accept_x, hit_accept_y, hit_accept_w, hit_accept_h;
extern uint32_t hit_forget_x, hit_forget_y, hit_forget_w, hit_forget_h;
extern uint32_t hit_back_x, hit_back_w, hit_fwd_x, hit_fwd_w;
extern uint32_t hit_bm_y, hit_bm_h, hit_bm_x[4], hit_bm_w[4];

struct br_tab *browser_cur(void);
void browser_select_tab(int i);
int  browser_new_tab(const char *url);
void browser_close_tab(int i);
int  browser_has_closed_tab(void);
void browser_restore_closed_tab(void);
void browser_closed_clear(void);
void browser_back(void);
void browser_forward(void);
void browser_reload(void);
void browser_bookmarks_init(void);
int  browser_bookmark_count(void);
const char *browser_bookmark_title(int idx);
const char *browser_bookmark_url(int idx);
int  browser_bookmark_add(const char *url, const char *title);
void browser_bookmark_go(int idx);
int  browser_ctx_menu(struct ctx_menu_item *items, int max_items);
int  browser_ctx_action(int action_id);
void browser_tab_teardown_js(struct br_tab *t);
void browser_rebuild_layout(struct br_tab *t, int content_w);
int  browser_is_local_host(const char *url);
void browser_normalize_url(const char *in, char *out, size_t out_cap);

void browser_clear_blocks(struct br_tab *t);
int  browser_add_block(struct br_tab *t, enum br_kind kind, const char *text);
int  browser_content_blocks(struct br_tab *t);
void browser_init_page_colors(struct br_tab *t, const char *html);
void browser_extract_title(struct br_tab *t, const char *html, int tab_index);
void browser_parse_html(struct br_tab *t, const char *html, int tab_index);
void browser_reader_fallback(struct br_tab *t, const char *html, int tab_index);

enum br_err_kind {
    BR_ERR_NETWORK = 0,
    BR_ERR_DNS,
    BR_ERR_TLS,
    BR_ERR_HTTP,
    BR_ERR_LOCAL,
};

void browser_error_page(struct br_tab *t, enum br_err_kind kind,
                        const char *detail, int http_st);

#endif

int browser_bookmark_remove(int idx);
int browser_download_save(const char *url, const char *body, size_t len, char *msg, size_t msg_cap);

const char *browser_page_body(size_t *len_out);
int browser_form_submit(struct br_tab *t, int form_node);
extern int browser_hist_navigating;
