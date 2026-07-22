#include "peakvec.h"
#include "blobstore.h"
#include "vfs.h"
#include "heap.h"
#include "util.h"
#include "cap.h"
#include "serial.h"

#define PEAKVEC_MAGIC "PEAKVEC1"
#define PEAKVEC_MAX_ENTRIES 4096u
#define PEAKVEC_NS_DEFAULT "agent"

struct peakvec_entry {
    char    key[PEAKVEC_KEY_MAX];
    char    meta[PEAKVEC_META_MAX];
    int16_t vec[PEAKVEC_DIM];
    uint8_t in_use;
    uint8_t _pad[3];
};

struct peakvec_hdr {
    char     magic[8];
    uint32_t version;
    uint32_t dim;
    uint32_t count;
    uint32_t capacity;
    uint32_t blob_id; /* 0 = VFS fallback file */
};

/* Single default namespace kept resident for query speed; spilled to blob/VFS. */
static struct peakvec_entry *entries;
static uint32_t capacity;
static uint32_t count;
static uint32_t blob_id;
static int use_blob;
static int ready;

static void embed_add(int32_t acc[PEAKVEC_DIM], uint32_t h, int weight) {
    uint32_t idx = h % PEAKVEC_DIM;
    acc[idx] += weight;
    acc[(idx + 7) % PEAKVEC_DIM] += weight / 2;
}

void peakvec_embed_text(const char *text, int16_t out[PEAKVEC_DIM]) {
    int32_t acc[PEAKVEC_DIM];
    memset(acc, 0, sizeof(acc));
    if (!text) {
        memset(out, 0, PEAKVEC_DIM * sizeof(int16_t));
        return;
    }
    /* Lowercase-ish hashed unigrams + bigrams. */
    uint32_t prev = 0;
    int have_prev = 0;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            have_prev = 0;
            continue;
        }
        /* djb2-ish */
        uint32_t h = 5381u;
        h = ((h << 5) + h) + c;
        /* extend over alnum run */
        const char *q = p;
        while (*q) {
            unsigned char d = (unsigned char)*q;
            if (d >= 'A' && d <= 'Z')
                d = (unsigned char)(d - 'A' + 'a');
            if (!((d >= 'a' && d <= 'z') || (d >= '0' && d <= '9')))
                break;
            h = ((h << 5) + h) + d;
            q++;
        }
        embed_add(acc, h, 3);
        if (have_prev)
            embed_add(acc, prev ^ (h * 0x9e3779b9u), 2);
        prev = h;
        have_prev = 1;
        if (q > p)
            p = q - 1;
    }
    /* L2 normalize into int16 range. */
    int64_t sumsq = 0;
    for (int i = 0; i < PEAKVEC_DIM; i++)
        sumsq += (int64_t)acc[i] * acc[i];
    if (sumsq == 0) {
        memset(out, 0, PEAKVEC_DIM * sizeof(int16_t));
        return;
    }
    /* scale so max component fits int16; approximate sqrt via integer */
    uint64_t s = (uint64_t)sumsq;
    uint64_t root = 1;
    while (root * root < s)
        root++;
    if (root == 0)
        root = 1;
    for (int i = 0; i < PEAKVEC_DIM; i++) {
        int64_t v = ((int64_t)acc[i] * 10000) / (int64_t)root;
        if (v > 32767)
            v = 32767;
        if (v < -32768)
            v = -32768;
        out[i] = (int16_t)v;
    }
}

static int32_t cosine_milli(const int16_t *a, const int16_t *b) {
    int64_t dot = 0, na = 0, nb = 0;
    for (int i = 0; i < PEAKVEC_DIM; i++) {
        dot += (int64_t)a[i] * b[i];
        na += (int64_t)a[i] * a[i];
        nb += (int64_t)b[i] * b[i];
    }
    if (na == 0 || nb == 0)
        return 0;
    /* score = dot / sqrt(na*nb) * 1000 — use integer approx */
    uint64_t prod = (uint64_t)na * (uint64_t)nb;
    uint64_t root = 1;
    while (root * root < prod && root < 0x1000000ull)
        root++;
    if (root == 0)
        return 0;
    return (int32_t)((dot * 1000) / (int64_t)root);
}

static const char *ns_path(const char *ns, char *out, size_t out_len) {
    if (!ns || !ns[0])
        ns = PEAKVEC_NS_DEFAULT;
    /* /var/peak/vec/<ns>.pv1 */
    const char *prefix = "/var/peak/vec/";
    size_t i = 0;
    for (; prefix[i] && i + 1 < out_len; i++)
        out[i] = prefix[i];
    for (size_t j = 0; ns[j] && i + 5 < out_len; j++) {
        char c = ns[j];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            c = '_';
        out[i++] = c;
    }
    out[i++] = '.';
    out[i++] = 'p';
    out[i++] = 'v';
    out[i++] = '1';
    out[i] = '\0';
    return out;
}

static int persist_ram_fallback(void) {
    char path[VFS_PATH_MAX];
    ns_path(PEAKVEC_NS_DEFAULT, path, sizeof(path));
    vfs_mkdir("/var/peak");
    vfs_mkdir("/var/peak/vec");
    size_t hdr_sz = sizeof(struct peakvec_hdr);
    size_t body = (size_t)count * sizeof(struct peakvec_entry);
    size_t total = hdr_sz + body;
    uint8_t *buf = kmalloc(total ? total : hdr_sz);
    if (!buf)
        return -1;
    struct peakvec_hdr h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, PEAKVEC_MAGIC, 8);
    h.version = 1;
    h.dim = PEAKVEC_DIM;
    h.count = count;
    h.capacity = capacity;
    h.blob_id = 0;
    memcpy(buf, &h, sizeof(h));
    if (count && entries)
        memcpy(buf + hdr_sz, entries, body);
    int r = vfs_write_file(path, buf, total);
    kfree(buf);
    return r;
}

static int persist_blob(void) {
    if (!use_blob || !blob_id)
        return persist_ram_fallback();
    struct peakvec_hdr h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, PEAKVEC_MAGIC, 8);
    h.version = 1;
    h.dim = PEAKVEC_DIM;
    h.count = count;
    h.capacity = capacity;
    h.blob_id = blob_id;
    size_t need = sizeof(h) + (size_t)capacity * sizeof(struct peakvec_entry);
    if (blobstore_resize(blob_id, need) != 0)
        return -1;
    if (blobstore_write(blob_id, 0, &h, sizeof(h)) < 0)
        return -1;
    if (entries && capacity) {
        if (blobstore_write(blob_id, sizeof(h), entries,
                            (size_t)capacity * sizeof(struct peakvec_entry)) < 0)
            return -1;
    }
    /* Also keep a tiny pointer file in VFS for discoverability. */
    char path[VFS_PATH_MAX];
    ns_path(PEAKVEC_NS_DEFAULT, path, sizeof(path));
    vfs_mkdir("/var/peak/vec");
    char meta[64];
    size_t o = 0;
    const char *p = "blob:";
    while (*p)
        meta[o++] = *p++;
    char num[16];
    itoa_u(blob_id, num, 10);
    for (size_t i = 0; num[i] && o + 1 < sizeof(meta); i++)
        meta[o++] = num[i];
    meta[o++] = '\n';
    meta[o] = '\0';
    vfs_write_file(path, meta, o);
    return 0;
}

static int ensure_capacity(uint32_t need) {
    if (need <= capacity)
        return 0;
    uint32_t nc = capacity ? capacity * 2 : 64;
    while (nc < need)
        nc *= 2;
    if (nc > PEAKVEC_MAX_ENTRIES)
        nc = PEAKVEC_MAX_ENTRIES;
    if (need > nc)
        return -1;
    struct peakvec_entry *n = kmalloc(nc * sizeof(struct peakvec_entry));
    if (!n)
        return -1;
    memset(n, 0, nc * sizeof(struct peakvec_entry));
    if (entries && count)
        memcpy(n, entries, count * sizeof(struct peakvec_entry));
    if (entries)
        kfree(entries);
    entries = n;
    capacity = nc;
    if (use_blob && blob_id)
        (void)blobstore_resize(blob_id,
                               sizeof(struct peakvec_hdr) +
                                   (size_t)capacity * sizeof(struct peakvec_entry));
    return 0;
}

static int load_from_vfs(void) {
    char path[VFS_PATH_MAX];
    ns_path(PEAKVEC_NS_DEFAULT, path, sizeof(path));
    size_t n = 0;
    uint8_t hdrbuf[sizeof(struct peakvec_hdr) + 64];
    if (vfs_read_file(path, hdrbuf, sizeof(hdrbuf), &n) != 0 || n == 0)
        return -1;
    /* blob:ID pointer file */
    if (n >= 5 && !memcmp(hdrbuf, "blob:", 5)) {
        uint32_t id = 0;
        for (size_t i = 5; i < n && hdrbuf[i] >= '0' && hdrbuf[i] <= '9'; i++)
            id = id * 10 + (uint32_t)(hdrbuf[i] - '0');
        if (!id || !blobstore_available())
            return -1;
        struct peakvec_hdr h;
        if (blobstore_read(id, 0, &h, sizeof(h)) != (int)sizeof(h))
            return -1;
        if (memcmp(h.magic, PEAKVEC_MAGIC, 8) != 0 || h.dim != PEAKVEC_DIM)
            return -1;
        if (ensure_capacity(h.capacity ? h.capacity : h.count + 8) != 0)
            return -1;
        size_t body = (size_t)h.count * sizeof(struct peakvec_entry);
        if (body && blobstore_read(id, sizeof(h), entries, body) < 0)
            return -1;
        count = h.count;
        blob_id = id;
        use_blob = 1;
        return 0;
    }
    if (n < sizeof(struct peakvec_hdr) || memcmp(hdrbuf, PEAKVEC_MAGIC, 8) != 0)
        return -1;
    struct peakvec_hdr h;
    memcpy(&h, hdrbuf, sizeof(h));
    if (h.dim != PEAKVEC_DIM)
        return -1;
    size_t file_len = 0;
    /* re-read full file */
    uint8_t *full = kmalloc(sizeof(h) + (size_t)h.count * sizeof(struct peakvec_entry) + 8);
    if (!full)
        return -1;
    if (vfs_read_file(path, full, sizeof(h) + (size_t)h.count * sizeof(struct peakvec_entry),
                      &file_len) != 0) {
        kfree(full);
        return -1;
    }
    if (ensure_capacity(h.count + 8) != 0) {
        kfree(full);
        return -1;
    }
    if (h.count)
        memcpy(entries, full + sizeof(h), (size_t)h.count * sizeof(struct peakvec_entry));
    count = h.count;
    kfree(full);
    return 0;
}

void peakvec_init(void) {
    entries = NULL;
    capacity = 0;
    count = 0;
    blob_id = 0;
    use_blob = 0;
    ready = 0;
    vfs_mkdir("/var/peak/vec");
    if (ensure_capacity(64) != 0)
        return;
    ready = 1;
    if (load_from_vfs() == 0) {
        serial_write_str("peakvec: restored\n");
        return;
    }
    /* Fresh index: prefer blobstore so it can grow beyond heap snapshots. */
    if (blobstore_available()) {
        size_t initial = sizeof(struct peakvec_hdr) +
                         64 * sizeof(struct peakvec_entry);
        if (blobstore_create(&blob_id, initial) == 0) {
            use_blob = 1;
            (void)persist_blob();
            serial_write_str("peakvec: ready (blob)\n");
            return;
        }
    }
    serial_write_str("peakvec: ready (vfs)\n");
}

int peakvec_upsert(const char *ns, const char *key,
                   const int16_t vec[PEAKVEC_DIM], const char *meta) {
    (void)ns;
    if (!ready || !key || !vec)
        return -1;
    if (!cap_check(CAP_AGENT) && !cap_check(CAP_FS_WRITE))
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].in_use && !strcmp(entries[i].key, key)) {
            memcpy(entries[i].vec, vec, sizeof(entries[i].vec));
            entries[i].meta[0] = '\0';
            if (meta) {
                size_t j = 0;
                for (; meta[j] && j + 1 < PEAKVEC_META_MAX; j++)
                    entries[i].meta[j] = meta[j];
                entries[i].meta[j] = '\0';
            }
            (void)persist_blob();
            return 0;
        }
    }
    if (ensure_capacity(count + 1) != 0)
        return -1;
    struct peakvec_entry *e = &entries[count];
    memset(e, 0, sizeof(*e));
    size_t j = 0;
    for (; key[j] && j + 1 < PEAKVEC_KEY_MAX; j++)
        e->key[j] = key[j];
    e->key[j] = '\0';
    if (meta) {
        j = 0;
        for (; meta[j] && j + 1 < PEAKVEC_META_MAX; j++)
            e->meta[j] = meta[j];
        e->meta[j] = '\0';
    }
    memcpy(e->vec, vec, sizeof(e->vec));
    e->in_use = 1;
    count++;
    (void)persist_blob();
    return 0;
}

int peakvec_delete(const char *ns, const char *key) {
    (void)ns;
    if (!ready || !key)
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].in_use && !strcmp(entries[i].key, key)) {
            entries[i].in_use = 0;
            /* compact */
            if (i + 1 < count)
                memmove(&entries[i], &entries[i + 1],
                        (count - i - 1) * sizeof(struct peakvec_entry));
            count--;
            (void)persist_blob();
            return 0;
        }
    }
    return -1;
}

int peakvec_query(const char *ns, const int16_t query[PEAKVEC_DIM],
                  int topk, struct peakvec_hit *hits) {
    (void)ns;
    if (!ready || !query || !hits || topk <= 0)
        return -1;
    if (topk > PEAKVEC_TOPK_MAX)
        topk = PEAKVEC_TOPK_MAX;
    for (int i = 0; i < topk; i++) {
        hits[i].key[0] = '\0';
        hits[i].meta[0] = '\0';
        hits[i].score_milli = -1000000;
    }
    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!entries[i].in_use)
            continue;
        int32_t sc = cosine_milli(query, entries[i].vec);
        /* insert into topk */
        int place = topk;
        for (int k = 0; k < topk; k++) {
            if (sc > hits[k].score_milli) {
                place = k;
                break;
            }
        }
        if (place >= topk)
            continue;
        for (int k = topk - 1; k > place; k--)
            hits[k] = hits[k - 1];
        memcpy(hits[place].key, entries[i].key, PEAKVEC_KEY_MAX);
        memcpy(hits[place].meta, entries[i].meta, PEAKVEC_META_MAX);
        hits[place].score_milli = sc;
        if (found < topk)
            found++;
    }
    return found;
}

int peakvec_count(const char *ns) {
    (void)ns;
    return ready ? (int)count : 0;
}

/* op: 1=upsert_text 2=query_text 3=count 4=delete
 * upsert: a1=key ptr, a2=text ptr (kernel pointers when called from agent)
 * query: a1=text, a2=hits ptr, a3=topk
 */
int64_t peakvec_syscall(uint64_t op, uint64_t a1, uint64_t a2, uint64_t a3) {
    if (!cap_check(CAP_AGENT) && !cap_check(CAP_FS_READ))
        return -1;
    if (op == 3)
        return peakvec_count(PEAKVEC_NS_DEFAULT);
    if (op == 4) {
        const char *key = (const char *)(uintptr_t)a1;
        return peakvec_delete(PEAKVEC_NS_DEFAULT, key) == 0 ? 0 : -1;
    }
    if (op == 1) {
        const char *key = (const char *)(uintptr_t)a1;
        const char *text = (const char *)(uintptr_t)a2;
        int16_t vec[PEAKVEC_DIM];
        peakvec_embed_text(text, vec);
        return peakvec_upsert(PEAKVEC_NS_DEFAULT, key, vec, text) == 0 ? 0 : -1;
    }
    if (op == 2) {
        const char *text = (const char *)(uintptr_t)a1;
        struct peakvec_hit *hits = (struct peakvec_hit *)(uintptr_t)a2;
        int topk = (int)a3;
        int16_t vec[PEAKVEC_DIM];
        peakvec_embed_text(text, vec);
        return peakvec_query(PEAKVEC_NS_DEFAULT, vec, topk, hits);
    }
    return -1;
}
