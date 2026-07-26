/* Host tests for Pass 46 jq-lite JSON parse/filter helpers. */
#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

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
    jnode nodes[64];
    int nnodes;
    jpair pairs[32];
    int npairs;
    int items[64];
    int nitems;
} jdoc;

typedef enum { JQ_KEY, JQ_EACH, JQ_KEYS, JQ_LENGTH } jq_kind;

typedef struct {
    jq_kind kind;
    char key[64];
} jq_step;

typedef struct {
    jq_step steps[8];
    int nsteps;
} jq_prog;

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

static int new_node(jdoc *d, jtype t) {
    if (d->nnodes >= 64)
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
        d->nodes[id].start = p;
        p++;
        while (*p && *p != '"')
            p++;
        if (*p != '"')
            return -1;
        d->nodes[id].end = p + 1;
        p++;
        *pp = p;
        return id;
    }
    if (!strncmp(p, "true", 4)) {
        int id = new_node(d, JT_TRUE);
        *pp = p + 4;
        return id;
    }
    if (!strncmp(p, "false", 5)) {
        int id = new_node(d, JT_FALSE);
        *pp = p + 5;
        return id;
    }
    if (!strncmp(p, "null", 4)) {
        int id = new_node(d, JT_NULL);
        *pp = p + 4;
        return id;
    }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        int id = new_node(d, JT_NUM);
        d->nodes[id].start = p;
        if (*p == '-')
            p++;
        while (*p >= '0' && *p <= '9')
            p++;
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
        if (*p == '[') {
            if (p[1] != ']')
                return -1;
            prog->steps[prog->nsteps].kind = JQ_EACH;
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
        prog->steps[prog->nsteps].kind = JQ_KEY;
        prog->nsteps++;
    }
    return 0;
}

static int obj_find(const jdoc *d, int oid, const char *key) {
    size_t klen = strlen(key);
    for (int i = 0; i < d->nodes[oid].count; i++) {
        int pi = d->nodes[oid].first + i;
        const char *ks = d->nodes[d->pairs[pi].key].start;
        const char *ke = d->nodes[d->pairs[pi].key].end;
        size_t raw = (size_t)(ke - ks);
        if (raw == klen + 2 && !memcmp(ks + 1, key, klen))
            return d->pairs[pi].val;
    }
    return -1;
}

static size_t print_compact(const jdoc *d, int id, char *out, size_t cap) {
    const jnode *n = &d->nodes[id];
    switch (n->type) {
    case JT_NULL:
        return (size_t)snprintf(out, cap, "null");
    case JT_TRUE:
        return (size_t)snprintf(out, cap, "true");
    case JT_FALSE:
        return (size_t)snprintf(out, cap, "false");
    case JT_NUM:
    case JT_STR:
        return (size_t)snprintf(out, cap, "%.*s", (int)(n->end - n->start), n->start);
    case JT_ARR: {
        size_t o = 0;
        o += (size_t)snprintf(out + o, cap - o, "[");
        for (int i = 0; i < n->count; i++) {
            if (i)
                o += (size_t)snprintf(out + o, cap - o, ",");
            o += print_compact(d, d->items[n->first + i], out + o, cap - o);
        }
        o += (size_t)snprintf(out + o, cap - o, "]");
        return o;
    }
    case JT_OBJ: {
        size_t o = 0;
        o += (size_t)snprintf(out + o, cap - o, "{");
        for (int i = 0; i < n->count; i++) {
            if (i)
                o += (size_t)snprintf(out + o, cap - o, ",");
            int pi = n->first + i;
            o += print_compact(d, d->pairs[pi].key, out + o, cap - o);
            o += (size_t)snprintf(out + o, cap - o, ":");
            o += print_compact(d, d->pairs[pi].val, out + o, cap - o);
        }
        o += (size_t)snprintf(out + o, cap - o, "}");
        return o;
    }
    }
    return 0;
}

static size_t print_keys_array(const jdoc *d, int oid, char *out, size_t cap) {
    char names_buf[8][64];
    const char *names[8];
    int n = d->nodes[oid].count;
    for (int i = 0; i < n; i++) {
        int pi = d->nodes[oid].first + i;
        const char *s = d->nodes[d->pairs[pi].key].start;
        const char *e = d->nodes[d->pairs[pi].key].end;
        size_t len = (size_t)(e - s);
        memcpy(names_buf[i], s, len);
        names_buf[i][len] = '\0';
        names[i] = names_buf[i];
    }
    for (int i = 1; i < n; i++) {
        char tmp[64];
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
        o += (size_t)snprintf(out + o, cap - o, "%s", names[i]);
    }
    out[o++] = ']';
    out[o] = '\0';
    return o;
}

static int eval_filter(const jdoc *d, int root, const jq_prog *prog, char *out, size_t cap) {
    if (prog->nsteps == 1 && prog->steps[0].kind == JQ_KEYS)
        return (int)print_keys_array(d, root, out, cap) > 0;
    if (prog->nsteps == 1 && prog->steps[0].kind == JQ_LENGTH) {
        int n = d->nodes[root].count;
        if (d->nodes[root].type == JT_ARR || d->nodes[root].type == JT_OBJ)
            return snprintf(out, cap, "%d", n) > 0;
        return 0;
    }
    int cur = root;
    for (int i = 0; i < prog->nsteps; i++) {
        if (prog->steps[i].kind == JQ_KEY) {
            cur = obj_find(d, cur, prog->steps[i].key);
            if (cur < 0)
                return 0;
        } else if (prog->steps[i].kind == JQ_EACH) {
            if (d->nodes[cur].type != JT_ARR || d->nodes[cur].count == 0)
                return 0;
            cur = d->items[d->nodes[cur].first];
        }
    }
    return (int)print_compact(d, cur, out, cap) > 0;
}

int main(void) {
    jdoc doc;
    jq_prog prog;
    char out[256];

    int root = parse_json(&doc, "{\"name\":\"peak\",\"n\":3}");
    expect(root >= 0, "parse object");

    expect(parse_filter(".name", &prog) == 0, "filter .name");
    expect(eval_filter(&doc, root, &prog, out, sizeof(out)), "eval .name");
    expect(strcmp(out, "\"peak\"") == 0, "name value");

    root = parse_json(&doc, "[10,20,30]");
    expect(parse_filter("length", &prog) == 0, "filter length");
    expect(eval_filter(&doc, root, &prog, out, sizeof(out)), "eval length");
    expect(strcmp(out, "3") == 0, "array length");

    root = parse_json(&doc, "{\"b\":1,\"a\":2}");
    expect(parse_filter("keys", &prog) == 0, "filter keys");
    expect(eval_filter(&doc, root, &prog, out, sizeof(out)), "eval keys");
    expect(strcmp(out, "[\"a\",\"b\"]") == 0, "sorted keys");

    root = parse_json(&doc, "{\"items\":[1,2]}");
    expect(parse_filter(".items[]", &prog) == 0, "filter .items[]");
    expect(eval_filter(&doc, root, &prog, out, sizeof(out)), "eval first item");
    expect(strcmp(out, "1") == 0, "array elem");

    if (fails) {
        fprintf(stderr, "%d jq-lite test(s) failed\n", fails);
        return 1;
    }
    printf("test_cli_jq: ok\n");
    return 0;
}
