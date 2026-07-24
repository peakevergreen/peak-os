#include "crypto.h"
#include "util.h"

/* ---- X25519 (RFC 7748 / TweetNaCl; limbs are int64) ---- */

typedef int64_t gf[16];

static void car25519(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b) {
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const gf n) {
    gf t, m;
    for (int i = 0; i < 16; i++)
        t[i] = n[i];
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int64_t b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, (int)(1 - b));
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void unpack25519(gf o, const uint8_t *n) {
    for (int i = 0; i < 16; i++)
        o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++)
        o[i] = a[i] + b[i];
}
static void Z(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++)
        o[i] = a[i] - b[i];
}
static void M(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++)
        t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++)
        t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++)
        o[i] = t[i];
    car25519(o);
    car25519(o);
}
static void S(gf o, const gf a) { M(o, a, a); }

static void inv25519(gf o, const gf i) {
    gf c;
    for (int a = 0; a < 16; a++)
        c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        S(c, c);
        if (a != 2 && a != 4)
            M(c, c, i);
    }
    for (int a = 0; a < 16; a++)
        o[a] = c[a];
}

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t z[32];
    memcpy(z, scalar, 32);
    z[0] &= 248;
    z[31] &= 127;
    z[31] |= 64;
    gf a, b, c, d, e, f, x;
    unpack25519(x, point);
    for (int i = 0; i < 16; i++) {
        b[i] = x[i];
        d[i] = a[i] = c[i] = 0;
    }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; i--) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r);
        sel25519(c, d, r);
        A(e, a, c);
        Z(a, a, c);
        A(c, b, d);
        Z(b, b, d);
        S(d, e);
        S(f, a);
        M(a, c, a);
        M(c, b, e);
        A(e, a, c);
        Z(a, a, c);
        S(b, a);
        Z(c, d, f);
        static const gf _121665 = {0xDB41, 1};
        M(a, c, _121665);
        A(a, a, d);
        M(c, c, a);
        M(a, d, f);
        M(d, b, x);
        S(b, e);
        sel25519(a, b, r);
        sel25519(c, d, r);
    }
    inv25519(c, c);
    M(a, a, c);
    pack25519(out, a);
}

void x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    static const uint8_t basepoint[32] = {9};
    x25519(out, scalar, basepoint);
}
