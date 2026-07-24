#ifndef PEAK_BROWSER_INTERNAL_H
#define PEAK_BROWSER_INTERNAL_H

#include "types.h"
#include "dom.h"
#include "css.h"
#include "browser_js.h"

#define BR_URL_MAX    160
#define BR_BODY_MAX   (128 * 1024)
#define BR_MAX_BLOCKS 192
#define BR_TEXT_MAX   120
#define BR_TITLE_MAX  48
#define BR_MAX_TABS   4
#define BR_MAX_BOXES  128

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
};

/* Shared session state (defined in browser.c). */
extern struct br_tab tabs[BR_MAX_TABS];
extern int ntabs;
extern int active;
extern int editing;
extern int needs_redraw;
extern uint32_t hit_tab_y, hit_tab_h, hit_tab_w;
extern uint32_t hit_plus_x, hit_go_x, hit_go_w, hit_bar_y, hit_bar_h;

struct br_tab *browser_cur(void);
void browser_select_tab(int i);
int  browser_new_tab(const char *url);
void browser_close_tab(int i);
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
