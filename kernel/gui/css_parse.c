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
    return -1;
}

static int parse_color(const char *v, struct css_color *c) {
    if (!v || !c)
        return -1;
    while (*v == ' ')
        v++;
    if (v[0] == '#' && strlen(v) >= 7) {
        int r1 = hex_digit(v[1]), r0 = hex_digit(v[2]);
        int g1 = hex_digit(v[3]), g0 = hex_digit(v[4]);
        int b1 = hex_digit(v[5]), b0 = hex_digit(v[6]);
        if (r1 < 0 || r0 < 0 || g1 < 0 || g0 < 0 || b1 < 0 || b0 < 0)
            return -1;
        c->r = (uint8_t)((r1 << 4) | r0);
        c->g = (uint8_t)((g1 << 4) | g0);
        c->b = (uint8_t)((b1 << 4) | b0);
        c->set = 1;
        return 0;
    }
    if (v[0] == '#' && strlen(v) >= 4) {
        int r = hex_digit(v[1]), g = hex_digit(v[2]), b = hex_digit(v[3]);
        if (r < 0 || g < 0 || b < 0)
            return -1;
        c->r = (uint8_t)((r << 4) | r);
        c->g = (uint8_t)((g << 4) | g);
        c->b = (uint8_t)((b << 4) | b);
        c->set = 1;
        return 0;
    }
    static const struct { const char *n; uint8_t r, g, b; } named[] = {
        {"black", 0, 0, 0},     {"white", 255, 255, 255}, {"red", 200, 40, 40},
        {"green", 40, 160, 80}, {"blue", 60, 120, 220},   {"gray", 128, 128, 128},
        {"grey", 128, 128, 128}, {"silver", 192, 192, 192}, {"navy", 0, 0, 128},
        {"teal", 0, 128, 128},  {"maroon", 128, 0, 0},    {"purple", 128, 0, 128},
        {"olive", 128, 128, 0}, {"lime", 0, 200, 0},      {"aqua", 0, 200, 200},
        {"orange", 255, 140, 0}, {"transparent", 0, 0, 0},
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (!strcmp(v, named[i].n)) {
            c->r = named[i].r;
            c->g = named[i].g;
            c->b = named[i].b;
            c->set = strcmp(v, "transparent") ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

static int parse_px(const char *val) {
    int n = 0;
    for (const char *p = val; *p >= '0' && *p <= '9'; p++)
        n = n * 10 + (*p - '0');
    return n;
}

static void apply_decl(struct css_style *st, const char *prop, const char *val) {
    if (!strcmp(prop, "color"))
        parse_color(val, &st->color);
    else if (!strcmp(prop, "background") || !strcmp(prop, "background-color"))
        parse_color(val, &st->background);
    else if (!strcmp(prop, "display")) {
        if (!strcmp(val, "none"))
            st->display = 2;
        else if (!strcmp(val, "inline") || !strcmp(val, "inline-block"))
            st->display = 1;
        else
            st->display = 0;
    } else if (!strcmp(prop, "font-size")) {
        int n = parse_px(val);
        if (n > 0)
            st->font_size = n;
    } else if (!strcmp(prop, "margin") || !strcmp(prop, "margin-top") ||
               !strcmp(prop, "margin-bottom")) {
        st->margin = parse_px(val);
    } else if (!strcmp(prop, "padding") || !strcmp(prop, "padding-left") ||
               !strcmp(prop, "padding-right")) {
        st->padding = parse_px(val);
    } else if (!strcmp(prop, "width")) {
        st->width = parse_px(val);
    } else if (!strcmp(prop, "max-width")) {
        st->max_width = parse_px(val);
    } else if (!strcmp(prop, "height")) {
        st->height = parse_px(val);
    } else if (!strcmp(prop, "text-align")) {
        if (!strcmp(val, "center"))
            st->text_align = 1;
        else if (!strcmp(val, "right"))
            st->text_align = 2;
        else
            st->text_align = 0;
    } else if (!strcmp(prop, "border") || !strcmp(prop, "border-width")) {
        int n = parse_px(val);
        if (n > 0)
            st->border = n;
        else if (strstr(val, "solid") || strstr(val, "1px"))
            st->border = 1;
    }
}

static int selector_specificity(const char *sel) {
    int spec = 0;
    for (const char *p = sel; *p; p++) {
        if (*p == '#')
            spec += 100;
        else if (*p == '.')
            spec += 10;
        else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
            if (p == sel || p[-1] == ' ' || p[-1] == '>')
                spec += 1;
        }
    }
    return spec;
}

void css_parse_stylesheet(struct css_sheet *s, const char *css) {
    if (!s || !css)
        return;
    const char *p = css;
    while (*p && s->nrules < CSS_MAX_RULES) {
        while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')
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
        if (*p == '@') { /* skip @media/@import blocks lite */
            while (*p && *p != '{' && *p != ';')
                p++;
            if (*p == ';') {
                p++;
                continue;
            }
            if (*p == '{') {
                int depth = 1;
                p++;
                while (*p && depth) {
                    if (*p == '{')
                        depth++;
                    else if (*p == '}')
                        depth--;
                    p++;
                }
            }
            continue;
        }
        char sel[64];
        size_t si = 0;
        while (*p && *p != '{' && si + 1 < sizeof(sel)) {
            if (*p != '\n' && *p != '\r')
                sel[si++] = *p;
            p++;
        }
        sel[si] = '\0';
        while (si > 0 && (sel[si - 1] == ' ' || sel[si - 1] == '\t'))
            sel[--si] = '\0';
        /* Take first selector if comma-separated. */
        for (size_t i = 0; i < si; i++) {
            if (sel[i] == ',') {
                sel[i] = '\0';
                break;
            }
        }
        if (*p != '{')
            break;
        p++;
        struct css_rule *r = &s->rules[s->nrules];
        memset(r, 0, sizeof(*r));
        snprintf(r->selector, sizeof(r->selector), "%s", sel);
        r->specificity = selector_specificity(sel);
        while (*p && *p != '}') {
            while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r' || *p == ';')
                p++;
            if (*p == '}' || !*p)
                break;
            char prop[40], val[80];
            size_t pi = 0;
            while (*p && *p != ':' && pi + 1 < sizeof(prop)) {
                if (*p != ' ' && *p != '\n' && *p != '\r')
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
                if (*p != '\n' && *p != '\r')
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
