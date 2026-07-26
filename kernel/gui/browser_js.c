/*
 * DOM ↔ JS bridge for browser tabs.
 *
 * browser_js_install_dom() wires document/querySelector, __dom_* natives, console.log,
 * location, and the small bootstrapping helpers ($, textContent, on, …).
 *
 * Web APIs (fetch, storage; AbortController not installed) are partial stubs
 * installed separately by webapi_install() — see webapi.h and webapi_stubs.c.
 */
#include "browser_js.h"
#include "browser.h"
#include "browser_isolation.h"
#include "../js/js_internal.h"
#include "util.h"
#include "heap.h"

void browser_js_host_init(struct browser_js_host *h, struct js_runtime *rt,
                          struct dom_document *doc, int *dirty) {
    memset(h, 0, sizeof(*h));
    h->rt = rt;
    h->doc = doc;
    h->dirty = dirty;
    h->handle_gen = 1;
}

void browser_js_invalidate_handles(struct browser_js_host *h) {
    if (!h)
        return;
    h->handle_gen++;
    if (h->handle_gen == 0)
        h->handle_gen = 1;
    h->nlisteners = 0;
    memset(h->listeners, 0, sizeof(h->listeners));
}

void browser_console_clear(struct browser_js_host *h) {
    if (!h)
        return;
    h->console_n = 0;
    for (int i = 0; i < 8; i++)
        h->console_log[i][0] = '\0';
}

void browser_console_set_filter(struct browser_js_host *h, const char *substr) {
    if (!h)
        return;
    if (!substr)
        substr = "";
    snprintf(h->console_filter, sizeof(h->console_filter), "%s", substr);
}

int browser_console_line_visible(const struct browser_js_host *h, const char *line) {
    if (!h || !h->console_filter[0])
        return 1;
    if (!line)
        return 0;
    return strstr(line, h->console_filter) != NULL;
}

static void mark_dirty(struct browser_js_host *h) {
    if (h && h->dirty)
        *h->dirty = 1;
    if (h && h->doc)
        h->doc->dirty = 1;
}

/* Packed handle: high 32 bits = gen, low 32 = node_id+1 (stored in dom_node ptr). */
static int make_dom_val(struct browser_js_host *h, struct js_runtime *rt, int node_id,
                        struct js_value *out) {
    struct js_object *o = js_obj_new(rt, 0);
    if (!o || !h)
        return -1;
    o->is_dom = 1;
    uint64_t packed = ((uint64_t)h->handle_gen << 32) | (uint32_t)(node_id + 1);
    o->dom_node = (void *)(uintptr_t)packed;
    out->type = JT_DOM;
    out->u.o = o;
    js_obj_set(rt, o, "__node", js_num((double)node_id));
    js_obj_set(rt, o, "__gen", js_num((double)h->handle_gen));
    return 0;
}

static int node_from_val(struct browser_js_host *h, const struct js_value *v) {
    if (!h || !v || (v->type != JT_DOM && v->type != JT_OBJ) || !v->u.o)
        return -1;
    if (v->u.o->is_dom) {
        uint64_t packed = (uint64_t)(uintptr_t)v->u.o->dom_node;
        uint32_t gen = (uint32_t)(packed >> 32);
        uint32_t nid1 = (uint32_t)packed;
        if (gen != h->handle_gen || nid1 == 0)
            return -1; /* stale or null handle */
        return (int)nid1 - 1;
    }
    struct js_value n, g;
    if (js_obj_get(NULL, v->u.o, "__gen", &g) == 0 && g.type == JT_NUM &&
        (uint32_t)g.u.n != h->handle_gen)
        return -1;
    if (js_obj_get(NULL, v->u.o, "__node", &n) == 0 && n.type == JT_NUM)
        return (int)n.u.n;
    return -1;
}


static int isolation_dom_gate(struct js_runtime *rt) {
    if (!browser_isolation_dom_allowed()) {
        snprintf(rt->err, sizeof(rt->err), "DOM blocked: ring-3 isolation unavailable");
        return -1;
    }
    return 0;
}

static int nat_get_el_by_id(struct js_runtime *rt, int argc, void *argv, void *ret,
                            void *ud) {
    if (isolation_dom_gate(rt))
        return -1;
    struct browser_js_host *h = ud;
    js_val_set_null(ret);
    if (!h || argc < 1)
        return 0;
    char id[64];
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], id, sizeof(id));
    int nid = dom_get_element_by_id(h->doc, id);
    if (nid >= 0)
        make_dom_val(h, rt, nid, (struct js_value *)ret);
    return 0;
}

static int nat_query_selector(struct js_runtime *rt, int argc, void *argv, void *ret,
                              void *ud) {
    if (isolation_dom_gate(rt))
        return -1;
    struct browser_js_host *h = ud;
    js_val_set_null(ret);
    if (!h || argc < 1)
        return 0;
    char sel[64];
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], sel, sizeof(sel));
    int nid = dom_query_selector(h->doc, h->doc->root, sel);
    if (nid >= 0)
        make_dom_val(h, rt, nid, (struct js_value *)ret);
    return 0;
}

static int nat_create_element(struct js_runtime *rt, int argc, void *argv, void *ret,
                              void *ud) {
    struct browser_js_host *h = ud;
    js_val_set_null(ret);
    if (!h || argc < 1)
        return 0;
    char tag[32];
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], tag, sizeof(tag));
    int nid = dom_create_element(h->doc, tag);
    if (nid >= 0) {
        make_dom_val(h, rt, nid, (struct js_value *)ret);
        mark_dirty(h);
    }
    return 0;
}

static int nat_append_child(struct js_runtime *rt, int argc, void *argv, void *ret,
                            void *ud) {
    struct browser_js_host *h = ud;
    (void)rt;
    js_val_set_undefined(ret);
    if (!h || argc < 2)
        return 0;
    int parent = node_from_val(h, &((struct js_value *)argv)[0]);
    int child = node_from_val(h, &((struct js_value *)argv)[1]);
    if (parent >= 0 && child >= 0) {
        dom_append_child(h->doc, parent, child);
        mark_dirty(h);
    }
    return 0;
}

static int nat_set_text(struct js_runtime *rt, int argc, void *argv, void *ret,
                        void *ud) {
    struct browser_js_host *h = ud;
    js_val_set_undefined(ret);
    if (!h || argc < 2)
        return 0;
    int nid = node_from_val(h, &((struct js_value *)argv)[0]);
    char text[DOM_TEXT_MAX];
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], text, sizeof(text));
    if (nid >= 0) {
        dom_set_text(h->doc, nid, text);
        mark_dirty(h);
    }
    return 0;
}

static int nat_get_text(struct js_runtime *rt, int argc, void *argv, void *ret,
                        void *ud) {
    struct browser_js_host *h = ud;
    js_val_set_string(rt, ret, "");
    if (!h || argc < 1)
        return 0;
    int nid = node_from_val(h, &((struct js_value *)argv)[0]);
    if (nid < 0)
        return 0;
    char buf[DOM_TEXT_MAX];
    dom_collect_text(h->doc, nid, buf, sizeof(buf));
    js_val_set_string(rt, ret, buf);
    return 0;
}

static int nat_set_attr(struct js_runtime *rt, int argc, void *argv, void *ret,
                        void *ud) {
    struct browser_js_host *h = ud;
    js_val_set_undefined(ret);
    if (!h || argc < 3)
        return 0;
    int nid = node_from_val(h, &((struct js_value *)argv)[0]);
    char name[32], val[96];
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], name, sizeof(name));
    js_val_to_cstring(rt, &((struct js_value *)argv)[2], val, sizeof(val));
    if (nid >= 0) {
        dom_set_attr(h->doc, nid, name, val);
        mark_dirty(h);
    }
    return 0;
}

static int nat_get_attr(struct js_runtime *rt, int argc, void *argv, void *ret,
                        void *ud) {
    struct browser_js_host *h = ud;
    js_val_set_null(ret);
    if (!h || argc < 2)
        return 0;
    int nid = node_from_val(h, &((struct js_value *)argv)[0]);
    char name[32];
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], name, sizeof(name));
    const char *v = nid >= 0 ? dom_get_attr(h->doc, nid, name) : NULL;
    if (v)
        js_val_set_string(rt, ret, v);
    return 0;
}

static int nat_set_inner_html(struct js_runtime *rt, int argc, void *argv, void *ret,
                              void *ud) {
    struct browser_js_host *h = ud;
    js_val_set_undefined(ret);
    if (!h || argc < 2)
        return 0;
    int nid = node_from_val(h, &((struct js_value *)argv)[0]);
    char html[512];
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], html, sizeof(html));
    if (nid >= 0) {
        dom_set_inner_html(h->doc, nid, html);
        mark_dirty(h);
    }
    return 0;
}

static int nat_add_event_listener(struct js_runtime *rt, int argc, void *argv, void *ret,
                                  void *ud) {
    struct browser_js_host *h = ud;
    js_val_set_undefined(ret);
    if (!h || argc < 3 || h->nlisteners >= 64)
        return 0;
    int nid = node_from_val(h, &((struct js_value *)argv)[0]);
    char type[16];
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], type, sizeof(type));
    struct js_value *fn = &((struct js_value *)argv)[2];
    if (nid < 0 || !js_val_is_function(fn))
        return 0;
    int i = h->nlisteners++;
    h->listeners[i].used = 1;
    h->listeners[i].node_id = nid;
    snprintf(h->listeners[i].type, sizeof(h->listeners[i].type), "%s", type);
    memcpy(h->listeners[i].fn, fn, JS_VALUE_BYTES);
    (void)rt;
    return 0;
}

static int nat_console_clear(struct js_runtime *rt, int argc, void *argv, void *ret,
                             void *ud) {
    (void)rt; (void)argc; (void)argv;
    browser_console_clear(ud);
    js_val_set_undefined(ret);
    return 0;
}

static int nat_console_log(struct js_runtime *rt, int argc, void *argv, void *ret,
                           void *ud) {
    struct browser_js_host *h = ud;
    char line[96];
    line[0] = '\0';
    size_t off = 0;
    for (int i = 0; i < argc; i++) {
        char part[64];
        js_val_to_cstring(rt, &((struct js_value *)argv)[i], part, sizeof(part));
        size_t n = strlen(part);
        if (off + n + 2 < sizeof(line)) {
            if (off)
                line[off++] = ' ';
            memcpy(line + off, part, n);
            off += n;
            line[off] = '\0';
        }
    }
    if (h) {
        int slot = h->console_n % 8;
        snprintf(h->console_log[slot], sizeof(h->console_log[slot]), "%s", line);
        h->console_n++;
    }
    js_val_set_undefined(ret);
    return 0;
}

static void install_fn(struct js_runtime *rt, struct js_value *obj, const char *name,
                       js_native_fn fn, void *ud) {
    struct js_object *o = js_obj_new(rt, 0);
    if (!o)
        return;
    o->is_native = 1;
    o->is_func = 1;
    o->native = fn;
    o->userdata = ud;
    struct js_value v;
    v.type = JT_NATIVE;
    v.u.o = o;
    js_val_set_prop(rt, obj, name, &v);
}

static int nat_query_selector_all(struct js_runtime *rt, int argc, void *argv, void *ret,
                                  void *ud);
static int nat_class_add(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud);
static int nat_class_remove(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud);
static int nat_class_toggle(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud);
static int nat_prevent_default(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud);
static int nat_location_assign(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud);
static int nat_location_reload(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud);

int browser_js_install_dom(struct browser_js_host *h) {
    if (!h || !h->rt || !h->doc)
        return -1;
    struct js_runtime *rt = h->rt;

    /* Override console.log to capture for UI */
    struct js_value console;
    js_val_new_object(rt, &console);
    install_fn(rt, &console, "log", nat_console_log, h);
    install_fn(rt, &console, "clear", nat_console_clear, h);
    js_obj_set(rt, rt->global, "console", console);

    struct js_value document;
    js_val_new_object(rt, &document);
    install_fn(rt, &document, "getElementById", nat_get_el_by_id, h);
    install_fn(rt, &document, "querySelector", nat_query_selector, h);
    install_fn(rt, &document, "querySelectorAll", nat_query_selector_all, h);
    install_fn(rt, &document, "createElement", nat_create_element, h);
    js_obj_set(rt, rt->global, "document", document);

    /* Peak DOM bridge helpers (not Web API stubs — those live in webapi_stubs.c). */
    js_rt_set_global_fn(rt, "__dom_appendChild", nat_append_child, h);
    js_rt_set_global_fn(rt, "__dom_setText", nat_set_text, h);
    js_rt_set_global_fn(rt, "__dom_getText", nat_get_text, h);
    js_rt_set_global_fn(rt, "__dom_setAttr", nat_set_attr, h);
    js_rt_set_global_fn(rt, "__dom_getAttr", nat_get_attr, h);
    js_rt_set_global_fn(rt, "__dom_setInnerHTML", nat_set_inner_html, h);
    js_rt_set_global_fn(rt, "__dom_on", nat_add_event_listener, h);
    js_rt_set_global_fn(rt, "__dom_classAdd", nat_class_add, h);
    js_rt_set_global_fn(rt, "__dom_classRemove", nat_class_remove, h);
    js_rt_set_global_fn(rt, "__dom_classToggle", nat_class_toggle, h);
    js_rt_set_global_fn(rt, "preventDefault", nat_prevent_default, h);

    /* location */
    struct js_value loc;
    js_val_new_object(rt, &loc);
    struct js_value href;
    js_val_set_string(rt, &href, h->doc->url);
    js_val_set_prop(rt, &loc, "href", &href);
    install_fn(rt, &loc, "assign", nat_location_assign, h);
    install_fn(rt, &loc, "reload", nat_location_reload, h);
    js_obj_set(rt, rt->global, "location", loc);

    /* window = global */
    struct js_value win;
    win.type = JT_OBJ;
    win.u.o = rt->global;
    js_obj_set(rt, rt->global, "window", win);

    /* Convenience: Element.prototype-like globals used by demo */
    const char *boot =
        "function $(s){return document.querySelector(s);}"
        "function textContent(el,t){if(t===undefined)return __dom_getText(el);"
        "__dom_setText(el,t);}"
        "function appendChild(p,c){__dom_appendChild(p,c);}"
        "function setAttr(el,n,v){__dom_setAttr(el,n,v);}"
        "function on(el,ty,fn){__dom_on(el,ty,fn);}"
        "function classListAdd(el,c){__dom_classAdd(el,c);}"
        "function classListRemove(el,c){__dom_classRemove(el,c);}"
        "function classListToggle(el,c){__dom_classToggle(el,c);}";
    char dump[32];
    js_eval(rt, boot, "<dom-bridge>", dump, sizeof(dump));
    return 0;
}

int browser_js_run_scripts(struct browser_js_host *h) {
    if (!h || !h->rt || !h->doc)
        return -1;
    char dump[64];
    int failed = 0;
    for (int i = 0; i < h->doc->nscripts; i++) {
        struct dom_script *sc = &h->doc->scripts[i];
        if (!sc->used || sc->external)
            continue; /* external loaded by browser resource layer */
        if (!sc->code)
            continue;
        if (js_eval(h->rt, sc->code, "<script>", dump, sizeof(dump)) != 0)
            failed++;
    }
    return failed ? -1 : 0;
}

int browser_js_dispatch_event(struct browser_js_host *h, int node_id, const char *type) {
    if (!h || !h->rt || !type)
        return -1;
    h->prevent_default = 0;
    for (int i = 0; i < h->nlisteners; i++) {
        if (!h->listeners[i].used || h->listeners[i].node_id != node_id)
            continue;
        if (strcmp(h->listeners[i].type, type) != 0)
            continue;
        struct js_value ret;
        js_val_call(h->rt, h->listeners[i].fn, NULL, 0, NULL, &ret);
        mark_dirty(h);
    }
    return 0;
}

int browser_js_dispatch_click(struct browser_js_host *h, int node_id) {
    return browser_js_dispatch_event(h, node_id, "click");
}

int browser_js_dispatch_input(struct browser_js_host *h, int node_id, const char *value) {
    if (!h || !h->rt)
        return -1;
    if (value)
        dom_set_attr(h->doc, node_id, "value", value);
    browser_js_dispatch_event(h, node_id, "input");
    browser_js_dispatch_event(h, node_id, "change");
    return 0;
}

int browser_js_dispatch_keydown(struct browser_js_host *h, int node_id, int key) {
    (void)key;
    return browser_js_dispatch_event(h, node_id, "keydown");
}

static int nat_prevent_default(struct js_runtime *rt, int argc, void *argv, void *ret,
                               void *ud) {
    (void)rt;
    (void)argc;
    (void)argv;
    struct browser_js_host *h = ud;
    if (h)
        h->prevent_default = 1;
    js_val_set_undefined(ret);
    return 0;
}

static int nat_query_selector_all(struct js_runtime *rt, int argc, void *argv, void *ret,
                                  void *ud) {
    struct browser_js_host *h = ud;
    js_val_set_undefined(ret);
    if (!h || argc < 1)
        return 0;
    char sel[64];
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], sel, sizeof(sel));
    struct js_value arr;
    js_val_new_array(rt, &arr);
    int found = 0;
    for (int id = 0; id < h->doc->nnodes && found < 32; id++) {
        int hit = dom_query_selector(h->doc, id, sel);
        /* dom_query_selector searches from root — use per-node match via getElement path */
        (void)hit;
    }
    /* Lite: return first match as single-element array when querySelector hits. */
    int nid = dom_query_selector(h->doc, h->doc->root >= 0 ? h->doc->root : 0, sel);
    if (nid >= 0) {
        struct js_value el;
        make_dom_val(h, rt, nid, &el);
        js_val_set_prop(rt, &arr, "0", &el);
        struct js_value len;
        js_val_set_number(&len, 1);
        js_val_set_prop(rt, &arr, "length", &len);
    }
    *(struct js_value *)ret = arr;
    return 0;
}

static int nat_class_list_op(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud,
                             int op) {
    struct browser_js_host *h = ud;
    js_val_set_undefined(ret);
    if (!h || argc < 2)
        return 0;
    int nid = node_from_val(h, &((struct js_value *)argv)[0]);
    char cls[32];
    js_val_to_cstring(rt, &((struct js_value *)argv)[1], cls, sizeof(cls));
    if (nid < 0 || !cls[0])
        return 0;
    const char *cur = dom_get_attr(h->doc, nid, "class");
    char buf[96];
    buf[0] = '\0';
    if (op == 0) { /* add */
        if (cur && cur[0])
            snprintf(buf, sizeof(buf), "%s %s", cur, cls);
        else
            snprintf(buf, sizeof(buf), "%s", cls);
    } else if (op == 1) { /* remove */
        if (cur) {
            /* drop exact token */
            const char *p = cur;
            size_t o = 0;
            while (*p && o + 1 < sizeof(buf)) {
                while (*p == ' ')
                    p++;
                const char *s = p;
                while (*p && *p != ' ')
                    p++;
                size_t n = (size_t)(p - s);
                if (n == strlen(cls) && !strncmp(s, cls, n))
                    continue;
                if (o)
                    buf[o++] = ' ';
                memcpy(buf + o, s, n);
                o += n;
                buf[o] = '\0';
            }
        }
    } else { /* toggle */
        int has = cur && strstr(cur, cls) != NULL;
        return nat_class_list_op(rt, argc, argv, ret, ud, has ? 1 : 0);
    }
    dom_set_attr(h->doc, nid, "class", buf);
    mark_dirty(h);
    return 0;
}

static int nat_class_add(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    return nat_class_list_op(rt, argc, argv, ret, ud, 0);
}
static int nat_class_remove(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    return nat_class_list_op(rt, argc, argv, ret, ud, 1);
}
static int nat_class_toggle(struct js_runtime *rt, int argc, void *argv, void *ret, void *ud) {
    return nat_class_list_op(rt, argc, argv, ret, ud, 2);
}

static int nat_location_assign(struct js_runtime *rt, int argc, void *argv, void *ret,
                               void *ud) {
    (void)ud;
    js_val_set_undefined(ret);
    if (argc < 1)
        return 0;
    char url[160];
    js_val_to_cstring(rt, &((struct js_value *)argv)[0], url, sizeof(url));
    if (url[0])
        browser_go(url);
    return 0;
}

static int nat_location_reload(struct js_runtime *rt, int argc, void *argv, void *ret,
                               void *ud) {
    (void)rt;
    (void)argc;
    (void)argv;
    (void)ud;
    js_val_set_undefined(ret);
    browser_reload();
    return 0;
}
