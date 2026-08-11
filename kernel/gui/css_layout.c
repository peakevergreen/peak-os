#include "css.h"
#include "fb.h"
#include "util.h"

/* Same as desktop_u — keep css_layout free of desktop_internal.h. */
static int layout_u(int v) {
    if (v < 0)
        v = 0;
    return (int)((uint32_t)v * fb_ui_scale());
}

static int layout_cell_h(void) { return (int)fb_cell_h(); }


static int sel_match(struct dom_document *doc, int node_id, const char *sel) {
    struct dom_node *n = dom_node(doc, node_id);
    if (!n || n->type != DOM_ELEMENT || !sel || !sel[0])
        return 0;
    /* Trim leading spaces. */
    while (*sel == ' ')
        sel++;
    /* Descendant: match last simple selector only (lite). */
    const char *last = sel;
    for (const char *p = sel; *p; p++)
        if (*p == ' ' || *p == '>')
            last = p + 1;
    while (*last == ' ')
        last++;
    sel = last;

    if (sel[0] == '#') {
        const char *id = dom_get_attr(doc, node_id, "id");
        return id && !strcmp(id, sel + 1);
    }
    if (sel[0] == '.') {
        const char *c = dom_get_attr(doc, node_id, "class");
        if (!c)
            return 0;
        const char *cls = sel + 1;
        size_t nlen = strlen(cls);
        const char *p = c;
        while (*p) {
            while (*p == ' ')
                p++;
            if (!strncmp(p, cls, nlen) && (p[nlen] == '\0' || p[nlen] == ' '))
                return 1;
            while (*p && *p != ' ')
                p++;
        }
        return 0;
    }
    char tag[32];
    size_t i = 0;
    for (; sel[i] && sel[i] != '.' && sel[i] != '#' && sel[i] != ' ' && sel[i] != ':' &&
           i + 1 < sizeof(tag);
         i++)
        tag[i] = sel[i];
    tag[i] = '\0';
    for (char *t = tag; *t; t++)
        if (*t >= 'A' && *t <= 'Z')
            *t = (char)(*t - 'A' + 'a');
    if (tag[0] && strcmp(n->tag, tag) != 0)
        return 0;
    /* tag.class or tag#id */
    if (sel[i] == '.') {
        const char *c = dom_get_attr(doc, node_id, "class");
        if (!c)
            return 0;
        return strstr(c, sel + i + 1) != NULL;
    }
    if (sel[i] == '#') {
        const char *id = dom_get_attr(doc, node_id, "id");
        return id && !strcmp(id, sel + i + 1);
    }
    return tag[0] != '\0';
}

void css_compute(struct css_sheet *s, struct dom_document *doc, int node_id,
                 struct css_style *out, const struct css_style *parent) {
    memset(out, 0, sizeof(*out));
    if (parent) {
        out->color = parent->color;
        out->font_size = parent->font_size;
    }
    if (!s)
        return;
    int best_spec[8];
    memset(best_spec, 0, sizeof(best_spec));
    for (int i = 0; i < s->nrules; i++) {
        if (!s->rules[i].used)
            continue;
        if (!sel_match(doc, node_id, s->rules[i].selector))
            continue;
        int spec = s->rules[i].specificity;
        struct css_style *st = &s->rules[i].style;
        if (st->color.set && spec >= best_spec[0]) {
            out->color = st->color;
            best_spec[0] = spec;
        }
        if (st->background.set && spec >= best_spec[1]) {
            out->background = st->background;
            best_spec[1] = spec;
        }
        if (st->font_size && spec >= best_spec[2]) {
            out->font_size = st->font_size;
            best_spec[2] = spec;
        }
        if (st->display && spec >= best_spec[3]) {
            out->display = st->display;
            best_spec[3] = spec;
        }
        if (spec >= best_spec[4]) {
            if (st->margin)
                out->margin = st->margin;
            if (st->padding)
                out->padding = st->padding;
            if (st->width)
                out->width = st->width;
            if (st->max_width)
                out->max_width = st->max_width;
            if (st->height)
                out->height = st->height;
            if (st->text_align)
                out->text_align = st->text_align;
            if (st->border)
                out->border = st->border;
            best_spec[4] = spec;
        }
    }
}

uint32_t css_to_rgb(const struct css_color *c, uint32_t fallback) {
    if (!c || !c->set)
        return fallback;
    return ((uint32_t)c->r << 16) | ((uint32_t)c->g << 8) | (uint32_t)c->b;
}

static int is_block_tag(const char *tag) {
    return !strcmp(tag, "h1") || !strcmp(tag, "h2") || !strcmp(tag, "h3") ||
           !strcmp(tag, "h4") || !strcmp(tag, "p") || !strcmp(tag, "li") ||
           !strcmp(tag, "div") || !strcmp(tag, "pre") || !strcmp(tag, "blockquote") ||
           !strcmp(tag, "section") || !strcmp(tag, "article") || !strcmp(tag, "main") ||
           !strcmp(tag, "header") || !strcmp(tag, "footer") || !strcmp(tag, "nav") ||
           !strcmp(tag, "ul") || !strcmp(tag, "ol") || !strcmp(tag, "form") ||
           !strcmp(tag, "table") || !strcmp(tag, "tr") || !strcmp(tag, "br") ||
           !strcmp(tag, "hr");
}

static int push_box(struct css_box *boxes, int max_boxes, int *n, int *y, int *x_inline,
                    int content_w, int node_id, int kind, const char *text,
                    struct css_style *st, int force_block) {
    if (*n >= max_boxes)
        return -1;
    int display = st->display;
    int block = force_block || display == 0;
    int gap_y = layout_u(4);
    int gap_x = layout_u(8);
    int min_w = layout_u(40);
    int w = content_w - (st->margin + st->padding) * 2;
    if (st->width > 0 && st->width < w)
        w = st->width;
    if (st->max_width > 0 && st->max_width < w)
        w = st->max_width;
    if (w < min_w)
        w = content_w > min_w ? content_w - gap_x : content_w;
    /* Heights track Peak cells / UI scale, not raw CSS px defaults. */
    int h;
    if (st->height > 0)
        h = layout_u(st->height);
    else if (st->font_size > 0)
        h = layout_u(st->font_size) + layout_u(6);
    else
        h = layout_cell_h();
    if (h < layout_cell_h())
        h = layout_cell_h();
    if (kind == 1) { /* image */
        if (st->height > 0)
            h = layout_u(st->height);
        else
            h = layout_u(48);
        if (st->width > 0)
            w = st->width;
    }
    if (kind == 2 || kind == 3) {
        if (st->height > 0)
            h = layout_u(st->height);
        else
            h = layout_u(24);
        if (h < layout_cell_h())
            h = layout_cell_h();
        if (w > layout_u(280))
            w = layout_u(280);
    }

    if (block || display != 1) {
        *x_inline = st->margin + st->padding;
        boxes[*n].x = *x_inline;
        boxes[*n].y = *y + st->margin;
    } else {
        if (*x_inline + w > content_w) {
            *x_inline = st->margin + st->padding;
            *y += h + gap_y;
        }
        boxes[*n].x = *x_inline;
        boxes[*n].y = *y;
        *x_inline += w + gap_x;
    }
    boxes[*n].node_id = node_id;
    boxes[*n].w = w;
    boxes[*n].h = h;
    boxes[*n].kind = kind;
    boxes[*n].style = *st;
    if (text)
        snprintf(boxes[*n].text, sizeof(boxes[*n].text), "%s", text);
    else
        boxes[*n].text[0] = '\0';
    if (block || display != 1)
        *y = boxes[*n].y + h + gap_y + st->margin;
    (*n)++;
    return 0;
}

int css_layout(struct dom_document *doc, struct css_sheet *sheet,
               struct css_box *boxes, int max_boxes, int content_w) {
    if (!doc || !boxes || max_boxes <= 0)
        return 0;
    int n = 0;
    int y = 0;
    int x_inline = 0;
    struct css_style root_st;
    memset(&root_st, 0, sizeof(root_st));

    int body = doc->body >= 0 ? doc->body : doc->root;
    if (body < 0)
        return 0;

    int queue[DOM_MAX_NODES];
    int qh = 0, qt = 0;
    {
        int steps = 0;
        for (int c = doc->nodes[body].first_child; c >= 0 && steps < doc->nnodes; steps++) {
            if (qt < DOM_MAX_NODES)
                queue[qt++] = c;
            c = dom_next_sibling(doc, c);
        }
    }
    while (qh < qt && n < max_boxes) {
        int id = queue[qh++];
        struct dom_node *node = dom_node(doc, id);
        if (!node)
            continue;
        struct css_style st;
        css_compute(sheet, doc, id, &st, &root_st);
        if (st.display == 2)
            continue;

        if (node->type == DOM_TEXT) {
            if (!node->text[0])
                continue;
            /* Line-wrap into multiple boxes when long. */
            const char *p = node->text;
            while (*p && n < max_boxes) {
                char line[160];
                size_t li = 0;
                int glyph_w = (int)fb_cell_w();
                if (glyph_w < 1)
                    glyph_w = 8;
                int cols = content_w / glyph_w;
                if (cols < 20)
                    cols = 20;
                if (cols > 150)
                    cols = 150;
                while (*p && (int)li < cols && li + 1 < sizeof(line)) {
                    if (*p == '\n') {
                        p++;
                        break;
                    }
                    line[li++] = *p++;
                }
                line[li] = '\0';
                if (!line[0])
                    continue;
                push_box(boxes, max_boxes, &n, &y, &x_inline, content_w, id, 0, line, &st, 0);
            }
            continue;
        }

        if (node->type != DOM_ELEMENT)
            continue;

        if (!strcmp(node->tag, "br")) {
            y += layout_cell_h();
            x_inline = 0;
            continue;
        }
        if (!strcmp(node->tag, "hr")) {
            st.border = 1;
            push_box(boxes, max_boxes, &n, &y, &x_inline, content_w, id, 0, "────────", &st, 1);
            continue;
        }
        if (!strcmp(node->tag, "img")) {
            const char *alt = dom_get_attr(doc, id, "alt");
            char label[80];
            snprintf(label, sizeof(label), "[img%s%s]", alt && alt[0] ? " " : "",
                     alt ? alt : "");
            push_box(boxes, max_boxes, &n, &y, &x_inline, content_w, id, 1, label, &st, 1);
            continue;
        }
        if (!strcmp(node->tag, "input")) {
            const char *type = dom_get_attr(doc, id, "type");
            const char *val = dom_get_attr(doc, id, "value");
            const char *ph = dom_get_attr(doc, id, "placeholder");
            char label[160];
            if (type && !strcmp(type, "submit"))
                snprintf(label, sizeof(label), "[%s]", val && val[0] ? val : "Submit");
            else if (type && !strcmp(type, "button"))
                snprintf(label, sizeof(label), "[%s]", val && val[0] ? val : "Button");
            else
                snprintf(label, sizeof(label), "%s",
                         val && val[0] ? val : (ph && ph[0] ? ph : "_______"));
            int kind = (type && (!strcmp(type, "submit") || !strcmp(type, "button"))) ? 3 : 2;
            push_box(boxes, max_boxes, &n, &y, &x_inline, content_w, id, kind, label, &st, 0);
            continue;
        }
        if (!strcmp(node->tag, "textarea")) {
            char text[160];
            dom_collect_text(doc, id, text, sizeof(text));
            if (!text[0])
                snprintf(text, sizeof(text), "_______");
            st.height = st.height ? st.height : 48;
            push_box(boxes, max_boxes, &n, &y, &x_inline, content_w, id, 2, text, &st, 1);
            continue;
        }
        if (!strcmp(node->tag, "button")) {
            char text[128];
            dom_collect_text(doc, id, text, sizeof(text));
            if (!text[0])
                snprintf(text, sizeof(text), "Button");
            char label[140];
            snprintf(label, sizeof(label), "[%s]", text);
            push_box(boxes, max_boxes, &n, &y, &x_inline, content_w, id, 3, label, &st, 0);
            continue;
        }
        if (!strcmp(node->tag, "select")) {
            char text[128];
            dom_collect_text(doc, id, text, sizeof(text));
            char label[140];
            snprintf(label, sizeof(label), "[%s ▼]", text[0] ? text : "select");
            push_box(boxes, max_boxes, &n, &y, &x_inline, content_w, id, 2, label, &st, 0);
            continue;
        }

        char text[160];
        dom_collect_text(doc, id, text, sizeof(text));
        int is_heading = !strcmp(node->tag, "h1") || !strcmp(node->tag, "h2") ||
                         !strcmp(node->tag, "h3") || !strcmp(node->tag, "h4");
        int is_block = is_block_tag(node->tag) || !strcmp(node->tag, "a");
        int is_inline = !strcmp(node->tag, "span") || !strcmp(node->tag, "strong") ||
                        !strcmp(node->tag, "em") || !strcmp(node->tag, "b") ||
                        !strcmp(node->tag, "i") || !strcmp(node->tag, "code") ||
                        !strcmp(node->tag, "label") || st.display == 1;

        if ((is_block || is_inline) && text[0] &&
            (is_heading || !strcmp(node->tag, "p") || !strcmp(node->tag, "li") ||
             !strcmp(node->tag, "a") || !strcmp(node->tag, "blockquote") ||
             !strcmp(node->tag, "pre") || is_inline)) {
            if (is_heading && !st.color.set) {
                st.color.r = 0x3D;
                st.color.g = 0xA3;
                st.color.b = 0x6A;
                st.color.set = 1;
            }
            if (!strcmp(node->tag, "a") && !st.color.set) {
                st.color.r = 0x9A;
                st.color.g = 0xC4;
                st.color.b = 0xAE;
                st.color.set = 1;
            }
            if (is_heading)
                st.font_size = st.font_size ? st.font_size : 22;
            push_box(boxes, max_boxes, &n, &y, &x_inline, content_w, id, 0, text, &st,
                     is_block && !is_inline);
            /* Still walk children for nested interactive controls. */
            if (!strcmp(node->tag, "form") || !strcmp(node->tag, "div") ||
                !strcmp(node->tag, "section") || !strcmp(node->tag, "li")) {
                int steps = 0;
                for (int c = node->first_child; c >= 0 && steps < doc->nnodes; steps++) {
                    if (qt < DOM_MAX_NODES)
                        queue[qt++] = c;
                    c = dom_next_sibling(doc, c);
                }
            }
            continue;
        }

        {
            int steps = 0;
            for (int c = node->first_child; c >= 0 && steps < doc->nnodes; steps++) {
                if (qt < DOM_MAX_NODES)
                    queue[qt++] = c;
                c = dom_next_sibling(doc, c);
            }
        }
    }
    return n;
}
