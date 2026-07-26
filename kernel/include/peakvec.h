#ifndef PEAK_PEAKVEC_H
#define PEAK_PEAKVEC_H

#include "types.h"

/* PeakVec — Peak-authored local vector index (brute-force cosine).
 * Index data is block-backed via blobstore when available; otherwise a
 * bounded in-RAM fallback under /var/peak/vec keeps demos working. */

#define PEAKVEC_DIM       64
#define PEAKVEC_KEY_MAX   64
#define PEAKVEC_META_MAX  96
#define PEAKVEC_TOPK_MAX      8
#define PEAKVEC_NS_MAX        32
#define PEAKVEC_ANN_BUCKETS   16
#define PEAKVEC_ANN_THRESHOLD 64

struct peakvec_hit {
    char     key[PEAKVEC_KEY_MAX];
    char     meta[PEAKVEC_META_MAX];
    int32_t  score_milli; /* cosine similarity * 1000 */
};

struct peakvec_query_explain {
    uint8_t  use_ann;
    uint8_t  query_bucket;
    uint8_t  ann_shortcut;
    uint8_t  multi_bucket_probe;
    uint8_t  probe_buckets;
    uint32_t ns_live;
    uint32_t bucket_probed;
    uint32_t remainder;
    uint32_t scored;
    uint32_t skipped_early;
    uint32_t elapsed_us;
};

struct peakvec_stats {
    uint32_t count;
    uint32_t capacity;
    uint32_t max_entries;
    uint8_t  use_blob;
    uint32_t blob_id;
    uint8_t  ann_active;
    uint32_t ann_threshold;
    uint32_t bucket_hist[PEAKVEC_ANN_BUCKETS];
};

void peakvec_init(void);

/* Hashing n-gram embedder (no model weights, no network). */
void peakvec_embed_text(const char *text, int16_t out[PEAKVEC_DIM]);

int peakvec_upsert(const char *ns, const char *key,
                   const int16_t vec[PEAKVEC_DIM], const char *meta);
int peakvec_delete(const char *ns, const char *key);
int peakvec_query(const char *ns, const int16_t query[PEAKVEC_DIM],
                  int topk, struct peakvec_hit *hits);
int peakvec_query_ex(const char *ns, const int16_t query[PEAKVEC_DIM],
                     int topk, struct peakvec_hit *hits,
                     struct peakvec_query_explain *explain);
int peakvec_count(const char *ns);

void peakvec_stats(const char *ns, struct peakvec_stats *out);

/* Syscall surface: op in a0 via peakvec_syscall. */
int64_t peakvec_syscall(uint64_t op, uint64_t a1, uint64_t a2, uint64_t a3);

#endif
