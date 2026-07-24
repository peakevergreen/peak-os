/*
 * Minimal RSA verify for TLS 1.2 ServerKeyExchange (PKCS#1 v1.5 + PSS-SHA256).
 * Modulus up to 4096 bits. Peak-authored.
 */
#include "crypto.h"
#include "util.h"

#define RSA_MAX_BYTES 512
#define RSA_MAX_LIMBS (RSA_MAX_BYTES / 4)

static void be_to_limbs(uint32_t *limbs, size_t nlimbs, const uint8_t *be, size_t belen) {
    memset(limbs, 0, nlimbs * 4);
    for (size_t i = 0; i < belen; i++) {
        size_t bitpos = (belen - 1 - i) * 8;
        size_t li = bitpos / 32;
        size_t sh = bitpos % 32;
        if (li < nlimbs)
            limbs[li] |= ((uint32_t)be[i]) << sh;
    }
}

static void limbs_to_be(uint8_t *be, size_t belen, const uint32_t *limbs, size_t nlimbs) {
    memset(be, 0, belen);
    for (size_t i = 0; i < belen; i++) {
        size_t bitpos = (belen - 1 - i) * 8;
        size_t li = bitpos / 32;
        size_t sh = bitpos % 32;
        if (li < nlimbs)
            be[i] = (uint8_t)((limbs[li] >> sh) & 0xff);
    }
}

static int bn_cmp(const uint32_t *a, const uint32_t *b, size_t n) {
    for (size_t i = n; i-- > 0;) {
        if (a[i] < b[i])
            return -1;
        if (a[i] > b[i])
            return 1;
    }
    return 0;
}

static uint32_t bn_sub(uint32_t *a, const uint32_t *b, size_t n) {
    uint32_t borrow = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t x = (uint64_t)a[i] - b[i] - borrow;
        a[i] = (uint32_t)x;
        borrow = (x >> 63) ? 1u : 0u;
    }
    return borrow;
}

/* out = (a * b) mod m, all n limbs (n <= RSA_MAX_LIMBS). */
static void bn_mul_mod(uint32_t *out, const uint32_t *a, const uint32_t *b, const uint32_t *m,
                       size_t n) {
    uint32_t prod[RSA_MAX_LIMBS * 2];
    memset(prod, 0, sizeof(prod));
    for (size_t i = 0; i < n; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < n; j++) {
            uint64_t t = (uint64_t)prod[i + j] + (uint64_t)a[i] * b[j] + carry;
            prod[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        size_t k = i + n;
        while (carry) {
            uint64_t t = (uint64_t)prod[k] + carry;
            prod[k] = (uint32_t)t;
            carry = t >> 32;
            k++;
        }
    }
    /* Binary reduction: for bit = top..0, if prod >= (m << bit) then subtract. */
    size_t pn = 2 * n;
    for (int bit = (int)(n * 32); bit >= 0; bit--) {
        uint32_t sh[RSA_MAX_LIMBS * 2];
        memset(sh, 0, pn * 4);
        size_t limb_off = (size_t)bit / 32;
        size_t bit_off = (size_t)bit % 32;
        for (size_t i = 0; i < n; i++) {
            uint64_t v = (uint64_t)m[i] << bit_off;
            size_t di = i + limb_off;
            if (di < pn)
                sh[di] |= (uint32_t)v;
            if (bit_off && di + 1 < pn)
                sh[di + 1] |= (uint32_t)(v >> 32);
        }
        if (bn_cmp(prod, sh, pn) >= 0)
            bn_sub(prod, sh, pn);
    }
    memcpy(out, prod, n * 4);
}

static void bn_modexp(uint32_t *out, const uint32_t *base, uint32_t e, const uint32_t *mod,
                      size_t n) {
    uint32_t r[RSA_MAX_LIMBS];
    uint32_t b[RSA_MAX_LIMBS];
    memset(r, 0, n * 4);
    r[0] = 1;
    memcpy(b, base, n * 4);
    while (e) {
        if (e & 1u) {
            uint32_t t[RSA_MAX_LIMBS];
            bn_mul_mod(t, r, b, mod, n);
            memcpy(r, t, n * 4);
        }
        e >>= 1;
        if (e) {
            uint32_t t[RSA_MAX_LIMBS];
            bn_mul_mod(t, b, b, mod, n);
            memcpy(b, t, n * 4);
        }
    }
    memcpy(out, r, n * 4);
}

static int der_get_int(const uint8_t *der, size_t len, size_t *off, uint8_t *out, size_t *out_len,
                       size_t out_cap) {
    if (*off >= len || der[*off] != 0x02)
        return -1;
    (*off)++;
    if (*off >= len)
        return -1;
    size_t ilen;
    uint8_t lb = der[(*off)++];
    if (lb < 0x80) {
        ilen = lb;
    } else if (lb == 0x81) {
        if (*off >= len)
            return -1;
        ilen = der[(*off)++];
    } else if (lb == 0x82) {
        if (*off + 1 >= len)
            return -1;
        ilen = ((size_t)der[*off] << 8) | der[*off + 1];
        *off += 2;
    } else {
        return -1;
    }
    if (*off + ilen > len || ilen == 0)
        return -1;
    const uint8_t *p = der + *off;
    *off += ilen;
    while (ilen > 1 && p[0] == 0) {
        p++;
        ilen--;
    }
    if (ilen > out_cap)
        return -1;
    memcpy(out, p, ilen);
    *out_len = ilen;
    return 0;
}

static int leaf_rsa_pub(const uint8_t *leaf, size_t leaf_len, uint8_t *n, size_t *nlen,
                        uint32_t *e_out) {
    static const uint8_t oid[] = {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};
    if (!leaf || leaf_len < sizeof(oid) + 16)
        return -1;
    size_t oid_at = (size_t)-1;
    for (size_t i = 0; i + sizeof(oid) <= leaf_len; i++) {
        if (!memcmp(leaf + i, oid, sizeof(oid))) {
            oid_at = i;
            break;
        }
    }
    if (oid_at == (size_t)-1)
        return -1;
    size_t i = oid_at + sizeof(oid);
    if (i + 2 <= leaf_len && leaf[i] == 0x05 && leaf[i + 1] == 0x00)
        i += 2;
    if (i >= leaf_len || leaf[i] != 0x03)
        return -1;
    i++;
    size_t blen;
    uint8_t lb = leaf[i++];
    if (lb < 0x80)
        blen = lb;
    else if (lb == 0x81) {
        if (i >= leaf_len)
            return -1;
        blen = leaf[i++];
    } else if (lb == 0x82) {
        if (i + 1 >= leaf_len)
            return -1;
        blen = ((size_t)leaf[i] << 8) | leaf[i + 1];
        i += 2;
    } else
        return -1;
    if (i + blen > leaf_len || blen < 3)
        return -1;
    if (leaf[i] != 0x00)
        return -1;
    i++;
    if (leaf[i] != 0x30)
        return -1;
    size_t off = i + 1;
    lb = leaf[off++];
    if (lb & 0x80) {
        size_t nb = lb & 0x7f;
        if (nb > 2 || off + nb > leaf_len)
            return -1;
        off += nb;
    }
    size_t e_len = 0;
    uint8_t ebuf[8];
    if (der_get_int(leaf, leaf_len, &off, n, nlen, RSA_MAX_BYTES) != 0)
        return -1;
    if (der_get_int(leaf, leaf_len, &off, ebuf, &e_len, sizeof(ebuf)) != 0)
        return -1;
    uint32_t e = 0;
    for (size_t j = 0; j < e_len; j++)
        e = (e << 8) | ebuf[j];
    if (e != 3 && e != 65537)
        return -1;
    *e_out = e;
    return 0;
}

static void mgf1_sha256(uint8_t *mask, size_t mask_len, const uint8_t *seed, size_t seed_len) {
    uint8_t counter[4], hash[32], buf[520];
    size_t out = 0;
    uint32_t c = 0;
    while (out < mask_len) {
        counter[0] = (uint8_t)(c >> 24);
        counter[1] = (uint8_t)(c >> 16);
        counter[2] = (uint8_t)(c >> 8);
        counter[3] = (uint8_t)c;
        if (seed_len > 512)
            return;
        memcpy(buf, seed, seed_len);
        memcpy(buf + seed_len, counter, 4);
        sha256(buf, seed_len + 4, hash);
        size_t take = mask_len - out;
        if (take > 32)
            take = 32;
        memcpy(mask + out, hash, take);
        out += take;
        c++;
    }
}

static int emsa_pkcs1_v15_sha256_check(const uint8_t *em, size_t k, const uint8_t digest[32]) {
    static const uint8_t digest_info_prefix[] = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
        0x05, 0x00, 0x04, 0x20};
    size_t di_len = sizeof(digest_info_prefix) + 32;
    if (k < di_len + 11)
        return -1;
    if (em[0] != 0x00 || em[1] != 0x01)
        return -1;
    size_t ps_end = k - di_len - 1;
    for (size_t i = 2; i < ps_end; i++) {
        if (em[i] != 0xff)
            return -1;
    }
    if (em[ps_end] != 0x00)
        return -1;
    if (memcmp(em + ps_end + 1, digest_info_prefix, sizeof(digest_info_prefix)) != 0)
        return -1;
    if (memcmp(em + ps_end + 1 + sizeof(digest_info_prefix), digest, 32) != 0)
        return -1;
    return 0;
}

static int emsa_pss_sha256_check(const uint8_t *em, size_t emLen, const uint8_t digest[32]) {
    const size_t hLen = 32;
    const size_t sLen = 32;
    if (emLen < hLen + sLen + 2)
        return -1;
    if (em[emLen - 1] != 0xbc)
        return -1;
    size_t maskedDBLen = emLen - hLen - 1;
    const uint8_t *maskedDB = em;
    const uint8_t *H = em + maskedDBLen;
    uint8_t dbMask[RSA_MAX_BYTES];
    uint8_t DB[RSA_MAX_BYTES];
    mgf1_sha256(dbMask, maskedDBLen, H, hLen);
    for (size_t i = 0; i < maskedDBLen; i++)
        DB[i] = maskedDB[i] ^ dbMask[i];
    DB[0] &= 0x7f;
    size_t ps_len = maskedDBLen - sLen - 1;
    for (size_t i = 0; i < ps_len; i++) {
        if (DB[i] != 0x00)
            return -1;
    }
    if (DB[ps_len] != 0x01)
        return -1;
    const uint8_t *salt = DB + ps_len + 1;
    uint8_t mprime[8 + 32 + 32];
    memset(mprime, 0, 8);
    memcpy(mprime + 8, digest, 32);
    memcpy(mprime + 40, salt, sLen);
    uint8_t H2[32];
    sha256(mprime, sizeof(mprime), H2);
    if (memcmp(H, H2, 32) != 0)
        return -1;
    return 0;
}

int rsa_verify_sha256(const uint8_t *leaf_der, size_t leaf_len, const uint8_t digest[32],
                      size_t digest_len, const uint8_t *sig, size_t sig_len, int pss) {
    uint8_t n[RSA_MAX_BYTES], em[RSA_MAX_BYTES];
    size_t nlen = 0;
    uint32_t e = 0;
    if (!digest || digest_len != 32 || !sig || !leaf_der)
        return -1;
    if (leaf_rsa_pub(leaf_der, leaf_len, n, &nlen, &e) != 0)
        return -1;
    if (nlen < 64 || nlen > RSA_MAX_BYTES || sig_len != nlen)
        return -1;
    size_t nlimbs = (nlen + 3) / 4;
    uint32_t mod[RSA_MAX_LIMBS], base[RSA_MAX_LIMBS], res[RSA_MAX_LIMBS];
    be_to_limbs(mod, nlimbs, n, nlen);
    be_to_limbs(base, nlimbs, sig, sig_len);
    if (bn_cmp(base, mod, nlimbs) >= 0)
        return -1;
    bn_modexp(res, base, e, mod, nlimbs);
    limbs_to_be(em, nlen, res, nlimbs);
    if (pss)
        return emsa_pss_sha256_check(em, nlen, digest);
    return emsa_pkcs1_v15_sha256_check(em, nlen, digest);
}
