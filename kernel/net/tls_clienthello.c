/*
 * TLS ClientHello serializer (shared by handshake + host golden tests).
 */
#include "tls_internal.h"
#include "timer.h"
#include "util.h"

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

/* 0 ok; -1 crypto RNG not ready; -2 message exceeds cap (post-serialize). */
int tls_build_client_hello(uint8_t *out, size_t cap, const char *sni, size_t *out_len) {
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
    out[0] = HS_CLIENT_HELLO;
    size_t o = 4;
    out[o++] = 0x03;
    out[o++] = 0x03;
    memcpy(out + o, client_random, 32);
    o += 32;
    out[o++] = 0;

    wr16(out + o, 20);
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
    wr16(out + o, 6);
    o += 2;
    wr16(out + o, 4);
    o += 2;
    wr16(out + o, 0x001d);
    o += 2;
    wr16(out + o, 0x0017);
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
    wr16(out + o, 11);
    o += 2;
    wr16(out + o, 9);
    o += 2;
    out[o++] = 8;
    memcpy(out + o, "http/1.1", 8);
    o += 8;

    wr16(out + o, 0x0017);
    o += 2;
    wr16(out + o, 0);
    o += 2;

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

    wr16(out + ext_len_at, (uint16_t)(o - ext_start));
    wr24(out + 1, (uint32_t)(o - 4));
    if (o > cap)
        return -2;
    *out_len = o;
    return 0;
}
