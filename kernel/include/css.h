#ifndef PEAK_CSS_H
#define PEAK_CSS_H

#include "types.h"
#include "dom.h"

#define CSS_MAX_RULES 128

struct css_color {
    uint8_t r, g, b;
    int set;
};

struct css_style {
    struct css_color color;
    struct css_color background;
    int font_size; /* px, 0 = default */
    int display;   /* 0 block, 1 inline, 2 none */
    int margin;
    int padding;
};

struct css_rule {
    int used;
    char selector[64];
    struct css_style style;
};

struct css_sheet {
    struct css_rule rules[CSS_MAX_RULES];
    int nrules;
};

void css_sheet_init(struct css_sheet *s);
void css_parse_stylesheet(struct css_sheet *s, const char *css);
void css_compute(struct css_sheet *s, struct dom_document *doc, int node_id,
                 struct css_style *out, const struct css_style *parent);
uint32_t css_to_rgb(const struct css_color *c, uint32_t fallback);

/* Simple layout box for paint. */
struct css_box {
    int node_id;
    int x, y, w, h;
    struct css_style style;
    char text[128];
};

int css_layout(struct dom_document *doc, struct css_sheet *sheet,
               struct css_box *boxes, int max_boxes, int content_w);

#endif
