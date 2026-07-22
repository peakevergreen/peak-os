#include "dom.h"
#include "dom_internal.h"
#include "heap.h"
#include "util.h"

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

/* Decode &name;/&#N; at *pp into a single char. Returns 1 and advances *pp
 * past the entity on success. */
static int decode_entity(const char **pp, char *out) {
    static const struct { const char *name; char ch; } tab[] = {
        { "amp;", '&' },   { "lt;", '<' },    { "gt;", '>' },
        { "quot;", '"' },  { "apos;", '\'' }, { "nbsp;", ' ' },
        { "mdash;", '-' }, { "ndash;", '-' }, { "hellip;", '.' },
        { "rsquo;", '\'' },{ "lsquo;", '\'' },{ "ldquo;", '"' },
        { "rdquo;", '"' }, { "raquo;", '>' }, { "laquo;", '<' },
        { "bull;", '*' },  { "middot;", '*' },{ "copy;", 'c' },
        { "trade;", 't' }, { "reg;", 'r' },   { "amp", '&' },
    };
    const char *p = *pp;
    if (*p != '&')
        return 0;
    p++;
    for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        size_t n = strlen(tab[i].name);
        if (!strncmp(p, tab[i].name, n)) {
            *out = tab[i].ch;
            *pp = p + n;
            return 1;
        }
    }
    if (*p == '#') {
        p++;
        int hex = 0;
        if (*p == 'x' || *p == 'X') {
            hex = 1;
            p++;
        }
        int v = 0, digits = 0;
        while (digits < 7) {
            char c = *p;
            int d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (hex && c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else
                break;
            v = v * (hex ? 16 : 10) + d;
            p++;
            digits++;
        }
        if (!digits)
            return 0;
        if (*p == ';')
            p++;
        if (v == 160)
            v = ' ';
        *out = (v >= 32 && v < 127) ? (char)v : '?';
        *pp = p;
        return 1;
    }
    return 0;
}

static int parse_attrs(struct dom_document *doc, int id, const char **pp) {
    const char *p = *pp;
    while (*p && *p != '>' && *p != '/') {
        p = skip_ws(p);
        if (*p == '>' || *p == '/' || !*p)
            break;
        char name[DOM_NAME_MAX];
        size_t ni = 0;
        while (*p && *p != '=' && *p != ' ' && *p != '>' && *p != '/' &&
               ni + 1 < sizeof(name)) {
            char c = *p++;
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
            name[ni++] = c;
        }
        name[ni] = '\0';
        /* Over-long name: consume the rest so we resume at a boundary. */
        while (*p && *p != '=' && *p != ' ' && *p != '>' && *p != '/')
            p++;
        p = skip_ws(p);
        char value[96] = "true";
        if (*p == '=') {
            p++;
            p = skip_ws(p);
            char q = 0;
            if (*p == '"' || *p == '\'') {
                q = *p++;
                size_t vi = 0;
                while (*p && *p != q && vi + 1 < sizeof(value))
                    value[vi++] = *p++;
                value[vi] = '\0';
                /* Truncated value: still consume to the closing quote,
                 * otherwise the tail is re-parsed as attrs/text. */
                while (*p && *p != q)
                    p++;
                if (*p == q)
                    p++;
            } else {
                size_t vi = 0;
                while (*p && *p != ' ' && *p != '>' && *p != '/' && vi + 1 < sizeof(value))
                    value[vi++] = *p++;
                value[vi] = '\0';
                while (*p && *p != ' ' && *p != '>' && *p != '/')
                    p++;
            }
        }
        if (name[0])
            dom_set_attr(doc, id, name, value);
    }
    *pp = p;
    return 0;
}

static int is_void_tag(const char *tag) {
    return dom_tag_eq(tag, "br") || dom_tag_eq(tag, "hr") || dom_tag_eq(tag, "img") ||
           dom_tag_eq(tag, "input") || dom_tag_eq(tag, "meta") || dom_tag_eq(tag, "link") ||
           dom_tag_eq(tag, "source");
}

/* True if p points at </tag... (case-insensitive), with a proper tag boundary
 * so "</svg>" is not mistaken for "</script>". */
static int at_close_tag(const char *p, const char *tag) {
    if (p[0] != '<' || p[1] != '/')
        return 0;
    p += 2;
    size_t n = strlen(tag);
    for (size_t i = 0; i < n; i++) {
        char a = p[i], b = tag[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    char c = p[n];
    return c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/';
}

static const char *skip_raw_until_close(const char *p, const char *tag) {
    while (*p) {
        if (at_close_tag(p, tag)) {
            while (*p && *p != '>')
                p++;
            if (*p == '>')
                p++;
            return p;
        }
        p++;
    }
    return p;
}

int dom_parse_html(struct dom_document *doc, const char *html, const char *url) {
    dom_doc_clear(doc);
    if (url)
        snprintf(doc->url, sizeof(doc->url), "%s", url);
    int doc_el = dom_create_element(doc, "html");
    doc->root = doc_el;
    int body = dom_create_element(doc, "body");
    doc->body = body;
    dom_append_child(doc, doc_el, body);

    int stack[64];
    int sp = 0;
    stack[sp++] = body;

    const char *p = html;
    /* skip to body if present */
    const char *bp = html;
    while (*bp) {
        if (bp[0] == '<' && (bp[1] == 'b' || bp[1] == 'B') &&
            (bp[2] == 'o' || bp[2] == 'O')) {
            while (*bp && *bp != '>')
                bp++;
            if (*bp == '>')
                bp++;
            p = bp;
            break;
        }
        bp++;
    }

    while (*p && sp > 0 && doc->nnodes < DOM_MAX_NODES - 4) {
        if (*p == '<') {
            p++;
            int closing = 0;
            if (*p == '/') {
                closing = 1;
                p++;
            }
            if (*p == '!' || *p == '?') {
                while (*p && *p != '>')
                    p++;
                if (*p == '>')
                    p++;
                continue;
            }
            char tag[DOM_NAME_MAX];
            size_t ti = 0;
            while (*p && *p != ' ' && *p != '>' && *p != '/' && ti + 1 < sizeof(tag)) {
                char c = *p++;
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
                tag[ti++] = c;
            }
            tag[ti] = '\0';

            if (dom_tag_eq(tag, "script")) {
                if (!closing) {
                    int ext = 0;
                    char src[160] = "";
                    /* parse attrs quickly */
                    const char *ap = p;
                    parse_attrs(doc, body, &ap); /* dummy attrs on body — fix below */
                    p = ap;
                    /* re-scan for src= */
                    const char *q = p;
                    while (q > html && *q != '<')
                        q--;
                    /* simpler: scan from tag start */
                    (void)ext;
                    const char *attrs_start = p;
                    while (*p && *p != '>') {
                        if ((p[0] == 's' || p[0] == 'S') && (p[1] == 'r' || p[1] == 'R') &&
                            (p[2] == 'c' || p[2] == 'C') && p[3] == '=') {
                            p += 4;
                            char qq = 0;
                            if (*p == '"' || *p == '\'')
                                qq = *p++;
                            size_t si = 0;
                            while (*p && ((qq && *p != qq) || (!qq && *p != ' ' && *p != '>')) &&
                                   si + 1 < sizeof(src))
                                src[si++] = *p++;
                            src[si] = '\0';
                            if (qq && *p == qq)
                                p++;
                            ext = 1;
                        } else {
                            p++;
                        }
                    }
                    (void)attrs_start;
                    if (*p == '>')
                        p++;
                    if (doc->nscripts < DOM_SCRIPT_MAX) {
                        struct dom_script *sc = &doc->scripts[doc->nscripts++];
                        memset(sc, 0, sizeof(*sc));
                        sc->used = 1;
                        if (ext && src[0]) {
                            sc->external = 1;
                            snprintf(sc->src, sizeof(sc->src), "%s", src);
                        } else {
                            const char *start = p;
                            const char *end = p;
                            while (*end && !at_close_tag(end, "script"))
                                end++;
                            size_t len = (size_t)(end - start);
                            sc->code = kmalloc(len + 1);
                            if (sc->code) {
                                memcpy(sc->code, start, len);
                                sc->code[len] = '\0';
                                sc->code_len = len;
                            }
                            p = end;
                        }
                    }
                    p = skip_raw_until_close(p, "script");
                } else {
                    while (*p && *p != '>')
                        p++;
                    if (*p == '>')
                        p++;
                }
                continue;
            }

            if (dom_tag_eq(tag, "style") || dom_tag_eq(tag, "head") || dom_tag_eq(tag, "svg")) {
                const char *close_name = dom_tag_eq(tag, "style") ? "style"
                                         : dom_tag_eq(tag, "head") ? "head"
                                                                    : "svg";
                if (!closing) {
                    while (*p && *p != '>')
                        p++;
                    if (*p == '>')
                        p++;
                    p = skip_raw_until_close(p, close_name);
                }
                continue;
            }

            if (dom_tag_eq(tag, "title") && !closing) {
                while (*p && *p != '>')
                    p++;
                if (*p == '>')
                    p++;
                size_t ti2 = 0;
                while (*p && *p != '<' && ti2 + 1 < sizeof(doc->title))
                    doc->title[ti2++] = *p++;
                doc->title[ti2] = '\0';
                while (*p && *p != '>')
                    p++;
                if (*p == '>')
                    p++;
                continue;
            }

            if (closing) {
                /* pop until matching tag */
                while (sp > 1) {
                    int top = stack[sp - 1];
                    sp--;
                    if (dom_tag_eq(doc->nodes[top].tag, tag))
                        break;
                }
                while (*p && *p != '>')
                    p++;
                if (*p == '>')
                    p++;
                continue;
            }

            int el = dom_create_element(doc, tag);
            if (el < 0)
                break;
            parse_attrs(doc, el, &p);
            int self = 0;
            if (*p == '/') {
                self = 1;
                p++;
            }
            if (*p == '>')
                p++;
            dom_append_child(doc, stack[sp - 1], el);
            if (!self && !is_void_tag(tag) && sp < 64)
                stack[sp++] = el;
            continue;
        }

        /* text */
        char text[DOM_TEXT_MAX];
        size_t ti = 0;
        while (*p && *p != '<' && ti + 1 < sizeof(text)) {
            char c;
            if (*p == '&') {
                if (!decode_entity(&p, &c))
                    c = *p++;
            } else {
                c = *p++;
            }
            if (c == '\n' || c == '\r' || c == '\t')
                c = ' ';
            if (c == ' ' && (ti == 0 || text[ti - 1] == ' '))
                continue;
            text[ti++] = c;
        }
        text[ti] = '\0';
        if (ti > 0) {
            int t = dom_create_text(doc, text);
            if (t >= 0)
                dom_append_child(doc, stack[sp - 1], t);
        }
    }
    doc->dirty = 1;
    return 0;
}
