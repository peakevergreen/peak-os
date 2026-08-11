/*
 * Host tests for DOM class/tag querySelector (kernel/gui/dom_core.c).
 * Covers token-aware class matching and root-scoped subtree scans.
 */
#include "dom.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void test_class_token_match(void) {
    struct dom_document doc;
    dom_doc_init(&doc);

    int root = dom_create_element(&doc, "div");
    int hit = dom_create_element(&doc, "span");
    int decoy = dom_create_element(&doc, "span");
    expect(root >= 0 && hit >= 0 && decoy >= 0, "create elements");
    expect(dom_append_child(&doc, root, hit) == 0, "append hit");
    expect(dom_append_child(&doc, root, decoy) == 0, "append decoy");
    expect(dom_set_attr(&doc, hit, "class", "btn primary") == 0, "set hit class");
    expect(dom_set_attr(&doc, decoy, "class", "btnprimary") == 0, "set decoy class");

    expect(dom_query_selector(&doc, root, ".primary") == hit, "token match .primary");
    expect(dom_query_selector(&doc, root, ".btn") == hit, "token match .btn");
    expect(dom_query_selector(&doc, root, ".btnprimary") == decoy, "full token decoy");
    expect(dom_query_selector(&doc, root, ".prim") == -1, "no substring false positive");
    expect(dom_query_selector(&doc, root, ".") == -1, "empty class selector");
}

static void test_root_scoped_scan(void) {
    struct dom_document doc;
    dom_doc_init(&doc);

    int html = dom_create_element(&doc, "html");
    int body = dom_create_element(&doc, "body");
    int panel = dom_create_element(&doc, "div");
    int inside = dom_create_element(&doc, "p");
    int outside = dom_create_element(&doc, "p");
    expect(html >= 0 && body >= 0 && panel >= 0 && inside >= 0 && outside >= 0,
           "create tree nodes");
    expect(dom_append_child(&doc, html, body) == 0, "html->body");
    expect(dom_append_child(&doc, body, panel) == 0, "body->panel");
    expect(dom_append_child(&doc, panel, inside) == 0, "panel->inside");
    /* Detached sibling with the same class must not match a panel-scoped query. */
    expect(dom_set_attr(&doc, outside, "class", "item") == 0, "outside class");
    expect(dom_set_attr(&doc, inside, "class", "item") == 0, "inside class");

    expect(dom_query_selector(&doc, panel, ".item") == inside, "scoped finds inside");
    expect(dom_query_selector(&doc, panel, "p") == inside, "scoped tag finds inside");
    expect(dom_query_selector(&doc, html, ".item") == inside, "doc root finds first in-tree");
    int unscoped = dom_query_selector(&doc, -1, ".item");
    expect(unscoped == inside || unscoped == outside, "unscoped finds a live .item");
    /* Detached outside must not match under panel. */
    expect(dom_query_selector(&doc, panel, ".item") != outside, "scoped excludes detached");
}

static void test_id_and_tag(void) {
    struct dom_document doc;
    dom_doc_init(&doc);

    int root = dom_create_element(&doc, "section");
    int a = dom_create_element(&doc, "article");
    int b = dom_create_element(&doc, "div");
    expect(dom_append_child(&doc, root, a) == 0, "append article");
    expect(dom_append_child(&doc, root, b) == 0, "append div");
    expect(dom_set_attr(&doc, b, "id", "main") == 0, "set id");

    expect(dom_query_selector(&doc, root, "article") == a, "tag query");
    expect(dom_query_selector(&doc, root, "#main") == b, "id query");
    expect(dom_get_element_by_id(&doc, "main") == b, "getElementById");
}

static void test_compound_selector(void) {
    struct dom_document doc;
    dom_doc_init(&doc);
    int root = dom_create_element(&doc, "div");
    int p1 = dom_create_element(&doc, "p");
    int p2 = dom_create_element(&doc, "p");
    expect(dom_append_child(&doc, root, p1) == 0, "append p1");
    expect(dom_append_child(&doc, root, p2) == 0, "append p2");
    expect(dom_set_attr(&doc, p1, "class", "note") == 0, "p1 class");
    expect(dom_set_attr(&doc, p2, "class", "warn") == 0, "p2 class");
    expect(dom_query_selector(&doc, root, "p.note") == p1, "compound p.note");
    expect(dom_query_selector(&doc, root, "p.warn") == p2, "compound p.warn");
    expect(dom_query_selector(&doc, root, "p.missing") == -1, "compound miss");
}

static void test_find_ancestor_safe(void) {
    struct dom_document doc;
    dom_doc_init(&doc);

    int form = dom_create_element(&doc, "form");
    int div = dom_create_element(&doc, "div");
    int input = dom_create_element(&doc, "input");
    expect(form >= 0 && div >= 0 && input >= 0, "create form tree");
    expect(dom_append_child(&doc, form, div) == 0, "form->div");
    expect(dom_append_child(&doc, div, input) == 0, "div->input");

    expect(dom_find_ancestor(&doc, input, "form") == form, "find form from input");
    expect(dom_find_ancestor(&doc, form, "form") == form, "form matches self");
    expect(dom_find_ancestor(&doc, input, "table") == -1, "missing ancestor");

    /* Corrupt parent: out of nnodes — must stop, not OOB. */
    doc.nodes[input].parent = doc.nnodes + 50;
    expect(dom_find_ancestor(&doc, input, "form") == -1, "oob parent stops");

    /* Self-cycle on parent link. */
    doc.nodes[input].parent = input;
    expect(dom_find_ancestor(&doc, input, "form") == -1, "self-cycle stops");

    /* Two-node cycle. */
    doc.nodes[input].parent = div;
    doc.nodes[div].parent = input;
    expect(dom_find_ancestor(&doc, input, "form") == -1, "cycle stops");
}

static void test_append_child_guards(void) {
    struct dom_document doc;
    dom_doc_init(&doc);

    int root = dom_create_element(&doc, "div");
    int a = dom_create_element(&doc, "span");
    int b = dom_create_element(&doc, "span");
    int c = dom_create_element(&doc, "span");
    expect(root >= 0 && a >= 0 && b >= 0 && c >= 0, "create append tree");
    expect(dom_append_child(&doc, root, a) == 0, "root->a");
    expect(dom_append_child(&doc, root, b) == 0, "root->b");
    expect(dom_append_child(&doc, a, c) == 0, "a->c");

    /* Cannot append self or ancestor under descendant. */
    expect(dom_append_child(&doc, a, a) != 0, "reject self");
    expect(dom_append_child(&doc, c, a) != 0, "reject ancestor under child");
    expect(dom_append_child(&doc, c, root) != 0, "reject root under descendant");

    /* Re-parent: unlink from old parent then append. */
    expect(dom_append_child(&doc, b, c) == 0, "reparent c under b");
    expect(doc.nodes[a].first_child < 0 || doc.nodes[a].first_child != c, "a lost c");
    expect(doc.nodes[c].parent == b, "c parent is b");
    expect(dom_append_child(&doc, b, c) == 0, "idempotent re-append");

    /* Bounded next_sibling stops on self-cycle. */
    doc.nodes[a].next_sibling = a;
    expect(dom_next_sibling(&doc, a) == -1, "self-cycle sibling stops");
}

static void test_inner_html_orphans_unused(void) {
    struct dom_document doc;
    dom_doc_init(&doc);

    int panel = dom_create_element(&doc, "div");
    int child = dom_create_element(&doc, "input");
    int nested = dom_create_element(&doc, "span");
    expect(panel >= 0 && child >= 0 && nested >= 0, "create innerHTML tree");
    expect(dom_append_child(&doc, panel, child) == 0, "panel->child");
    expect(dom_append_child(&doc, child, nested) == 0, "child->nested");
    expect(dom_node(&doc, child) != NULL, "child live before");
    expect(dom_node(&doc, nested) != NULL, "nested live before");

    dom_set_inner_html(&doc, panel, "hi");
    expect(dom_node(&doc, child) == NULL, "orphaned child unused");
    expect(dom_node(&doc, nested) == NULL, "orphaned nested unused");
    expect(dom_node(&doc, panel) != NULL, "panel still live");
    /* Stale focus_node pattern: cleared when !dom_node. */
    int focus_node = child;
    if (!dom_node(&doc, focus_node))
        focus_node = -1;
    expect(focus_node == -1, "stale focus cleared after innerHTML");
}

int main(void) {
    test_class_token_match();
    test_root_scoped_scan();
    test_id_and_tag();
    test_compound_selector();
    test_find_ancestor_safe();
    test_append_child_guards();
    test_inner_html_orphans_unused();
    if (fails) {
        fprintf(stderr, "test_dom: %d failure(s)\n", fails);
        return 1;
    }
    printf("test_dom: ok\n");
    return 0;
}
