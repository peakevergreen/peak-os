#ifndef PEAK_DOM_H
#define PEAK_DOM_H

#include "types.h"

#define DOM_MAX_NODES   512
#define DOM_NAME_MAX    32
#define DOM_ATTR_MAX    8
#define DOM_ATTR_VAL_MAX 64
#define DOM_TEXT_MAX    160
#define DOM_SCRIPT_MAX  16

enum dom_node_type {
    DOM_ELEMENT = 1,
    DOM_TEXT = 3,
    DOM_DOCUMENT = 9,
};

struct dom_attr {
    char name[DOM_NAME_MAX];
    char value[DOM_ATTR_VAL_MAX];
};

struct dom_node {
    int used;
    enum dom_node_type type;
    char tag[DOM_NAME_MAX];
    char text[DOM_TEXT_MAX];
    struct dom_attr attrs[DOM_ATTR_MAX];
    int nattrs;
    int parent;
    int first_child;
    int next_sibling;
    int id; /* index in document pool */
};

struct dom_script {
    int used;
    int external;
    char src[160];
    char *code; /* kmalloc'd inline source */
    size_t code_len;
    int deferred;
    int async;
};

struct dom_document {
    struct dom_node nodes[DOM_MAX_NODES];
    int nnodes;
    int root; /* document element html */
    int body;
    char title[64];
    char url[160];
    struct dom_script scripts[DOM_SCRIPT_MAX];
    int nscripts;
    int dirty;
};

void dom_doc_init(struct dom_document *doc);
void dom_doc_clear(struct dom_document *doc);
int dom_parse_html(struct dom_document *doc, const char *html, const char *url);

struct dom_node *dom_node(struct dom_document *doc, int id);
int dom_create_element(struct dom_document *doc, const char *tag);
int dom_create_text(struct dom_document *doc, const char *text);
int dom_append_child(struct dom_document *doc, int parent, int child);
int dom_set_attr(struct dom_document *doc, int id, const char *name, const char *value);
const char *dom_get_attr(struct dom_document *doc, int id, const char *name);
int dom_set_text(struct dom_document *doc, int id, const char *text);
int dom_query_selector(struct dom_document *doc, int root, const char *sel);
int dom_get_element_by_id(struct dom_document *doc, const char *id);
void dom_set_inner_html(struct dom_document *doc, int id, const char *html);

/* Collect visible text blocks for reader/paint fallback. */
int dom_collect_text(struct dom_document *doc, int id, char *out, size_t cap);

#endif
