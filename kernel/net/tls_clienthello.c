/*
 * TLS ClientHello serializer (shared by handshake + host golden tests).
 */
#include "tls_internal.h"
#include "tls_session.h"
#include "tls_ech.h"
#include "timer.h"
#include "util.h"
#include "random.h"
#include "util.h"
#include "random.h"

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t grease_from(uint8_t b) {
    static const uint16_t g[] = {
        0x0A0A, 0x1A1A, 0x2A2A, 0x3A3A, 0x4A4A, 0x5A5A, 0x6A6A, 0x7A7A,
        0x8A8A, 0x9A9A, 0xAAAA, 0xBABA, 0xCACA, 0xDADA, 0xEAEA, 0xFAFA};
    return g[b & 0x0f];
}

int tls_build_client_hello(uint8_t *out, size_t cap, const char *sni, size_t *out_len) {
    {
        int ech = tls_ech_prepare_client_hello();
        if (ech == -1)
            return -3;
        if (ech == -2)
            return -3;
    }
    if (crypto_random(client_random, 32) != 0)
        return -1;
    if (crypto_random(tls13_priv, 32) != 0)
        return -1;
    x25519_base(tls13_client_pub, tls13_priv);
    uint32_t t = (uint32_t)timer_ticks();
    client_random[0] = (uint8_t)(t >> 24);
    client_random[1] = (uint8_t)(t >> 16);
    client_random[2] = (uint8_t)(t >> 8);
    client_random[3] = (uint8_t)t;

    uint16_t grease_cs = grease_from(client_random[4]);
    uint16_t grease_grp = grease_from(client_random[5] ^ 0x55);
    if (grease_grp == grease_cs)
        grease_grp = grease_from(client_random[5] ^ 0xaa);
    uint16_t grease_ext = grease_from(client_random[6] ^ 0x33);
    if (grease_ext == grease_cs || grease_ext == grease_grp)
        grease_ext = grease_from(client_random[6] ^ 0xcc);

    uint8_t resume_ticket[TLS_SESSION_TICKET_MAX];
    size_t resume_tlen = sizeof(resume_ticket);
    struct tls_session_meta resume_meta;
    int resume_have = 0;
    int resume_tls13 = 0;
    if (sni && sni[0]) {
        resume_have = tls_session_get(sni, resume_ticket, &resume_tlen, &resume_meta);
        if (resume_have)
            resume_tls13 = resume_meta.tls13;
    }

    out[0] = HS_CLIENT_HELLO;
    size_t o = 4;
    out[o++] = 0x03;
    out[o++] = 0x03;
    memcpy(out + o, client_random, 32);
    o += 32;
    out[o++] = 0;

    wr16(out + o, 22);
    o += 2;
    wr16(out + o, grease_cs);
    o += 2;
    wr16(out + o, CS_TLS13_AES128_GCM);
    o += 2;
    wr16(out + o, CS_TLS13_CHACHA20);
    o += 2;
    wr16(out + o, CS_TLS13_AES256_GCM);
    o += 2;
    wr16(out + o, CS_ECDHE_ECDSA_CHACHA20);
    o += 2;
    wr16(out + o, CS_ECDHE_RSA_CHACHA20);
    o += 2;
    wr16(out + o, CS_ECDHE_ECDSA_AES128_GCM);
    o += 2;
    wr16(out + o, CS_ECDHE_RSA_AES128_GCM);
    o += 2;
    wr16(out + o, CS_ECDHE_ECDSA_AES256_GCM);
    o += 2;
    wr16(out + o, CS_ECDHE_RSA_AES256_GCM);
    o += 2;
    wr16(out + o, 0x00FF);
    o += 2;

    out[o++] = 1;
    out[o++] = 0;

    size_t ext_len_at = o;
    o += 2;
    size_t ext_start = o;

    wr16(out + o, grease_ext);
    o += 2;
    wr16(out + o, 0);
    o += 2;

    if (sni && sni[0]) {
        size_t sl = strlen(sni);
        wr16(out + o, 0x0000);
        o += 2;
        wr16(out + o, (uint16_t)(sl + 5));
        o += 2;
        wr16(out + o, (uint16_t)(sl + 3));
        o += 2;
        out[o++] = 0;
        wr16(out + o, (uint16_t)sl);
        o += 2;
        memcpy(out + o, sni, sl);
        o += sl;
    }

    wr16(out + o, 0x000a);
    o += 2;
    wr16(out + o, 10);
    o += 2;
    wr16(out + o, 8);
    o += 2;
    wr16(out + o, grease_grp);
    o += 2;
    wr16(out + o, 0x001d);
    o += 2;
    wr16(out + o, 0x0017);
    o += 2;
    wr16(out + o, 0x0018);
    o += 2;

    wr16(out + o, 0x000b);
    o += 2;
    wr16(out + o, 2);
    o += 2;
    out[o++] = 1;
    out[o++] = 0;

    wr16(out + o, 0x000d);
    o += 2;
    wr16(out + o, 14);
    o += 2;
    wr16(out + o, 12);
    o += 2;
    wr16(out + o, 0x0403);
    o += 2;
    wr16(out + o, 0x0503);
    o += 2;
    wr16(out + o, 0x0804);
    o += 2;
    wr16(out + o, 0x0805);
    o += 2;
    wr16(out + o, 0x0401);
    o += 2;
    wr16(out + o, 0x0501);
    o += 2;

    wr16(out + o, 0x0010);
    o += 2;
    wr16(out + o, 14);
    o += 2;
    wr16(out + o, 12);
    o += 2;
    out[o++] = 2;
    memcpy(out + o, "h2", 2);
    o += 2;
    out[o++] = 8;
    memcpy(out + o, "http/1.1", 8);
    o += 8;

    wr16(out + o, 0x0017);
    o += 2;
    wr16(out + o, 0);
    o += 2;

    if (!resume_tls13) {
        wr16(out + o, 0x0023);
        o += 2;
        if (resume_have && resume_tlen > 0 && resume_tlen <= TLS_SESSION_TICKET_MAX) {
            wr16(out + o, (uint16_t)resume_tlen);
            o += 2;
            memcpy(out + o, resume_ticket, resume_tlen);
            o += resume_tlen;
        } else {
            wr16(out + o, 0);
            o += 2;
        }
    }

    wr16(out + o, 0xFF01);
    o += 2;
    wr16(out + o, 1);
    o += 2;
    out[o++] = 0;

    wr16(out + o, 0x002b);
    o += 2;
    wr16(out + o, 5);
    o += 2;
    out[o++] = 4;
    wr16(out + o, 0x0304);
    o += 2;
    wr16(out + o, 0x0303);
    o += 2;

    wr16(out + o, 0x0033);
    o += 2;
    wr16(out + o, 38);
    o += 2;
    wr16(out + o, 36);
    o += 2;
    wr16(out + o, 0x001d);
    o += 2;
    wr16(out + o, 32);
    o += 2;
    memcpy(out + o, tls13_client_pub, 32);
    o += 32;

    if (resume_have && resume_tls13 && resume_tlen > 0 && resume_tlen <= TLS_SESSION_TICKET_MAX) {
        wr16(out + o, 0x002d);
        o += 2;
        wr16(out + o, 2);
        o += 2;
        out[o++] = 1;
        out[o++] = 1;

        wr16(out + o, 0x0029);
        o += 2;
        size_t psk_ext_len_at = o;
        o += 2;

        uint16_t ident_len = (uint16_t)(2 + resume_tlen + 4);
        wr16(out + o, ident_len);
        o += 2;
        wr16(out + o, (uint16_t)resume_tlen);
        o += 2;
        memcpy(out + o, resume_ticket, resume_tlen);
        o += resume_tlen;
        wr32(out + o, 0);
        o += 4;

        size_t binders_len_at = o;
        o += 2; /* binders length placeholder */
        size_t binder_entry_at = o;
        size_t hash_len_pre = resume_meta.sha384 ? 48u : 32u;
        out[o++] = (uint8_t)hash_len_pre;
        memset(out + o, 0, hash_len_pre);
        o += hash_len_pre;

        wr16(out + psk_ext_len_at, (uint16_t)(o - psk_ext_len_at - 2));

        wr16(out + ext_len_at, (uint16_t)(o - ext_start));
        wr24(out + 1, (uint32_t)(o - 4));
        if (o > cap)
            return -2;

        uint8_t binder[48];
        size_t hash_len = resume_meta.sha384 ? 48u : 32u;
        int sha384 = resume_meta.sha384 ? 1 : 0;
        int br = -1;
        if (resume_meta.res_master_len)
            br = tls13_compute_psk_binder(sha384, resume_meta.res_master, resume_meta.res_master_len,
                                          resume_meta.ticket_nonce_len ? resume_meta.ticket_nonce
                                                                         : NULL,
                                          resume_meta.ticket_nonce_len, out, o, binders_len_at,
                                          binder, hash_len);
        else
            br = tls13_compute_psk_binder_from_ticket(sha384, resume_ticket, resume_tlen, out, o,
                                                      binders_len_at, binder, hash_len);
        if (br != 0)
            return -2;
        memcpy(out + binder_entry_at + 1, binder, hash_len);
        memzero_explicit(binder, sizeof(binder));
        wr16(out + binders_len_at, (uint16_t)(1 + hash_len));
        *out_len = o;
        return 0;
    }


    wr16(out + ext_len_at, (uint16_t)(o - ext_start));
    wr24(out + 1, (uint32_t)(o - 4));
    if (o > cap)
        return -2;
    *out_len = o;
    return 0;
}
