/*
 * Host tests: css_layout applies layout_u to margin/padding/width/max_width
 * so horizontal box placement tracks UI scale (same as heights).
 */
#include "css.h"
#include "dom.h"
#include "fb.h"

#include <stdio.h>
#include <string.h>

static uint32_t g_scale = 1;
static int fails;

uint32_t fb_ui_scale(void) { return g_scale; }
uint32_t fb_char_w(void) { return 8 * g_scale; }
uint32_t fb_char_h(void) { return 16 * g_scale; }
uint32_t fb_cell_w(void) { return fb_char_w(); }
uint32_t fb_cell_h(void) { return fb_char_h() + g_scale; }

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int build_box_doc(struct dom_document *doc, struct css_sheet *sheet) {
    dom_doc_init(doc);
    css_sheet_init(sheet);
    css_parse_stylesheet(sheet, ".box{padding:8px;width:100px;margin:4px;max-width:200px}");

    int html = dom_create_element(doc, "html");
    int body = dom_create_element(doc, "body");
    int p = dom_create_element(doc, "p");
    int text = dom_create_text(doc, "Hello");
    if (html < 0 || body < 0 || p < 0 || text < 0)
        return -1;
    doc->root = html;
    doc->body = body;
    if (dom_append_child(doc, html, body) != 0)
        return -1;
    if (dom_append_child(doc, body, p) != 0)
        return -1;
    if (dom_set_attr(doc, p, "class", "box") != 0)
        return -1;
    if (dom_append_child(doc, p, text) != 0)
        return -1;
    return 0;
}

static void test_padding_width_scales(void) {
    struct dom_document doc;
    struct css_sheet sheet;
    struct css_box boxes[8];
    const int content_w = 800;

    expect(build_box_doc(&doc, &sheet) == 0, "build fixture");

    g_scale = 1;
    int n1 = css_layout(&doc, &sheet, boxes, 8, content_w);
    expect(n1 == 1, "scale1 one box");
    int x1 = boxes[0].x;
    int w1 = boxes[0].w;
    int y1 = boxes[0].y;
    expect(x1 == 4 + 8, "scale1 x = margin+padding");
    expect(w1 == 100, "scale1 width 100");
    expect(y1 == 4, "scale1 y = margin");
    /* Style fields stay raw CSS px (scaling is placement-only). */
    expect(boxes[0].style.padding == 8, "style.padding raw");
    expect(boxes[0].style.width == 100, "style.width raw");
    expect(boxes[0].style.margin == 4, "style.margin raw");

    g_scale = 4;
    int n4 = css_layout(&doc, &sheet, boxes, 8, content_w);
    expect(n4 == 1, "scale4 one box");
    expect(boxes[0].x == x1 * 4, "scale4 x *4");
    expect(boxes[0].w == w1 * 4, "scale4 width *4");
    expect(boxes[0].y == y1 * 4, "scale4 y *4");
    expect(boxes[0].style.padding == 8, "scale4 style.padding still raw");
    expect(boxes[0].style.width == 100, "scale4 style.width still raw");
}

static void test_max_width_scales(void) {
    struct dom_document doc;
    struct css_sheet sheet;
    struct css_box boxes[8];

    dom_doc_init(&doc);
    css_sheet_init(&sheet);
    /* width larger than max-width → max-width wins after scale. */
    css_parse_stylesheet(&sheet, "p{width:300px;max-width:50px;margin:0;padding:0}");

    int html = dom_create_element(&doc, "html");
    int body = dom_create_element(&doc, "body");
    int p = dom_create_element(&doc, "p");
    int text = dom_create_text(&doc, "Hi");
    expect(html >= 0 && body >= 0 && p >= 0 && text >= 0, "maxw nodes");
    doc.root = html;
    doc.body = body;
    expect(dom_append_child(&doc, html, body) == 0, "html->body");
    expect(dom_append_child(&doc, body, p) == 0, "body->p");
    expect(dom_append_child(&doc, p, text) == 0, "p->text");

    g_scale = 1;
    expect(css_layout(&doc, &sheet, boxes, 8, 800) == 1, "maxw scale1");
    expect(boxes[0].w == 50, "scale1 max-width 50");

    g_scale = 4;
    expect(css_layout(&doc, &sheet, boxes, 8, 800) == 1, "maxw scale4");
    expect(boxes[0].w == 200, "scale4 max-width 200");
}

int main(void) {
    test_padding_width_scales();
    test_max_width_scales();
    if (fails) {
        fprintf(stderr, "test_css_layout: %d failure(s)\n", fails);
        return 1;
    }
    printf("test_css_layout: ok\n");
    return 0;
}
