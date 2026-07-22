#include "css.h"
#include "util.h"

void css_sheet_init(struct css_sheet *s) {
    memset(s, 0, sizeof(*s));
}

static int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return 0;
}

static int parse_color(const char *v, struct css_color *c) {
    if (!v || !c)
        return -1;
    if (v[0] == '#' && strlen(v) == 7) {
        c->r = (uint8_t)((hex_digit(v[1]) << 4) | hex_digit(v[2]));
        c->g = (uint8_t)((hex_digit(v[3]) << 4) | hex_digit(v[4]));
        c->b = (uint8_t)((hex_digit(v[5]) << 4) | hex_digit(v[6]));
        c->set = 1;
        return 0;
    }
    if (!strcmp(v, "black")) {
        c->r = c->g = c->b = 0;
        c->set = 1;
        return 0;
    }
    if (!strcmp(v, "white")) {
        c->r = c->g = c->b = 255;
        c->set = 1;
        return 0;
    }
    if (!strcmp(v, "red")) {
        c->r = 200;
        c->g = 40;
        c->b = 40;
        c->set = 1;
        return 0;
    }
    if (!strcmp(v, "green")) {
        c->r = 40;
        c->g = 160;
        c->b = 80;
        c->set = 1;
        return 0;
    }
    return -1;
}

static void apply_decl(struct css_style *st, const char *prop, const char *val) {
    if (!strcmp(prop, "color"))
        parse_color(val, &st->color);
    else if (!strcmp(prop, "background") || !strcmp(prop, "background-color"))
        parse_color(val, &st->background);
    else if (!strcmp(prop, "display")) {
        if (!strcmp(val, "none"))
            st->display = 2;
        else if (!strcmp(val, "inline"))
            st->display = 1;
        else
            st->display = 0;
    } else if (!strcmp(prop, "font-size")) {
        int n = 0;
        for (const char *p = val; *p >= '0' && *p <= '9'; p++)
            n = n * 10 + (*p - '0');
        if (n > 0)
            st->font_size = n;
    } else if (!strcmp(prop, "margin")) {
        int n = 0;
        for (const char *p = val; *p >= '0' && *p <= '9'; p++)
            n = n * 10 + (*p - '0');
        st->margin = n;
    } else if (!strcmp(prop, "padding")) {
        int n = 0;
        for (const char *p = val; *p >= '0' && *p <= '9'; p++)
            n = n * 10 + (*p - '0');
        st->padding = n;
    }
}

void css_parse_stylesheet(struct css_sheet *s, const char *css) {
    if (!s || !css)
        return;
    const char *p = css;
    while (*p && s->nrules < CSS_MAX_RULES) {
        while (*p == ' ' || *p == '\n' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/'))
                p++;
            if (*p)
                p += 2;
            continue;
        }
        char sel[64];
        size_t si = 0;
        while (*p && *p != '{' && si + 1 < sizeof(sel)) {
            if (*p != '\n')
                sel[si++] = *p;
            p++;
        }
        sel[si] = '\0';
        while (si > 0 && (sel[si - 1] == ' ' || sel[si - 1] == '\t'))
            sel[--si] = '\0';
        if (*p != '{')
            break;
        p++;
        struct css_rule *r = &s->rules[s->nrules];
        memset(r, 0, sizeof(*r));
        snprintf(r->selector, sizeof(r->selector), "%s", sel);
        while (*p && *p != '}') {
            while (*p == ' ' || *p == '\n' || *p == '\t' || *p == ';')
                p++;
            if (*p == '}' || !*p)
                break;
            char prop[32], val[64];
            size_t pi = 0;
            while (*p && *p != ':' && pi + 1 < sizeof(prop)) {
                if (*p != ' ' && *p != '\n')
                    prop[pi++] = *p;
                p++;
            }
            prop[pi] = '\0';
            if (*p == ':')
                p++;
            while (*p == ' ')
                p++;
            size_t vi = 0;
            while (*p && *p != ';' && *p != '}' && vi + 1 < sizeof(val)) {
                if (*p != '\n')
                    val[vi++] = *p;
                p++;
            }
            while (vi > 0 && val[vi - 1] == ' ')
                vi--;
            val[vi] = '\0';
            apply_decl(&r->style, prop, val);
            if (*p == ';')
                p++;
        }
        if (*p == '}')
            p++;
        r->used = 1;
        s->nrules++;
    }
}

static int sel_match(struct dom_document *doc, int node_id, const char *sel) {
    struct dom_node *n = dom_node(doc, node_id);
    if (!n || n->type != DOM_ELEMENT || !sel)
        return 0;
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
        for (const char *p = c; *p; p++) {
            size_t i = 0;
            while (i < nlen && p[i] == cls[i])
                i++;
            if (i == nlen)
                return 1;
        }
        return 0;
    }
    char tag[32];
    size_t i = 0;
    for (; sel[i] && sel[i] != '.' && sel[i] != '#' && sel[i] != ' ' && i + 1 < sizeof(tag);
         i++)
        tag[i] = sel[i];
    tag[i] = '\0';
    for (char *t = tag; *t; t++)
        if (*t >= 'A' && *t <= 'Z')
            *t = (char)(*t - 'A' + 'a');
    return !strcmp(n->tag, tag);
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
    for (int i = 0; i < s->nrules; i++) {
        if (!s->rules[i].used)
            continue;
        if (sel_match(doc, node_id, s->rules[i].selector)) {
            struct css_style *st = &s->rules[i].style;
            if (st->color.set)
                out->color = st->color;
            if (st->background.set)
                out->background = st->background;
            if (st->font_size)
                out->font_size = st->font_size;
            if (st->display)
                out->display = st->display;
            if (st->margin)
                out->margin = st->margin;
            if (st->padding)
                out->padding = st->padding;
        }
    }
}

uint32_t css_to_rgb(const struct css_color *c, uint32_t fallback) {
    if (!c || !c->set)
        return fallback;
    return ((uint32_t)c->r << 16) | ((uint32_t)c->g << 8) | (uint32_t)c->b;
}

int css_layout(struct dom_document *doc, struct css_sheet *sheet,
               struct css_box *boxes, int max_boxes, int content_w) {
    if (!doc || !boxes || max_boxes <= 0)
        return 0;
    int n = 0;
    int y = 0;
    struct css_style root_st;
    memset(&root_st, 0, sizeof(root_st));

    int body = doc->body >= 0 ? doc->body : doc->root;
    if (body < 0)
        return 0;

    int queue[DOM_MAX_NODES];
    int qh = 0, qt = 0;
    for (int c = doc->nodes[body].first_child; c >= 0; c = doc->nodes[c].next_sibling) {
        if (qt < DOM_MAX_NODES)
            queue[qt++] = c;
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
            boxes[n].node_id = id;
            boxes[n].x = st.margin + st.padding;
            boxes[n].y = y;
            boxes[n].w = content_w - boxes[n].x * 2;
            if (boxes[n].w < 40)
                boxes[n].w = content_w;
            boxes[n].h = st.font_size ? st.font_size + 4 : 20;
            boxes[n].style = st;
            snprintf(boxes[n].text, sizeof(boxes[n].text), "%s", node->text);
            y += boxes[n].h + 4;
            n++;
        } else if (node->type == DOM_ELEMENT) {
            char text[128];
            dom_collect_text(doc, id, text, sizeof(text));
            int is_heading = !strcmp(node->tag, "h1") || !strcmp(node->tag, "h2") ||
                             !strcmp(node->tag, "h3");
            int is_block = is_heading || !strcmp(node->tag, "p") || !strcmp(node->tag, "li") ||
                           !strcmp(node->tag, "div") || !strcmp(node->tag, "pre") ||
                           !strcmp(node->tag, "blockquote") || !strcmp(node->tag, "button") ||
                           !strcmp(node->tag, "a");
            if (is_block && text[0]) {
                boxes[n].node_id = id;
                boxes[n].x = st.margin + st.padding;
                boxes[n].y = y;
                boxes[n].w = content_w - boxes[n].x * 2;
                if (boxes[n].w < 40)
                    boxes[n].w = content_w;
                boxes[n].h = is_heading ? 28 : 20;
                boxes[n].style = st;
                if (is_heading && !st.color.set) {
                    boxes[n].style.color.r = 0x3D;
                    boxes[n].style.color.g = 0xA3;
                    boxes[n].style.color.b = 0x6A;
                    boxes[n].style.color.set = 1;
                }
                if (!strcmp(node->tag, "a") && !st.color.set) {
                    boxes[n].style.color.r = 0x9A;
                    boxes[n].style.color.g = 0xC4;
                    boxes[n].style.color.b = 0xAE;
                    boxes[n].style.color.set = 1;
                }
                snprintf(boxes[n].text, sizeof(boxes[n].text), "%s", text);
                y += boxes[n].h + (is_heading ? 8 : 4);
                n++;
            }
            if (!is_block || !text[0]) {
                for (int c = node->first_child; c >= 0; c = doc->nodes[c].next_sibling)
                    if (qt < DOM_MAX_NODES)
                        queue[qt++] = c;
            }
        }
    }
    return n;
}
