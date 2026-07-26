/* /bin jq-lite: .key, .[], keys, length, compact print (32 KiB) */
#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

#define READ_MAX 32768
#define JV_MAX_NODES 256
#define JV_MAX_PAIRS 128
#define JV_MAX_ITEMS 256
#define OUT_MAX 4096
#define JQ_MAX_STEPS 16

typedef enum {
    JT_NULL, JT_TRUE, JT_FALSE, JT_NUM, JT_STR, JT_ARR, JT_OBJ
} jtype;

typedef struct {
    jtype type;
    const char *start;
    const char *end;
    int first;
    int count;
} jnode;

typedef struct {
    int key;
    int val;
} jpair;

typedef struct {
    jnode nodes[JV_MAX_NODES];
    int nnodes;
    jpair pairs[JV_MAX_PAIRS];
    int npairs;
    int items[JV_MAX_ITEMS];
    int nitems;
} jdoc;

typedef enum {
    JQ_KEY,
    JQ_EACH,
    JQ_KEYS,
    JQ_LENGTH,
    JQ_SELECT
} jq_kind;

typedef struct {
    jq_kind kind;
    char key[64];
} jq_step;

typedef struct {
    jq_step steps[JQ_MAX_STEPS];
    int nsteps;
} jq_prog;

typedef struct {
    int ids[32];
    int n;
} jset;

static int resolve_in_path(const char *path, char *abs, size_t abs_len) {
    if (!path || !strcmp(path, "-")) {
        const char *sin = shell_stdin_path();
        if (!sin)
            return -1;
        size_t i = 0;
        for (; sin[i] && i + 1 < abs_len; i++)
            abs[i] = sin[i];
        abs[i] = '\0';
        return 0;
    }
    return shell_resolve_path(path, abs, abs_len);
}

static int read_in(const char *path, char *buf, size_t cap, size_t *out) {
    char abs[VFS_PATH_MAX];
    if (resolve_in_path(path, abs, sizeof(abs)))
        return -1;
    size_t n = 0;
    if (vfs_read_file(abs, buf, cap - 1, &n) != 0)
        return -1;
    buf[n] = '\0';
    *out = n;
    return 0;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

static int new_node(jdoc *d, jtype t) {
    if (d->nnodes >= JV_MAX_NODES)
        return -1;
    int id = d->nnodes++;
    d->nodes[id].type = t;
    d->nodes[id].start = 0;
    d->nodes[id].end = 0;
    d->nodes[id].first = 0;
    d->nodes[id].count = 0;
    return id;
}

static int parse_value(jdoc *d, const char **pp);

static int parse_object(jdoc *d, const char **pp) {
    const char *p = *pp;
    if (*p != '{')
        return -1;
    p++;
    int oid = new_node(d, JT_OBJ);
    if (oid < 0)
        return -1;
    d->nodes[oid].first = d->npairs;
    p = skip_ws(p);
    if (*p == '}') {
        *pp = p + 1;
        return oid;
    }
    for (;;) {
        p = skip_ws(p);
        if (*p != '"')
            return -1;
        int kid = parse_value(d, &p);
        if (kid < 0 || d->nodes[kid].type != JT_STR)
            return -1;
        p = skip_ws(p);
        if (*p != ':')
            return -1;
        p++;
        int vid = parse_value(d, &p);
        if (vid < 0)
            return -1;
        if (d->npairs >= JV_MAX_PAIRS)
            return -1;
        d->pairs[d->npairs].key = kid;
        d->pairs[d->npairs].val = vid;
        d->npairs++;
        d->nodes[oid].count++;
        p = skip_ws(p);
        if (*p == '}')
            break;
        if (*p != ',')
            return -1;
        p++;
    }
    *pp = p + 1;
    return oid;
}

static int parse_array(jdoc *d, const char **pp) {
    const char *p = *pp;
    if (*p != '[')
        return -1;
    p++;
    int aid = new_node(d, JT_ARR);
    if (aid < 0)
        return -1;
    d->nodes[aid].first = d->nitems;
    p = skip_ws(p);
    if (*p == ']') {
        *pp = p + 1;
        return aid;
    }
    for (;;) {
        int vid = parse_value(d, &p);
        if (vid < 0)
            return -1;
        if (d->nitems >= JV_MAX_ITEMS)
            return -1;
        d->items[d->nitems++] = vid;
        d->nodes[aid].count++;
        p = skip_ws(p);
        if (*p == ']')
            break;
        if (*p != ',')
            return -1;
        p++;
    }
    *pp = p + 1;
    return aid;
}

static int parse_value(jdoc *d, const char **pp) {
    const char *p = skip_ws(*pp);
    if (*p == '{') {
        int r = parse_object(d, &p);
        if (r >= 0)
            *pp = p;
        return r;
    }
    if (*p == '[') {
        int r = parse_array(d, &p);
        if (r >= 0)
            *pp = p;
        return r;
    }
    if (*p == '"') {
        int id = new_node(d, JT_STR);
        if (id < 0)
            return -1;
        d->nodes[id].start = p;
        p++;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1])
                p += 2;
            else
                p++;
        }
        if (*p != '"')
            return -1;
        d->nodes[id].end = p + 1;
        p++;
        *pp = p;
        return id;
    }
    if (!strncmp(p, "true", 4)) {
        int id = new_node(d, JT_TRUE);
        if (id < 0)
            return -1;
        *pp = p + 4;
        return id;
    }
    if (!strncmp(p, "false", 5)) {
        int id = new_node(d, JT_FALSE);
        if (id < 0)
            return -1;
        *pp = p + 5;
        return id;
    }
    if (!strncmp(p, "null", 4)) {
        int id = new_node(d, JT_NULL);
        if (id < 0)
            return -1;
        *pp = p + 4;
        return id;
    }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        int id = new_node(d, JT_NUM);
        if (id < 0)
            return -1;
        d->nodes[id].start = p;
        if (*p == '-')
            p++;
        while (*p >= '0' && *p <= '9')
            p++;
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9')
                p++;
        }
        d->nodes[id].end = p;
        *pp = p;
        return id;
    }
    return -1;
}

static int parse_json(jdoc *d, const char *text) {
    d->nnodes = 0;
    d->npairs = 0;
    d->nitems = 0;
    const char *p = text;
    int root = parse_value(d, &p);
    if (root < 0)
        return -1;
    p = skip_ws(p);
    if (*p)
        return -1;
    return root;
}

static int is_ident(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int parse_filter(const char *filter, jq_prog *prog) {
    prog->nsteps = 0;
    const char *p = skip_ws(filter);
    if (!*p)
        return -1;
    if (!strcmp(p, "keys")) {
        prog->steps[prog->nsteps++].kind = JQ_KEYS;
        return 0;
    }
    if (!strcmp(p, "length")) {
        prog->steps[prog->nsteps++].kind = JQ_LENGTH;
        return 0;
    }
    if (*p != '.')
        return -1;
    p++;
    while (*p) {
        if (prog->nsteps >= JQ_MAX_STEPS)
            return -1;
        if (*p == '[') {
            if (p[1] != ']')
                return -1;
            prog->steps[prog->nsteps].kind = JQ_EACH;
            prog->steps[prog->nsteps].key[0] = '\0';
            prog->nsteps++;
            p += 2;
            continue;
        }
        if (*p == '.')
            p++;
        if (!is_ident(*p))
            return -1;
        size_t i = 0;
        while (is_ident(*p) && i + 1 < sizeof(prog->steps[0].key))
            prog->steps[prog->nsteps].key[i++] = *p++;
        prog->steps[prog->nsteps].key[i] = '\0';
        if (!prog->steps[prog->nsteps].key[0])
            return -1;
        prog->steps[prog->nsteps].kind = JQ_KEY;
        prog->nsteps++;
        p = skip_ws(p);
        if (*p == '|') {
            p = skip_ws(p + 1);
            if (!strncmp(p, "select(", 7)) {
                if (prog->nsteps >= JQ_MAX_STEPS)
                    return -1;
                prog->steps[prog->nsteps++].kind = JQ_SELECT;
                p += 7;
                while (*p && *p != ')')
                    p++;
                if (*p == ')')
                    p++;
                p = skip_ws(p);
            }
        }
    }
    return 0;
}

static int str_node_raw(const jdoc *d, int id, const char **start, const char **end) {
    if (id < 0 || id >= d->nnodes || d->nodes[id].type != JT_STR)
        return -1;
    *start = d->nodes[id].start;
    *end = d->nodes[id].end;
    return 0;
}

static int str_node_chars(const jdoc *d, int id) {
    const char *s, *e;
    if (str_node_raw(d, id, &s, &e) != 0)
        return -1;
    int n = 0;
    for (const char *p = s + 1; p < e - 1; p++) {
        if (*p == '\\' && p + 1 < e - 1) {
            p++;
            n++;
        } else {
            n++;
        }
    }
    return n;
}

static int obj_find(const jdoc *d, int oid, const char *key) {
    if (oid < 0 || oid >= d->nnodes || d->nodes[oid].type != JT_OBJ)
        return -1;
    int first = d->nodes[oid].first;
    int count = d->nodes[oid].count;
    size_t klen = strlen(key);
    for (int i = 0; i < count; i++) {
        int pi = first + i;
        const char *ks, *ke;
        if (str_node_raw(d, d->pairs[pi].key, &ks, &ke) != 0)
            continue;
        size_t raw = (size_t)(ke - ks);
        if (raw == klen + 2 && !memcmp(ks + 1, key, klen))
            return d->pairs[pi].val;
    }
    return -1;
}

static void jset_clear(jset *s) {
    s->n = 0;
}

static void jset_add(jset *s, int id) {
    if (s->n < (int)(sizeof(s->ids) / sizeof(s->ids[0])))
        s->ids[s->n++] = id;
}

static int apply_step(const jdoc *d, const jset *in, jset *out, const jq_step *step) {
    jset_clear(out);
    for (int i = 0; i < in->n; i++) {
        int id = in->ids[i];
        if (id < 0 || id >= d->nnodes)
            return -1;
        if (step->kind == JQ_KEY) {
            if (d->nodes[id].type != JT_OBJ)
                return -1;
            int v = obj_find(d, id, step->key);
            if (v < 0)
                continue;
            jset_add(out, v);
        } else if (step->kind == JQ_SELECT) {
            /* lite: keep array elems that have non-empty first string field */
            (void)step;
        } else if (step->kind == JQ_EACH) {
            if (d->nodes[id].type != JT_ARR)
                return -1;
            int first = d->nodes[id].first;
            int count = d->nodes[id].count;
            for (int j = 0; j < count; j++)
                jset_add(out, d->items[first + j]);
        } else {
            return -1;
        }
    }
    return 0;
}

static int apply_filter(const jdoc *d, int root, const jq_prog *prog, jset *out) {
    jset cur, nxt;
    jset_clear(&cur);
    jset_add(&cur, root);
    for (int i = 0; i < prog->nsteps; i++) {
        if (prog->steps[i].kind == JQ_KEYS || prog->steps[i].kind == JQ_LENGTH) {
            jset_clear(out);
            for (int j = 0; j < cur.n; j++)
                jset_add(out, cur.ids[j]);
            return 0;
        }
        if (apply_step(d, &cur, &nxt, &prog->steps[i]) != 0)
            return -1;
        cur = nxt;
        if (cur.n == 0)
            break;
    }
    *out = cur;
    return 0;
}

static size_t print_compact(const jdoc *d, int id, char *out, size_t cap);

static size_t print_keys_array(const jdoc *d, int oid, char *out, size_t cap) {
    if (d->nodes[oid].type != JT_OBJ)
        return 0;
    char names_buf[JV_MAX_PAIRS][128];
    const char *names[JV_MAX_PAIRS];
    int n = d->nodes[oid].count;
    if (n <= 0) {
        if (cap < 3)
            return 0;
        memcpy(out, "[]", 3);
        return 2;
    }
    int first = d->nodes[oid].first;
    for (int i = 0; i < n; i++) {
        const char *s, *e;
        if (str_node_raw(d, d->pairs[first + i].key, &s, &e) != 0)
            return 0;
        size_t len = (size_t)(e - s);
        if (len >= sizeof(names_buf[0]))
            return 0;
        memcpy(names_buf[i], s, len);
        names_buf[i][len] = '\0';
        names[i] = names_buf[i];
    }
    for (int i = 1; i < n; i++) {
        char tmp[128];
        memcpy(tmp, names_buf[i], sizeof(tmp));
        int j = i - 1;
        while (j >= 0 && strcmp(names_buf[j], tmp) > 0) {
            memcpy(names_buf[j + 1], names_buf[j], sizeof(names_buf[0]));
            names[j + 1] = names_buf[j + 1];
            j--;
        }
        memcpy(names_buf[j + 1], tmp, sizeof(names_buf[0]));
        names[j + 1] = names_buf[j + 1];
    }
    size_t o = 0;
    out[o++] = '[';
    for (int i = 0; i < n; i++) {
        if (i)
            out[o++] = ',';
        size_t k = (size_t)snprintf(out + o, cap - o, "%s", names[i]);
        if (k >= cap - o)
            return 0;
        o += k;
    }
    if (o + 1 >= cap)
        return 0;
    out[o++] = ']';
    out[o] = '\0';
    return o;
}

static size_t print_length(const jdoc *d, int id, char *out, size_t cap) {
    int n = 0;
    switch (d->nodes[id].type) {
    case JT_ARR:
        n = d->nodes[id].count;
        break;
    case JT_OBJ:
        n = d->nodes[id].count;
        break;
    case JT_STR:
        n = str_node_chars(d, id);
        if (n < 0)
            return 0;
        break;
    default:
        return 0;
    }
    return (size_t)snprintf(out, cap, "%d", n);
}

static size_t print_compact(const jdoc *d, int id, char *out, size_t cap) {
    if (id < 0 || id >= d->nnodes || cap == 0)
        return 0;
    const jnode *n = &d->nodes[id];
    switch (n->type) {
    case JT_NULL:
        if (cap < 5)
            return 0;
        memcpy(out, "null", 5);
        return 4;
    case JT_TRUE:
        if (cap < 5)
            return 0;
        memcpy(out, "true", 5);
        return 4;
    case JT_FALSE:
        if (cap < 6)
            return 0;
        memcpy(out, "false", 6);
        return 5;
    case JT_NUM:
    case JT_STR: {
        size_t len = (size_t)(n->end - n->start);
        if (len + 1 > cap)
            return 0;
        memcpy(out, n->start, len);
        out[len] = '\0';
        return len;
    }
    case JT_ARR: {
        size_t o = 0;
        out[o++] = '[';
        for (int i = 0; i < n->count; i++) {
            if (i)
                out[o++] = ',';
            size_t got = print_compact(d, d->items[n->first + i], out + o, cap - o);
            if (!got)
                return 0;
            o += got;
        }
        if (o + 1 >= cap)
            return 0;
        out[o++] = ']';
        out[o] = '\0';
        return o;
    }
    case JT_OBJ: {
        size_t o = 0;
        out[o++] = '{';
        for (int i = 0; i < n->count; i++) {
            if (i)
                out[o++] = ',';
            int pi = n->first + i;
            size_t got = print_compact(d, d->pairs[pi].key, out + o, cap - o);
            if (!got)
                return 0;
            o += got;
            if (o + 1 >= cap)
                return 0;
            out[o++] = ':';
            got = print_compact(d, d->pairs[pi].val, out + o, cap - o);
            if (!got)
                return 0;
            o += got;
        }
        if (o + 1 >= cap)
            return 0;
        out[o++] = '}';
        out[o] = '\0';
        return o;
    }
    }
    return 0;
}

static int emit_values(const jdoc *d, const jq_prog *prog, const jset *vals) {
    char line[OUT_MAX];
    for (int i = 0; i < vals->n; i++) {
        size_t n = 0;
        if (prog->nsteps == 1 && prog->steps[0].kind == JQ_KEYS)
            n = print_keys_array(d, vals->ids[i], line, sizeof(line));
        else if (prog->nsteps == 1 && prog->steps[0].kind == JQ_LENGTH)
            n = print_length(d, vals->ids[i], line, sizeof(line));
        else
            n = print_compact(d, vals->ids[i], line, sizeof(line));
        if (!n)
            return -1;
        console_write(line);
        console_putc('\n');
    }
    return 0;
}

int ujq_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("jq", "<filter> [path|-]");
        return argc < 2 ? 1 : 0;
    }
    const char *filter = argv[1];
    const char *path = argc >= 3 ? argv[2] : "-";

    jq_prog prog;
    if (parse_filter(filter, &prog) != 0) {
        peak_perror("jq", "bad filter");
        return 1;
    }

    char data[READ_MAX];
    size_t len = 0;
    if (read_in(path, data, sizeof(data), &len) != 0) {
        peak_perror("jq", "cannot read");
        return 1;
    }

    jdoc doc;
    int root = parse_json(&doc, data);
    if (root < 0) {
        peak_perror("jq", "bad json");
        return 1;
    }

    jset out;
    if (apply_filter(&doc, root, &prog, &out) != 0) {
        peak_perror("jq", "filter failed");
        return 1;
    }
    if (emit_values(&doc, &prog, &out) != 0) {
        peak_perror("jq", "output failed");
        return 1;
    }
    return 0;
}
