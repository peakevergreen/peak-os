#include "dom.h"
#include "heap.h"
#include "util.h"

void dom_doc_init(struct dom_document *doc) {
    memset(doc, 0, sizeof(*doc));
    doc->root = -1;
    doc->body = -1;
}

void dom_doc_clear(struct dom_document *doc) {
    for (int i = 0; i < doc->nscripts; i++) {
        kfree(doc->scripts[i].code);
        doc->scripts[i].code = NULL;
    }
    memset(doc, 0, sizeof(*doc));
    doc->root = -1;
    doc->body = -1;
}

struct dom_node *dom_node(struct dom_document *doc, int id) {
    if (!doc || id < 0 || id >= doc->nnodes || !doc->nodes[id].used)
        return NULL;
    return &doc->nodes[id];
}

static int alloc_node(struct dom_document *doc) {
    if (doc->nnodes >= DOM_MAX_NODES)
        return -1;
    int id = doc->nnodes++;
    memset(&doc->nodes[id], 0, sizeof(doc->nodes[id]));
    doc->nodes[id].used = 1;
    doc->nodes[id].id = id;
    doc->nodes[id].parent = -1;
    doc->nodes[id].first_child = -1;
    doc->nodes[id].next_sibling = -1;
    return id;
}

int dom_create_element(struct dom_document *doc, const char *tag) {
    int id = alloc_node(doc);
    if (id < 0)
        return -1;
    doc->nodes[id].type = DOM_ELEMENT;
    snprintf(doc->nodes[id].tag, sizeof(doc->nodes[id].tag), "%s", tag ? tag : "div");
    /* lowercase tag */
    for (char *p = doc->nodes[id].tag; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
    doc->dirty = 1;
    return id;
}

int dom_create_text(struct dom_document *doc, const char *text) {
    int id = alloc_node(doc);
    if (id < 0)
        return -1;
    doc->nodes[id].type = DOM_TEXT;
    snprintf(doc->nodes[id].text, sizeof(doc->nodes[id].text), "%s", text ? text : "");
    doc->dirty = 1;
    return id;
}

int dom_append_child(struct dom_document *doc, int parent, int child) {
    struct dom_node *p = dom_node(doc, parent);
    struct dom_node *c = dom_node(doc, child);
    if (!p || !c || p->type == DOM_TEXT)
        return -1;
    c->parent = parent;
    if (p->first_child < 0) {
        p->first_child = child;
    } else {
        int n = p->first_child;
        while (doc->nodes[n].next_sibling >= 0)
            n = doc->nodes[n].next_sibling;
        doc->nodes[n].next_sibling = child;
    }
    doc->dirty = 1;
    return 0;
}

int dom_set_attr(struct dom_document *doc, int id, const char *name, const char *value) {
    struct dom_node *n = dom_node(doc, id);
    if (!n || !name)
        return -1;
    for (int i = 0; i < n->nattrs; i++) {
        if (!strcmp(n->attrs[i].name, name)) {
            snprintf(n->attrs[i].value, sizeof(n->attrs[i].value), "%s", value ? value : "");
            doc->dirty = 1;
            return 0;
        }
    }
    if (n->nattrs >= DOM_ATTR_MAX)
        return -1;
    snprintf(n->attrs[n->nattrs].name, sizeof(n->attrs[0].name), "%s", name);
    snprintf(n->attrs[n->nattrs].value, sizeof(n->attrs[0].value), "%s", value ? value : "");
    n->nattrs++;
    doc->dirty = 1;
    return 0;
}

const char *dom_get_attr(struct dom_document *doc, int id, const char *name) {
    struct dom_node *n = dom_node(doc, id);
    if (!n || !name)
        return NULL;
    for (int i = 0; i < n->nattrs; i++)
        if (!strcmp(n->attrs[i].name, name))
            return n->attrs[i].value;
    return NULL;
}

int dom_set_text(struct dom_document *doc, int id, const char *text) {
    struct dom_node *n = dom_node(doc, id);
    if (!n)
        return -1;
    if (n->type == DOM_TEXT) {
        snprintf(n->text, sizeof(n->text), "%s", text ? text : "");
    } else {
        /* replace children with one text node */
        n->first_child = -1;
        int t = dom_create_text(doc, text);
        if (t < 0)
            return -1;
        return dom_append_child(doc, id, t);
    }
    doc->dirty = 1;
    return 0;
}

static int tag_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

int dom_get_element_by_id(struct dom_document *doc, const char *id) {
    if (!doc || !id)
        return -1;
    for (int i = 0; i < doc->nnodes; i++) {
        if (!doc->nodes[i].used || doc->nodes[i].type != DOM_ELEMENT)
            continue;
        const char *v = dom_get_attr(doc, i, "id");
        if (v && !strcmp(v, id))
            return i;
    }
    return -1;
}

int dom_query_selector(struct dom_document *doc, int root, const char *sel) {
    if (!doc || !sel)
        return -1;
    if (sel[0] == '#')
        return dom_get_element_by_id(doc, sel + 1);
    if (sel[0] == '.') {
        const char *cls = sel + 1;
        for (int i = 0; i < doc->nnodes; i++) {
            if (!doc->nodes[i].used || doc->nodes[i].type != DOM_ELEMENT)
                continue;
            if (root >= 0 && i != root) {
                /* allow any descendant — simple full scan */
            }
            const char *c = dom_get_attr(doc, i, "class");
            if (c) {
                size_t n = strlen(cls);
                for (const char *p = c; *p; p++) {
                    size_t i2 = 0;
                    while (i2 < n && p[i2] == cls[i2])
                        i2++;
                    if (i2 == n)
                        return i;
                }
            }
        }
        return -1;
    }
    /* tag name */
    for (int i = 0; i < doc->nnodes; i++) {
        if (!doc->nodes[i].used || doc->nodes[i].type != DOM_ELEMENT)
            continue;
        if (tag_eq(doc->nodes[i].tag, sel))
            return i;
    }
    (void)root;
    return -1;
}

void dom_set_inner_html(struct dom_document *doc, int id, const char *html) {
    struct dom_node *n = dom_node(doc, id);
    if (!n || n->type != DOM_ELEMENT)
        return;
    n->first_child = -1;
    if (!html || !html[0]) {
        doc->dirty = 1;
        return;
    }
    /* minimal: create a text node of stripped content */
    char buf[DOM_TEXT_MAX];
    size_t o = 0;
    for (const char *p = html; *p && o + 1 < sizeof(buf); p++) {
        if (*p == '<') {
            while (*p && *p != '>')
                p++;
            continue;
        }
        buf[o++] = *p;
    }
    buf[o] = '\0';
    int t = dom_create_text(doc, buf);
    if (t >= 0)
        dom_append_child(doc, id, t);
}

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
    return tag_eq(tag, "br") || tag_eq(tag, "hr") || tag_eq(tag, "img") ||
           tag_eq(tag, "input") || tag_eq(tag, "meta") || tag_eq(tag, "link") ||
           tag_eq(tag, "source");
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

            if (tag_eq(tag, "script")) {
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

            if (tag_eq(tag, "style") || tag_eq(tag, "head") || tag_eq(tag, "svg")) {
                const char *close_name = tag_eq(tag, "style") ? "style"
                                         : tag_eq(tag, "head") ? "head"
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

            if (tag_eq(tag, "title") && !closing) {
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
                    if (tag_eq(doc->nodes[top].tag, tag))
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

int dom_collect_text(struct dom_document *doc, int id, char *out, size_t cap) {
    if (!out || !cap)
        return 0;
    out[0] = '\0';
    struct dom_node *n = dom_node(doc, id);
    if (!n)
        return 0;
    if (n->type == DOM_TEXT) {
        snprintf(out, cap, "%s", n->text);
        return (int)strlen(out);
    }
    size_t o = 0;
    for (int c = n->first_child; c >= 0; c = doc->nodes[c].next_sibling) {
        char piece[DOM_TEXT_MAX];
        dom_collect_text(doc, c, piece, sizeof(piece));
        size_t pl = strlen(piece);
        if (!pl)
            continue;
        if (o && o + 1 < cap)
            out[o++] = ' ';
        for (size_t i = 0; i < pl && o + 1 < cap; i++)
            out[o++] = piece[i];
    }
    out[o] = '\0';
    return (int)o;
}
