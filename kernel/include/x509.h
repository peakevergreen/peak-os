#ifndef PEAK_X509_H
#define PEAK_X509_H

#include "types.h"

#define X509_SAN_MAX  8
#define X509_NAME_MAX 128

struct x509_time {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

struct x509_cert {
    int has_validity;
    struct x509_time not_before;
    struct x509_time not_after;
    const uint8_t *spki;
    size_t spki_len;
    char sans[X509_SAN_MAX][X509_NAME_MAX];
    int san_count;
    int has_basic_constraints;
    int is_ca;
    int has_key_usage;
    uint16_t key_usage;
    uint8_t ski[32];
    size_t ski_len;
    uint8_t aki[32];
    size_t aki_len;
};

/* 0 ok, -1 malformed. Pointers in out->spki alias into der. */
int x509_parse_der(const uint8_t *der, size_t der_len, struct x509_cert *out);
int x509_time_compare(const struct x509_time *a, const struct x509_time *b);
/* 1 match, 0 mismatch, -1 no SANs. */
int x509_cert_hostname_match(const struct x509_cert *c, const char *sni);
/* 1 valid at now, 0 outside window, -1 missing validity. */
int x509_cert_time_valid(const struct x509_cert *c, const struct x509_time *now);

#endif
