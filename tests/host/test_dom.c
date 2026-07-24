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

int main(void) {
    test_class_token_match();
    test_root_scoped_scan();
    test_id_and_tag();
    if (fails) {
        fprintf(stderr, "test_dom: %d failure(s)\n", fails);
        return 1;
    }
    printf("test_dom: ok\n");
    return 0;
}
