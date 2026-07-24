/*
 * Minimal X.509 DER leaf parser (parse-only; path building is tls/09).
 * Extracts SAN dNSName, validity, SPKI, BasicConstraints, KeyUsage, SKI/AKI.
 */
#include "x509.h"
#include "tls_util.h"
#include "util.h"

static int der_len(const uint8_t *p, size_t avail, size_t *hdr, size_t *len) {
    if (avail < 2)
        return -1;
    size_t i = 1;
    uint8_t b = p[i++];
    size_t L;
    if (b < 0x80) {
        L = b;
    } else if (b == 0x81) {
        if (i >= avail)
            return -1;
        L = p[i++];
    } else if (b == 0x82) {
        if (i + 1 >= avail)
            return -1;
        L = ((size_t)p[i] << 8) | p[i + 1];
        i += 2;
    } else {
        return -1;
    }
    if (i + L > avail)
        return -1;
    *hdr = i;
    *len = L;
    return 0;
}

static int expect_tag(const uint8_t **pp, size_t *remain, uint8_t tag, const uint8_t **out,
                      size_t *out_len) {
    if (*remain < 2 || (*pp)[0] != tag)
        return -1;
    size_t hdr, len;
    if (der_len(*pp, *remain, &hdr, &len) != 0)
        return -1;
    *out = *pp + hdr;
    *out_len = len;
    *pp += hdr + len;
    *remain -= hdr + len;
    return 0;
}

static int skip_tag(const uint8_t **pp, size_t *remain, uint8_t tag) {
    const uint8_t *v;
    size_t vl;
    return expect_tag(pp, remain, tag, &v, &vl);
}

static int parse_utctime(const char *buf, int *year, int *mon, int *day, int *hour, int *min,
                         int *sec) {
    for (int i = 0; i < 12; i++) {
        if (buf[i] < '0' || buf[i] > '9')
            return -1;
    }
    if (buf[12] != 'Z')
        return -1;
    *year = (buf[0] - '0') * 10 + (buf[1] - '0');
    *mon = (buf[2] - '0') * 10 + (buf[3] - '0');
    *day = (buf[4] - '0') * 10 + (buf[5] - '0');
    *hour = (buf[6] - '0') * 10 + (buf[7] - '0');
    *min = (buf[8] - '0') * 10 + (buf[9] - '0');
    *sec = (buf[10] - '0') * 10 + (buf[11] - '0');
    return 0;
}

static int parse_time(const uint8_t *p, size_t len, struct x509_time *t) {
    char buf[20];
    if (len < 13 || len > 15)
        return -1;
    memcpy(buf, p, len);
    buf[len] = '\0';
    int year, mon, day, hour, min, sec;
    if (len == 13) {
        if (parse_utctime(buf, &year, &mon, &day, &hour, &min, &sec) != 0)
            return -1;
        year += (year >= 50) ? 1900 : 2000;
    } else {
        for (int i = 0; i < 14; i++) {
            if (buf[i] < '0' || buf[i] > '9')
                return -1;
        }
        if (buf[14] != 'Z')
            return -1;
        year = (buf[0] - '0') * 1000 + (buf[1] - '0') * 100 + (buf[2] - '0') * 10 + (buf[3] - '0');
        mon = (buf[4] - '0') * 10 + (buf[5] - '0');
        day = (buf[6] - '0') * 10 + (buf[7] - '0');
        hour = (buf[8] - '0') * 10 + (buf[9] - '0');
        min = (buf[10] - '0') * 10 + (buf[11] - '0');
        sec = (buf[12] - '0') * 10 + (buf[13] - '0');
    }
    if (mon < 1 || mon > 12 || day < 1 || day > 31)
        return -1;
    t->year = (uint16_t)year;
    t->month = (uint8_t)mon;
    t->day = (uint8_t)day;
    t->hour = (uint8_t)hour;
    t->minute = (uint8_t)min;
    t->second = (uint8_t)sec;
    return 0;
}

static int cmp_time(const struct x509_time *a, const struct x509_time *b) {
    if (a->year != b->year)
        return a->year < b->year ? -1 : 1;
    if (a->month != b->month)
        return a->month < b->month ? -1 : 1;
    if (a->day != b->day)
        return a->day < b->day ? -1 : 1;
    if (a->hour != b->hour)
        return a->hour < b->hour ? -1 : 1;
    if (a->minute != b->minute)
        return a->minute < b->minute ? -1 : 1;
    if (a->second != b->second)
        return a->second < b->second ? -1 : 1;
    return 0;
}

static void add_san(struct x509_cert *c, const char *name, size_t nlen) {
    if (c->san_count >= X509_SAN_MAX || nlen == 0 || nlen >= X509_NAME_MAX)
        return;
    memcpy(c->sans[c->san_count], name, nlen);
    c->sans[c->san_count][nlen] = '\0';
    c->san_count++;
}

static int parse_extensions(const uint8_t *exts, size_t elen, struct x509_cert *out) {
    const uint8_t *p = exts;
    size_t rem = elen;
    while (rem > 0) {
        const uint8_t *seq;
        size_t slen;
        if (expect_tag(&p, &rem, 0x30, &seq, &slen) != 0)
            return -1;
        const uint8_t *q = seq;
        size_t qr = slen;
        const uint8_t *oid;
        size_t oid_len;
        if (expect_tag(&q, &qr, 0x06, &oid, &oid_len) != 0)
            return -1;
        if (qr > 0 && q[0] == 0x01) {
            const uint8_t *bv;
            size_t bl;
            if (expect_tag(&q, &qr, 0x01, &bv, &bl) != 0)
                return -1;
        }
        const uint8_t *oct;
        size_t ol;
        if (expect_tag(&q, &qr, 0x04, &oct, &ol) != 0)
            return -1;

        if (oid_len == 3 && oid[0] == 0x55 && oid[1] == 0x1d && oid[2] == 0x11) {
            const uint8_t *sp = oct;
            size_t sr = ol;
            const uint8_t *gn;
            size_t gl;
            if (expect_tag(&sp, &sr, 0x30, &gn, &gl) != 0)
                return -1;
            const uint8_t *gp = gn;
            size_t gr = gl;
            while (gr > 0) {
                size_t hdr, nlen;
                if (der_len(gp, gr, &hdr, &nlen) != 0)
                    return -1;
                if (gp[0] == 0x82)
                    add_san(out, (const char *)(gp + hdr), nlen);
                gp += hdr + nlen;
                gr -= hdr + nlen;
            }
        } else if (oid_len == 3 && oid[0] == 0x55 && oid[1] == 0x1d && oid[2] == 0x13) {
            const uint8_t *sp = oct;
            size_t sr = ol;
            const uint8_t *bc;
            size_t bl;
            if (expect_tag(&sp, &sr, 0x30, &bc, &bl) != 0)
                return -1;
            out->has_basic_constraints = 1;
            out->is_ca = 0;
            if (bl >= 3 && bc[0] == 0x01 && bc[1] == 0x01 && bc[2] == 0xff)
                out->is_ca = 1;
        } else if (oid_len == 3 && oid[0] == 0x55 && oid[1] == 0x1d && oid[2] == 0x0f) {
            const uint8_t *sp = oct;
            size_t sr = ol;
            const uint8_t *bits;
            size_t bl;
            if (expect_tag(&sp, &sr, 0x03, &bits, &bl) != 0)
                return -1;
            if (bl >= 2) {
                out->has_key_usage = 1;
                out->key_usage = bits[1];
                if (bl >= 3)
                    out->key_usage = (uint16_t)((bits[1] << 8) | bits[2]);
            }
        } else if (oid_len == 3 && oid[0] == 0x55 && oid[1] == 0x1d && oid[2] == 0x0e) {
            const uint8_t *sp = oct;
            size_t sr = ol;
            const uint8_t *ki;
            size_t kl;
            if (expect_tag(&sp, &sr, 0x04, &ki, &kl) != 0)
                return -1;
            if (kl <= sizeof(out->ski)) {
                memcpy(out->ski, ki, kl);
                out->ski_len = kl;
            }
        } else if (oid_len == 3 && oid[0] == 0x55 && oid[1] == 0x1d && oid[2] == 0x23) {
            const uint8_t *sp = oct;
            size_t sr = ol;
            const uint8_t *seq;
            size_t sl;
            if (expect_tag(&sp, &sr, 0x30, &seq, &sl) != 0)
                return -1;
            if (sl >= 2 && seq[0] == 0x80) {
                size_t hdr, kl;
                if (der_len(seq, sl, &hdr, &kl) == 0 && kl <= sizeof(out->aki)) {
                    memcpy(out->aki, seq + hdr, kl);
                    out->aki_len = kl;
                }
            }
        }
    }
    return 0;
}

int x509_parse_der(const uint8_t *der, size_t der_len, struct x509_cert *out) {
    if (!der || !out || der_len < 64)
        return -1;
    memset(out, 0, sizeof(*out));
    const uint8_t *p = der;
    size_t rem = der_len;
    const uint8_t *cert;
    size_t clen;
    if (expect_tag(&p, &rem, 0x30, &cert, &clen) != 0)
        return -1;
    p = cert;
    rem = clen;
    const uint8_t *tbs;
    size_t tbslen;
    if (expect_tag(&p, &rem, 0x30, &tbs, &tbslen) != 0)
        return -1;

    const uint8_t *q = tbs;
    size_t qr = tbslen;
    if (qr > 0 && q[0] == 0xa0) {
        if (skip_tag(&q, &qr, 0xa0) != 0)
            return -1;
    }
    if (skip_tag(&q, &qr, 0x02) != 0)
        return -1;
    if (skip_tag(&q, &qr, 0x30) != 0)
        return -1;
    if (skip_tag(&q, &qr, 0x30) != 0)
        return -1;
    const uint8_t *val;
    size_t vlen;
    if (expect_tag(&q, &qr, 0x30, &val, &vlen) != 0)
        return -1;
    {
        const uint8_t *vp = val;
        size_t vr = vlen;
        const uint8_t *t0, *t1;
        size_t l0, l1;
        if (vp[0] == 0x17) {
            if (expect_tag(&vp, &vr, 0x17, &t0, &l0) != 0)
                return -1;
        } else if (expect_tag(&vp, &vr, 0x18, &t0, &l0) != 0)
            return -1;
        if (vp[0] == 0x17) {
            if (expect_tag(&vp, &vr, 0x17, &t1, &l1) != 0)
                return -1;
        } else if (expect_tag(&vp, &vr, 0x18, &t1, &l1) != 0)
            return -1;
        if (parse_time(t0, l0, &out->not_before) != 0 || parse_time(t1, l1, &out->not_after) != 0)
            return -1;
        out->has_validity = 1;
    }
    if (skip_tag(&q, &qr, 0x30) != 0)
        return -1;
    {
        const uint8_t *spki_start = q;
        if (skip_tag(&q, &qr, 0x30) != 0)
            return -1;
        out->spki = spki_start;
        out->spki_len = (size_t)(q - spki_start);
    }
    while (qr > 0 && (q[0] == 0xa1 || q[0] == 0xa2)) {
        if (skip_tag(&q, &qr, q[0]) != 0)
            return -1;
    }
    if (qr > 0 && q[0] == 0xa3) {
        const uint8_t *ex;
        size_t exl;
        if (expect_tag(&q, &qr, 0xa3, &ex, &exl) != 0)
            return -1;
        const uint8_t *ep = ex;
        size_t er = exl;
        const uint8_t *seq;
        size_t sl;
        if (expect_tag(&ep, &er, 0x30, &seq, &sl) != 0)
            return -1;
        if (parse_extensions(seq, sl, out) != 0)
            return -1;
    }
    return 0;
}

int x509_time_compare(const struct x509_time *a, const struct x509_time *b) {
    return cmp_time(a, b);
}

int x509_cert_hostname_match(const struct x509_cert *c, const char *sni) {
    if (!c || !sni || !sni[0])
        return -1;
    if (c->san_count == 0)
        return -1;
    for (int i = 0; i < c->san_count; i++) {
        if (tls_hostname_matches_sni(c->sans[i], sni))
            return 1;
    }
    return 0;
}

int x509_cert_time_valid(const struct x509_cert *c, const struct x509_time *now) {
    if (!c || !c->has_validity || !now)
        return -1;
    if (cmp_time(now, &c->not_before) < 0)
        return 0;
    if (cmp_time(now, &c->not_after) > 0)
        return 0;
    return 1;
}
