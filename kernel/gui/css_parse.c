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
