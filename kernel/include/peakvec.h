#ifndef PEAK_PEAKVEC_H
#define PEAK_PEAKVEC_H

#include "types.h"

/* PeakVec — Peak-authored local vector index (brute-force cosine).
 * Index data is block-backed via blobstore when available; otherwise a
 * bounded in-RAM fallback under /var/peak/vec keeps demos working. */

#define PEAKVEC_DIM       64
#define PEAKVEC_KEY_MAX   64
#define PEAKVEC_META_MAX  96
#define PEAKVEC_TOPK_MAX  8
#define PEAKVEC_NS_MAX    32

struct peakvec_hit {
    char     key[PEAKVEC_KEY_MAX];
    char     meta[PEAKVEC_META_MAX];
    int32_t  score_milli; /* cosine similarity * 1000 */
};

void peakvec_init(void);

/* Hashing n-gram embedder (no model weights, no network). */
void peakvec_embed_text(const char *text, int16_t out[PEAKVEC_DIM]);

int peakvec_upsert(const char *ns, const char *key,
                   const int16_t vec[PEAKVEC_DIM], const char *meta);
int peakvec_delete(const char *ns, const char *key);
int peakvec_query(const char *ns, const int16_t query[PEAKVEC_DIM],
                  int topk, struct peakvec_hit *hits);
int peakvec_count(const char *ns);

/* Syscall surface: op in a0 via peakvec_syscall. */
int64_t peakvec_syscall(uint64_t op, uint64_t a1, uint64_t a2, uint64_t a3);

#endif
