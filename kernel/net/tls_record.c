#include "tls_internal.h"
#include "net_internal.h"
#include "timer.h"
#include "util.h"

void tls_wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void tls_wr24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

uint16_t tls_rd16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

uint32_t tls_rd24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static int tcp_read_full(uint8_t *buf, size_t need, uint32_t timeout_ticks) {
    size_t got = 0;
    uint64_t start = timer_ticks();
    while (got < need) {
        size_t n = 0;
        if (net_tcp_recv(buf + got, need - got, &n, 50) != 0) {
            if (timer_ticks() - start > timeout_ticks)
                return -1;
            net_poll();
            continue;
        }
        got += n;
        start = timer_ticks();
    }
    return 0;
}

static void seq_bytes(uint8_t out[8], uint64_t seq) {
    for (int i = 0; i < 8; i++)
        out[i] = (uint8_t)((seq >> (56 - 8 * i)) & 0xFF);
}

int tls_send_record(uint8_t type, const uint8_t *data, size_t len, int encrypted) {
    uint8_t rec[16600];
    if (!encrypted) {
        if (5 + len > sizeof(rec))
            return -1;
        rec[0] = type;
        rec[1] = 0x03;
        rec[2] = 0x03;
        tls_wr16(rec + 3, (uint16_t)len);
        memcpy(rec + 5, data, len);
        return net_tcp_send(rec, 5 + len);
    }

    uint8_t aad[13];
    seq_bytes(aad, client_seq);
    aad[8] = type;
    aad[9] = 0x03;
    aad[10] = 0x03;
    tls_wr16(aad + 11, (uint16_t)len);

    uint8_t cipher[16000];
    uint8_t tag[16];
    if (len > sizeof(cipher))
        return -1;

    if (cipher_kind == CIPHER_CHACHA20) {
        /* RFC 7905: nonce = IV XOR (0x00000000 || seq); no explicit nonce in record */
        uint8_t nonce[12];
        memcpy(nonce, client_iv, 12);
        uint8_t seqb[8];
        seq_bytes(seqb, client_seq);
        for (int i = 0; i < 8; i++)
            nonce[4 + i] ^= seqb[i];
        if (chacha20_poly1305_encrypt(client_key, nonce, aad, 13, data, len, cipher, tag) != 0)
            return -1;
        size_t payload = len + 16;
        rec[0] = type;
        rec[1] = 0x03;
        rec[2] = 0x03;
        tls_wr16(rec + 3, (uint16_t)payload);
        memcpy(rec + 5, cipher, len);
        memcpy(rec + 5 + len, tag, 16);
        client_seq++;
        return net_tcp_send(rec, 5 + payload);
    }

    /* AES-128-GCM: 4-byte fixed IV + 8-byte explicit nonce */
    uint8_t iv[12];
    memcpy(iv, client_iv, 4);
    uint8_t explicit[8];
    seq_bytes(explicit, client_seq);
    memcpy(iv + 4, explicit, 8);
    if (aes128_gcm_encrypt(client_key, iv, aad, 13, data, len, cipher, tag) != 0)
        return -1;
    size_t payload = 8 + len + 16;
    rec[0] = type;
    rec[1] = 0x03;
    rec[2] = 0x03;
    tls_wr16(rec + 3, (uint16_t)payload);
    memcpy(rec + 5, explicit, 8);
    memcpy(rec + 13, cipher, len);
    memcpy(rec + 13 + len, tag, 16);
    client_seq++;
    return net_tcp_send(rec, 5 + payload);
}

int tls_recv_record(uint8_t *type_out, uint8_t *buf, size_t cap, size_t *out_len,
                    uint32_t timeout_ticks, int encrypted) {
    uint8_t hdr[5];
    if (tcp_read_full(hdr, 5, timeout_ticks) != 0)
        return -1;
    uint8_t type = hdr[0];
    uint16_t len = tls_rd16(hdr + 3);
    /* TLS 1.2 ciphertext: 16384 plaintext + IV/tag overhead (RFC 5246 §6.2.3).
     * Static, not stack: the kernel stack is 8 KiB. Single TLS session only. */
    static uint8_t payload[16384 + 512];
    if (len > sizeof(payload))
        return -1;
    /* Header consumed — we are committed to this record. Bailing out here
     * desyncs the byte stream and the AEAD sequence, killing the session,
     * so wait out retransmission stalls (timer resets on progress). */
    uint32_t body_timeout = timeout_ticks > NET_TLS_RECORD_BODY_TICKS
                                ? timeout_ticks
                                : NET_TLS_RECORD_BODY_TICKS;
    if (tcp_read_full(payload, len, body_timeout) != 0)
        return -1;

    if (!encrypted) {
        if (len > cap)
            return -1;
        memcpy(buf, payload, len);
        *out_len = len;
        *type_out = type;
        return 0;
    }

    uint8_t aad[13];
    seq_bytes(aad, server_seq);
    aad[8] = type;
    aad[9] = 0x03;
    aad[10] = 0x03;

    if (cipher_kind == CIPHER_CHACHA20) {
        if (len < 16)
            return -1;
        size_t clen = len - 16;
        tls_wr16(aad + 11, (uint16_t)clen);
        if (clen > cap)
            return -1;
        uint8_t nonce[12];
        memcpy(nonce, server_iv, 12);
        uint8_t seqb[8];
        seq_bytes(seqb, server_seq);
        for (int i = 0; i < 8; i++)
            nonce[4 + i] ^= seqb[i];
        if (chacha20_poly1305_decrypt(server_key, nonce, aad, 13, payload, clen,
                                      payload + clen, buf) != 0)
            return -1;
        server_seq++;
        *out_len = clen;
        *type_out = type;
        return 0;
    }

    if (len < 8 + 16)
        return -1;
    uint8_t iv[12];
    memcpy(iv, server_iv, 4);
    memcpy(iv + 4, payload, 8);
    size_t clen = len - 8 - 16;
    tls_wr16(aad + 11, (uint16_t)clen);
    if (clen > cap)
        return -1;
    if (aes128_gcm_decrypt(server_key, iv, aad, 13, payload + 8, clen, payload + 8 + clen, buf) != 0)
        return -1;
    server_seq++;
    *out_len = clen;
    *type_out = type;
    return 0;
}
