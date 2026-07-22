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

int main(void) {
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

    /* In-memory upsert/query via real kernel peakvec.c. */
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
    expect(hits[0].score_milli > 0, "self-query positive score");

    peakvec_embed_text("unrelated gardening tips", vec);
    n = peakvec_query("agent", vec, 3, hits);
    expect(n == 0 || hits[0].score_milli < 900000, "unrelated query weaker");

    if (fails) {
        fprintf(stderr, "%d peakvec test(s) failed\n", fails);
        return 1;
    }
    printf("test_peakvec: ok\n");
    return 0;
}
