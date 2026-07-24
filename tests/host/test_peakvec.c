/*
 * Host tests for PeakVec — links kernel/peakvec.c under PEAK_HOST_TEST stubs
 * instead of mirroring embed/query logic in tests/host.
 */
#include "peakvec.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int64_t dot(const int16_t *a, const int16_t *b) {
    int64_t sum = 0;
    for (int i = 0; i < PEAKVEC_DIM; i++)
        sum += (int64_t)a[i] * b[i];
    return sum;
}

static int64_t norm_sq(const int16_t *a) {
    return dot(a, a);
}

static void upsert_text(const char *key, const char *text) {
    int16_t vec[PEAKVEC_DIM];
    peakvec_embed_text(text, vec);
    expect(peakvec_upsert("agent", key, vec, text) == 0, "upsert_text");
}

static void test_embed_similarity(void) {
    int16_t a[PEAKVEC_DIM], b[PEAKVEC_DIM], z[PEAKVEC_DIM];

    peakvec_embed_text("create fibonacci helper", a);
    peakvec_embed_text("create fibonacci helper", b);
    peakvec_embed_text("unrelated gardening tips", z);

    int64_t dot_ab = dot(a, b);
    int64_t dot_az = dot(a, z);
    int64_t na = norm_sq(a);
    int64_t nb = norm_sq(b);
    expect(dot_ab > 0 && na > 0 && nb > 0, "identical goals embed similarly");
    expect(dot_ab >= dot_az, "related goal scores >= unrelated");
}

static void test_basic_query(void) {
    peakvec_init();
    expect(peakvec_count("agent") == 0, "fresh index empty");

    int16_t vec[PEAKVEC_DIM];
    peakvec_embed_text("create hello.c", vec);
    expect(peakvec_upsert("agent", "goal1", vec, "create hello.c") == 0, "upsert");
    expect(peakvec_count("agent") == 1, "count after upsert");

    peakvec_embed_text("create hello.c", vec);
    struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
    int n = peakvec_query("agent", vec, 3, hits);
    expect(n >= 1, "query returns hit");
    expect(hits[0].key[0] != '\0', "query hit has key");
    expect(hits[0].score_milli > 900, "self-query strongly positive");
    expect(!strcmp(hits[0].key, "goal1"), "self-query top key");

    peakvec_embed_text("unrelated gardening tips", vec);
    n = peakvec_query("agent", vec, 3, hits);
    expect(n == 0 || hits[0].score_milli < 900, "unrelated query weaker");
}

static void test_topk_ordering(void) {
    peakvec_init();
    upsert_text("exact", "install nginx reverse proxy");
    upsert_text("near", "setup nginx reverse proxy config");
    upsert_text("mid", "configure http reverse proxy");
    upsert_text("far", "bake sourdough bread recipe");
    upsert_text("noise1", "orbital mechanics textbook notes");
    upsert_text("noise2", "watercolor landscape painting tips");
    upsert_text("noise3", "classical guitar fingerstyle drills");
    upsert_text("noise4", "fermentation temperature charts");

    int16_t q[PEAKVEC_DIM];
    peakvec_embed_text("install nginx reverse proxy", q);
    struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
    int n = peakvec_query("agent", q, 3, hits);
    expect(n == 3, "topk returns 3");
    expect(!strcmp(hits[0].key, "exact"), "best match is exact upsert");
    expect(hits[0].score_milli >= hits[1].score_milli, "score desc[0>=1]");
    expect(hits[1].score_milli >= hits[2].score_milli, "score desc[1>=2]");
    expect(strcmp(hits[0].key, "far") != 0, "far not top hit");
    expect(strcmp(hits[1].key, "far") != 0, "far not second hit");
}

static void test_early_out_stable(void) {
    /* Fill past top-k so early-out path runs; ordering must stay correct. */
    peakvec_init();
    char key[32];
    char text[64];
    for (int i = 0; i < 40; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        snprintf(text, sizeof(text), "document topic number %d about kernels", i);
        upsert_text(key, text);
    }
    upsert_text("needle", "peakvec cosine brute force query path");

    int16_t q[PEAKVEC_DIM];
    peakvec_embed_text("peakvec cosine brute force query path", q);
    struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
    int n = peakvec_query("agent", q, 5, hits);
    expect(n == 5, "early-out corpus returns topk");
    expect(!strcmp(hits[0].key, "needle"), "needle wins amid fillers");
    for (int i = 1; i < n; i++)
        expect(hits[i - 1].score_milli >= hits[i].score_milli, "early-out keeps order");
}

static void test_delete_and_zero_query(void) {
    peakvec_init();
    upsert_text("keep", "retain this memory entry");
    upsert_text("drop", "delete this memory entry");
    expect(peakvec_count("agent") == 2, "count before delete");
    expect(peakvec_delete("agent", "drop") == 0, "delete drop");
    expect(peakvec_count("agent") == 1, "count after delete");

    int16_t q[PEAKVEC_DIM];
    peakvec_embed_text("delete this memory entry", q);
    struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
    int n = peakvec_query("agent", q, 3, hits);
    expect(n == 1, "only keep remains");
    expect(!strcmp(hits[0].key, "keep"), "keep is sole hit");

    memset(q, 0, sizeof(q));
    n = peakvec_query("agent", q, 3, hits);
    expect(n == 0 || hits[0].score_milli == 0, "zero query yields no useful hit");
}

static void test_upsert_refreshes_norm(void) {
    peakvec_init();
    upsert_text("g", "alpha beta gamma");
    int16_t q[PEAKVEC_DIM];
    peakvec_embed_text("alpha beta gamma", q);
    struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
    int n = peakvec_query("agent", q, 1, hits);
    expect(n == 1 && hits[0].score_milli > 900, "pre-refresh self score");

    upsert_text("g", "completely different gardening tips");
    peakvec_embed_text("alpha beta gamma", q);
    n = peakvec_query("agent", q, 1, hits);
    expect(n == 1, "still one entry after refresh");
    expect(hits[0].score_milli < 900, "refreshed vector weakens old query");
}

int main(void) {
    test_embed_similarity();
    test_basic_query();
    test_topk_ordering();
    test_early_out_stable();
    test_delete_and_zero_query();
    test_upsert_refreshes_norm();

    if (fails) {
        fprintf(stderr, "%d peakvec test(s) failed\n", fails);
        return 1;
    }
    printf("test_peakvec: ok\n");
    return 0;
}
