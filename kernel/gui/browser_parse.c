#include "browser_internal.h"
#include "theme.h"
#include "util.h"

static const char *find_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        for (; i < n; i++) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b)
                break;
        }
        if (i == n)
            return p;
    }
    return NULL;
}

static int tag_is(const char *p, const char *name) {
    size_t n = strlen(name);
    if (strncmp(p, name, n) != 0)
        return 0;
    char c = p[n];
    return c == '>' || c == ' ' || c == '\t' || c == '/' || c == '\n' || c == '\r';
}

static const char *skip_tag(const char *p) {
    while (*p && *p != '>')
        p++;
    if (*p == '>')
        p++;
    return p;
}

static const char *skip_until_close(const char *p, const char *close) {
    const char *f = find_ci(p, close);
    if (!f)
        return p + strlen(p);
    return skip_tag(f);
}

static int is_noncontent_open(const char *p, int extended) {
    if (tag_is(p, "script") || tag_is(p, "style") || tag_is(p, "svg") ||
        tag_is(p, "template") || tag_is(p, "head") || tag_is(p, "iframe"))
        return 1;
    if (extended && (tag_is(p, "link") || tag_is(p, "meta") || tag_is(p, "img") ||
                     tag_is(p, "source") || tag_is(p, "path") || tag_is(p, "use")))
        return 1;
    return 0;
}

static const char *skip_noncontent_open(const char *p, int extended) {
    if (!is_noncontent_open(p, extended))
        return NULL;
    if (tag_is(p, "script"))
        return skip_until_close(p, "</script");
    if (tag_is(p, "style"))
        return skip_until_close(p, "</style");
    if (tag_is(p, "svg"))
        return skip_until_close(p, "</svg");
    if (tag_is(p, "template"))
        return skip_until_close(p, "</template");
    if (tag_is(p, "head"))
        return skip_until_close(p, "</head");
    return skip_tag(p);
}

static int decode_entity(const char **pp, char *out) {
    const char *p = *pp;
    if (*p != '&')
        return 0;
    p++;
    if (!strncmp(p, "amp;", 4)) {
        *out = '&';
        *pp = p + 4;
        return 1;
    }
    if (!strncmp(p, "lt;", 3)) {
        *out = '<';
        *pp = p + 3;
        return 1;
    }
    if (!strncmp(p, "gt;", 3)) {
        *out = '>';
        *pp = p + 3;
        return 1;
    }
    if (!strncmp(p, "quot;", 5)) {
        *out = '"';
        *pp = p + 5;
        return 1;
    }
    if (!strncmp(p, "nbsp;", 5)) {
        *out = ' ';
        *pp = p + 5;
        return 1;
    }
    if (!strncmp(p, "mdash;", 6) || !strncmp(p, "ndash;", 6)) {
        *out = '-';
        *pp = p + 6;
        return 1;
    }
    if (*p == '#') {
        p++;
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        if (*p == ';')
            p++;
        *out = (v > 0 && v < 127) ? (char)v : '?';
        *pp = p;
        return 1;
    }
    return 0;
}

static void trim_inplace(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n'))
        s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    if (i) {
        size_t j = 0;
        while (s[i])
            s[j++] = s[i++];
        s[j] = '\0';
    }
}

static void collect_text(const char **pp, char *buf, size_t cap, int preserve_space) {
    size_t o = 0;
    int space = 1;
    const char *p = *pp;
    while (*p && *p != '<' && o + 1 < cap) {
        char c;
        if (*p == '&') {
            if (!decode_entity(&p, &c))
                c = *p++;
        } else {
            c = *p++;
        }
        if (!preserve_space && (c == '\n' || c == '\r' || c == '\t'))
            c = ' ';
        if (!preserve_space && c == ' ') {
            if (space)
                continue;
            space = 1;
        } else {
            space = 0;
        }
        buf[o++] = c;
    }
    buf[o] = '\0';
    *pp = p;
    trim_inplace(buf);
}

void browser_clear_blocks(struct br_tab *t) {
    t->nblocks = 0;
    memset(t->blocks, 0, sizeof(t->blocks));
}

int browser_add_block(struct br_tab *t, enum br_kind kind, const char *text) {
    if (t->nblocks >= BR_MAX_BLOCKS)
        return -1;
    t->blocks[t->nblocks].kind = (uint8_t)kind;
    if (text) {
        size_t i = 0;
        for (; text[i] && i + 1 < BR_TEXT_MAX; i++)
            t->blocks[t->nblocks].text[i] = text[i];
        t->blocks[t->nblocks].text[i] = '\0';
    } else {
        t->blocks[t->nblocks].text[0] = '\0';
    }
    t->nblocks++;
    return 0;
}

static void flush_text(struct br_tab *t, enum br_kind kind, char *buf) {
    trim_inplace(buf);
    if (!buf[0])
        return;
    const char *p = buf;
    while (*p) {
        char chunk[BR_TEXT_MAX];
        size_t i = 0;
        while (p[i] && i + 1 < BR_TEXT_MAX)
            i++;
        if (p[i]) {
            size_t brk = i;
            while (brk > 40 && p[brk] != ' ')
                brk--;
            if (p[brk] == ' ')
                i = brk;
        }
        memcpy(chunk, p, i);
        chunk[i] = '\0';
        trim_inplace(chunk);
        if (chunk[0])
            browser_add_block(t, kind, chunk);
        p += i;
        while (*p == ' ')
            p++;
    }
    buf[0] = '\0';
}

int browser_content_blocks(struct br_tab *t) {
    int n = 0;
    for (int i = 0; i < t->nblocks; i++) {
        uint8_t k = t->blocks[i].kind;
        if (k != BR_SPACER && k != BR_HR && t->blocks[i].text[0])
            n++;
    }
    return n;
}

void browser_init_page_colors(struct br_tab *t, const char *html) {
    (void)html;
    const struct peak_theme *th = theme_get();
    t->page_bg = th->bg;
    t->page_fg = th->fg;
    t->page_muted = th->dim;
    t->page_accent = th->accent;
    t->page_link = th->accent;
    t->page_surface = th->surface;
    t->colors_set = 1;
}

void browser_extract_title(struct br_tab *t, const char *html, int tab_index) {
    const char *p = find_ci(html, "<title");
    if (!p) {
        snprintf(t->title, sizeof(t->title), "Tab %d", tab_index + 1);
        return;
    }
    while (*p && *p != '>')
        p++;
    if (*p == '>')
        p++;
    size_t i = 0;
    while (*p && *p != '<' && i + 1 < BR_TITLE_MAX) {
        char c = *p++;
        if (c == '\n' || c == '\r')
            continue;
        t->title[i++] = c;
    }
    t->title[i] = '\0';
    if (!i)
        snprintf(t->title, sizeof(t->title), "Tab %d", tab_index + 1);
}

void browser_reader_fallback(struct br_tab *t, const char *html, int tab_index) {
    browser_clear_blocks(t);
    browser_extract_title(t, html, tab_index);
    browser_init_page_colors(t, html);
    browser_add_block(t, BR_H2, t->title[0] ? t->title : "Page");
    browser_add_block(t, BR_P, "(reader mode — scripts skipped/over budget; extracted text)");
    browser_add_block(t, BR_HR, "");

    const char *p = html;
    const char *body = find_ci(html, "<body");
    if (body)
        p = skip_tag(body);

    enum br_kind curk = BR_P;
    while (*p && t->nblocks < BR_MAX_BLOCKS) {
        if (*p == '<') {
            p++;
            int closing = 0;
            if (*p == '/') {
                closing = 1;
                p++;
            }
            if (!closing) {
                const char *skipped = skip_noncontent_open(p, 0);
                if (skipped) {
                    p = skipped;
                    continue;
                }
            }
            if (!closing && tag_is(p, "a"))
                curk = BR_LINK;
            else if (!closing && (tag_is(p, "h1") || tag_is(p, "h2")))
                curk = BR_H2;
            else if (!closing && (tag_is(p, "h3") || tag_is(p, "h4") || tag_is(p, "li")))
                curk = tag_is(p, "li") ? BR_LI : BR_H3;
            else if (closing && (tag_is(p, "a") || tag_is(p, "h1") || tag_is(p, "h2") ||
                                 tag_is(p, "h3") || tag_is(p, "li") || tag_is(p, "p")))
                curk = BR_P;
            p = skip_tag(p);
            continue;
        }
        char piece[BR_TEXT_MAX];
        collect_text(&p, piece, sizeof(piece), 0);
        if (piece[0] && strlen(piece) >= 3)
            flush_text(t, curk, piece);
    }
}

void browser_parse_html(struct br_tab *t, const char *html, int tab_index) {
    browser_clear_blocks(t);
    browser_extract_title(t, html, tab_index);
    browser_init_page_colors(t, html);

    const char *p = html;
    const char *body = find_ci(html, "<body");
    if (body)
        p = skip_tag(body);

    enum br_kind curk = BR_P;
    int pending_break = 0;

    while (*p && t->nblocks < BR_MAX_BLOCKS) {
        if (*p == '<') {
            p++;
            int closing = 0;
            if (*p == '/') {
                closing = 1;
                p++;
            }

            if (!closing) {
                const char *skipped = skip_noncontent_open(p, 1);
                if (skipped) {
                    p = skipped;
                    continue;
                }
            }

            if (!closing && tag_is(p, "br")) {
                pending_break = 1;
                p = skip_tag(p);
                continue;
            }
            if (!closing && tag_is(p, "hr")) {
                browser_add_block(t, BR_HR, "");
                p = skip_tag(p);
                continue;
            }
            if (!closing && tag_is(p, "h1")) {
                curk = BR_H1;
                p = skip_tag(p);
                continue;
            }
            if (!closing && tag_is(p, "h2")) {
                curk = BR_H2;
                p = skip_tag(p);
                continue;
            }
            if (!closing && (tag_is(p, "h3") || tag_is(p, "h4") || tag_is(p, "h5"))) {
                curk = BR_H3;
                p = skip_tag(p);
                continue;
            }
            if (!closing && tag_is(p, "li")) {
                curk = BR_LI;
                p = skip_tag(p);
                continue;
            }
            if (!closing && tag_is(p, "a")) {
                curk = BR_LINK;
                p = skip_tag(p);
                continue;
            }
            if (!closing && (tag_is(p, "code") || tag_is(p, "pre"))) {
                curk = BR_CODE;
                p = skip_tag(p);
                continue;
            }
            if (!closing && tag_is(p, "blockquote")) {
                curk = BR_QUOTE;
                p = skip_tag(p);
                continue;
            }
            if (!closing && (tag_is(p, "p") || tag_is(p, "article") ||
                             tag_is(p, "td") || tag_is(p, "th") ||
                             tag_is(p, "figcaption") || tag_is(p, "label"))) {
                curk = BR_P;
                p = skip_tag(p);
                continue;
            }
            if (!closing && (tag_is(p, "div") || tag_is(p, "span") || tag_is(p, "main") ||
                             tag_is(p, "section") || tag_is(p, "nav") || tag_is(p, "header") ||
                             tag_is(p, "footer") || tag_is(p, "ul") || tag_is(p, "ol") ||
                             tag_is(p, "noscript") || tag_is(p, "button") || tag_is(p, "strong") ||
                             tag_is(p, "em") || tag_is(p, "b") || tag_is(p, "i") ||
                             tag_is(p, "small") || tag_is(p, "time") || tag_is(p, "font"))) {
                p = skip_tag(p);
                continue;
            }

            if (closing) {
                if (tag_is(p, "h1") || tag_is(p, "h2") || tag_is(p, "h3") ||
                    tag_is(p, "h4") || tag_is(p, "h5") || tag_is(p, "p") ||
                    tag_is(p, "li") || tag_is(p, "blockquote") || tag_is(p, "tr") ||
                    tag_is(p, "article") || tag_is(p, "br")) {
                    pending_break = 1;
                    curk = BR_P;
                } else if (tag_is(p, "a") || tag_is(p, "code") || tag_is(p, "pre") ||
                           tag_is(p, "span") || tag_is(p, "strong") || tag_is(p, "em") ||
                           tag_is(p, "b") || tag_is(p, "i")) {
                    curk = BR_P;
                }
            }
            p = skip_tag(p);
            continue;
        }

        if (pending_break) {
            browser_add_block(t, BR_SPACER, "");
            pending_break = 0;
        }

        char piece[BR_TEXT_MAX];
        collect_text(&p, piece, sizeof(piece), curk == BR_CODE);
        if (piece[0])
            flush_text(t, curk, piece);
    }

    if (browser_content_blocks(t) < 2)
        browser_reader_fallback(t, html, tab_index);
}
