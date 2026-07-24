#include "dom.h"
#include "dom_internal.h"
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

/* Whole class-token match (space/tab separated), not substring. */
static int class_has_token(const char *class_attr, const char *cls, size_t cls_len) {
    if (!class_attr || !cls || !cls_len)
        return 0;
    for (const char *p = class_attr; *p;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if ((size_t)(p - start) == cls_len && !strncmp(start, cls, cls_len))
            return 1;
    }
    return 0;
}

/* Preorder successor under `root` (inclusive walk starts at root). */
static int dom_next_in_subtree(struct dom_document *doc, int root, int cur) {
    struct dom_node *n = &doc->nodes[cur];
    if (n->first_child >= 0)
        return n->first_child;
    while (cur >= 0 && cur != root) {
        if (doc->nodes[cur].next_sibling >= 0)
            return doc->nodes[cur].next_sibling;
        cur = doc->nodes[cur].parent;
    }
    return -1;
}

static int query_class_in_subtree(struct dom_document *doc, int root, const char *cls,
                                  size_t cls_len) {
    for (int i = root; i >= 0; i = dom_next_in_subtree(doc, root, i)) {
        if (!doc->nodes[i].used || doc->nodes[i].type != DOM_ELEMENT)
            continue;
        const char *c = dom_get_attr(doc, i, "class");
        if (c && class_has_token(c, cls, cls_len))
            return i;
    }
    return -1;
}

static int query_tag_in_subtree(struct dom_document *doc, int root, const char *tag) {
    for (int i = root; i >= 0; i = dom_next_in_subtree(doc, root, i)) {
        if (!doc->nodes[i].used || doc->nodes[i].type != DOM_ELEMENT)
            continue;
        if (dom_tag_eq(doc->nodes[i].tag, tag))
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
        size_t cls_len = strlen(cls);
        if (!cls_len)
            return -1;
        if (root >= 0)
            return query_class_in_subtree(doc, root, cls, cls_len);
        /* No root: scan live elements only (still token-match, not substring). */
        for (int i = 0; i < doc->nnodes; i++) {
            if (!doc->nodes[i].used || doc->nodes[i].type != DOM_ELEMENT)
                continue;
            const char *c = dom_get_attr(doc, i, "class");
            if (c && class_has_token(c, cls, cls_len))
                return i;
        }
        return -1;
    }
    /* tag name */
    if (root >= 0)
        return query_tag_in_subtree(doc, root, sel);
    for (int i = 0; i < doc->nnodes; i++) {
        if (!doc->nodes[i].used || doc->nodes[i].type != DOM_ELEMENT)
            continue;
        if (dom_tag_eq(doc->nodes[i].tag, sel))
            return i;
    }
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
