#include "tls.h"
#include "crypto.h"
#include "tls_util.h"
#include "net.h"
#include "timer.h"
#include "util.h"
#include "serial.h"
#include "vfs.h"

#define TLS_CONTENT_HS   22
#define TLS_CONTENT_CCS  20
#define TLS_CONTENT_APP  23
#define TLS_CONTENT_ALERT 21

#define HS_CLIENT_HELLO  1
#define HS_SERVER_HELLO  2
#define HS_CERTIFICATE   11
#define HS_SERVER_KEY_EX 12
#define HS_SERVER_HELLO_DONE 14
#define HS_CLIENT_KEY_EX 16
#define HS_FINISHED      20

#define CS_ECDHE_RSA_AES128_GCM     0xC02F
#define CS_ECDHE_ECDSA_AES128_GCM   0xC02B
#define CS_ECDHE_RSA_CHACHA20       0xCCA8
#define CS_ECDHE_ECDSA_CHACHA20     0xCCA9

#define CIPHER_AES128_GCM  0
#define CIPHER_CHACHA20    1

static int tls_up;
static int cipher_kind;
static int use_ems;
static int cert_verified;
static int hostname_matched;
static int hostname_parse_skipped;
static const char *cert_fail_reason;
static char last_err[96];
#define TLS_PIN_MAX 8
static uint8_t trust_pins[TLS_PIN_MAX][32];
static int trust_pin_count;
static uint8_t client_random[32];
static uint8_t server_random[32];
static uint8_t master_secret[48];
static uint8_t client_key[32], server_key[32];
static uint8_t client_iv[12], server_iv[12];
static uint64_t client_seq, server_seq;
static struct sha256_ctx transcript;
static uint8_t rx_app[16384];
static size_t rx_app_len;
static uint8_t hs_reasm[24576];
static size_t hs_reasm_len;

static void tls_set_err(const char *msg) {
    size_t i = 0;
    if (!msg)
        msg = "unknown";
    for (; msg[i] && i + 1 < sizeof(last_err); i++)
        last_err[i] = msg[i];
    last_err[i] = '\0';
    serial_write_str("tls: ");
    serial_write_str(last_err);
    serial_write_str("\n");
}

const char *tls_last_error(void) {
    return last_err[0] ? last_err : "no error";
}

static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static int is_chacha(uint16_t cs) {
    return cs == CS_ECDHE_RSA_CHACHA20 || cs == CS_ECDHE_ECDSA_CHACHA20;
}

static int is_aes128_gcm(uint16_t cs) {
    return cs == CS_ECDHE_RSA_AES128_GCM || cs == CS_ECDHE_ECDSA_AES128_GCM;
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

static int send_record(uint8_t type, const uint8_t *data, size_t len, int encrypted) {
    uint8_t rec[16600];
    if (!encrypted) {
        if (5 + len > sizeof(rec))
            return -1;
        rec[0] = type;
        rec[1] = 0x03;
        rec[2] = 0x03;
        wr16(rec + 3, (uint16_t)len);
        memcpy(rec + 5, data, len);
        return net_tcp_send(rec, 5 + len);
    }

    uint8_t aad[13];
    seq_bytes(aad, client_seq);
    aad[8] = type;
    aad[9] = 0x03;
    aad[10] = 0x03;
    wr16(aad + 11, (uint16_t)len);

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
        wr16(rec + 3, (uint16_t)payload);
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
    wr16(rec + 3, (uint16_t)payload);
    memcpy(rec + 5, explicit, 8);
    memcpy(rec + 13, cipher, len);
    memcpy(rec + 13 + len, tag, 16);
    client_seq++;
    return net_tcp_send(rec, 5 + payload);
}

static int recv_record(uint8_t *type_out, uint8_t *buf, size_t cap, size_t *out_len,
                       uint32_t timeout_ticks, int encrypted) {
    uint8_t hdr[5];
    if (tcp_read_full(hdr, 5, timeout_ticks) != 0)
        return -1;
    uint8_t type = hdr[0];
    uint16_t len = rd16(hdr + 3);
    /* TLS 1.2 ciphertext: 16384 plaintext + IV/tag overhead (RFC 5246 §6.2.3).
     * Static, not stack: the kernel stack is 8 KiB. Single TLS session only. */
    static uint8_t payload[16384 + 512];
    if (len > sizeof(payload))
        return -1;
    /* Header consumed — we are committed to this record. Bailing out here
     * desyncs the byte stream and the AEAD sequence, killing the session,
     * so wait out retransmission stalls (timer resets on progress). */
    uint32_t body_timeout = timeout_ticks > 1200 ? timeout_ticks : 1200;
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
        wr16(aad + 11, (uint16_t)clen);
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
    wr16(aad + 11, (uint16_t)clen);
    if (clen > cap)
        return -1;
    if (aes128_gcm_decrypt(server_key, iv, aad, 13, payload + 8, clen, payload + 8 + clen, buf) != 0)
        return -1;
    server_seq++;
    *out_len = clen;
    *type_out = type;
    return 0;
}

static void transcript_add(const uint8_t *hs_msg, size_t len) {
    sha256_ctx_update(&transcript, hs_msg, len);
}

static int build_client_hello(uint8_t *out, size_t cap, const char *sni, size_t *out_len) {
    if (crypto_random(client_random, 32) != 0)
        return -1;
    /* TLS 1.2: first 4 bytes are GMT unix time; remaining 28 stay random. */
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

    /* Prefer ChaCha20; include renegotiation SCSV (0x00FF) */
    wr16(out + o, 10);
    o += 2;
    wr16(out + o, CS_ECDHE_ECDSA_CHACHA20);
    o += 2;
    wr16(out + o, CS_ECDHE_RSA_CHACHA20);
    o += 2;
    wr16(out + o, CS_ECDHE_ECDSA_AES128_GCM);
    o += 2;
    wr16(out + o, CS_ECDHE_RSA_AES128_GCM);
    o += 2;
    wr16(out + o, 0x00FF); /* TLS_EMPTY_RENEGOTIATION_INFO_SCSV */
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
    wr16(out + o, 4);
    o += 2;
    wr16(out + o, 2);
    o += 2;
    wr16(out + o, 0x001d);
    o += 2;

    wr16(out + o, 0x000b);
    o += 2;
    wr16(out + o, 2);
    o += 2;
    out[o++] = 1;
    out[o++] = 0;

    wr16(out + o, 0x000d);
    o += 2;
    wr16(out + o, 10);
    o += 2;
    wr16(out + o, 8);
    o += 2;
    wr16(out + o, 0x0403); /* ecdsa_secp256r1_sha256 */
    o += 2;
    wr16(out + o, 0x0503); /* ecdsa_secp256r1_sha384 */
    o += 2;
    wr16(out + o, 0x0804); /* rsa_pss_rsae_sha256 */
    o += 2;
    wr16(out + o, 0x0401); /* rsa_pkcs1_sha256 */
    o += 2;

    /* extended_master_secret */
    wr16(out + o, 0x0017);
    o += 2;
    wr16(out + o, 0);
    o += 2;

    /* renegotiation_info (empty) */
    wr16(out + o, 0xFF01);
    o += 2;
    wr16(out + o, 1);
    o += 2;
    out[o++] = 0;

    wr16(out + ext_len_at, (uint16_t)(o - ext_start));
    wr24(out + 1, (uint32_t)(o - 4));
    if (o > cap)
        return -1;
    *out_len = o;
    return 0;
}

static int parse_server_hello(const uint8_t *msg, size_t len, uint16_t *cs_out) {
    if (len < 40 || msg[0] != HS_SERVER_HELLO)
        return -1;
    uint32_t mlen = rd24(msg + 1);
    if (mlen + 4 > len)
        return -1;
    const uint8_t *p = msg + 4;
    const uint8_t *end = msg + 4 + mlen;
    if (p + 2 > end || p[0] != 0x03 || p[1] != 0x03)
        return -1;
    p += 2;
    if (p + 32 > end)
        return -1;
    memcpy(server_random, p, 32);
    p += 32;
    if (p >= end)
        return -1;
    uint8_t sid_len = *p++;
    if (p + sid_len + 3 > end)
        return -1;
    p += sid_len;
    *cs_out = rd16(p);
    p += 2;
    p++; /* compression */
    if (!is_aes128_gcm(*cs_out) && !is_chacha(*cs_out))
        return -1;

    use_ems = 0;
    if (p + 2 <= end) {
        uint16_t ext_total = rd16(p);
        p += 2;
        const uint8_t *ext_end = p + ext_total;
        if (ext_end > end)
            ext_end = end;
        while (p + 4 <= ext_end) {
            uint16_t et = rd16(p);
            p += 2;
            uint16_t el = rd16(p);
            p += 2;
            if (p + el > ext_end)
                break;
            if (et == 0x0017) /* extended_master_secret */
                use_ems = 1;
            p += el;
        }
    }
    return 0;
}

static int parse_server_key_exchange(const uint8_t *msg, size_t len, uint8_t peer_pub[32]) {
    if (len < 4 || msg[0] != HS_SERVER_KEY_EX)
        return -1;
    uint32_t mlen = rd24(msg + 1);
    if (mlen + 4 > len)
        return -1;
    const uint8_t *p = msg + 4;
    const uint8_t *end = msg + 4 + mlen;
    if (p + 4 > end)
        return -1;
    if (p[0] != 3)
        return -1;
    uint16_t curve = rd16(p + 1);
    if (curve != 0x001d)
        return -1;
    p += 3;
    uint8_t plen = *p++;
    if (plen != 32 || p + 32 > end)
        return -1;
    memcpy(peer_pub, p, 32);
    return 0;
}

static void derive_keys(const uint8_t premaster[32], uint16_t cs) {
    if (use_ems) {
        uint8_t session_hash[32];
        struct sha256_ctx tmp = transcript;
        sha256_ctx_final(&tmp, session_hash);
        tls_prf_sha256(premaster, 32, "extended master secret",
                       session_hash, 32, master_secret, 48);
    } else {
        uint8_t seed[64];
        memcpy(seed, client_random, 32);
        memcpy(seed + 32, server_random, 32);
        tls_prf_sha256(premaster, 32, "master secret", seed, 64, master_secret, 48);
    }

    uint8_t seed2[64];
    memcpy(seed2, server_random, 32);
    memcpy(seed2 + 32, client_random, 32);

    if (is_chacha(cs)) {
        cipher_kind = CIPHER_CHACHA20;
        uint8_t key_block[88];
        tls_prf_sha256(master_secret, 48, "key expansion", seed2, 64, key_block, 88);
        memcpy(client_key, key_block, 32);
        memcpy(server_key, key_block + 32, 32);
        memcpy(client_iv, key_block + 64, 12);
        memcpy(server_iv, key_block + 76, 12);
    } else {
        cipher_kind = CIPHER_AES128_GCM;
        uint8_t key_block[40];
        tls_prf_sha256(master_secret, 48, "key expansion", seed2, 64, key_block, 40);
        memcpy(client_key, key_block, 16);
        memcpy(server_key, key_block + 16, 16);
        memcpy(client_iv, key_block + 32, 4);
        memcpy(server_iv, key_block + 36, 4);
    }
}

static int send_finished(void) {
    uint8_t hash[32];
    struct sha256_ctx tmp = transcript;
    sha256_ctx_final(&tmp, hash);
    uint8_t verify[12];
    tls_prf_sha256(master_secret, 48, "client finished", hash, 32, verify, 12);
    uint8_t msg[16];
    msg[0] = HS_FINISHED;
    wr24(msg + 1, 12);
    memcpy(msg + 4, verify, 12);
    transcript_add(msg, 16);
    return send_record(TLS_CONTENT_HS, msg, 16, 1);
}

static int expect_finished(uint32_t timeout_ticks) {
    uint8_t type;
    uint8_t buf[64];
    size_t n = 0;
    if (recv_record(&type, buf, sizeof(buf), &n, timeout_ticks, 1) != 0)
        return -1;
    if (type == TLS_CONTENT_CCS) {
        if (recv_record(&type, buf, sizeof(buf), &n, timeout_ticks, 1) != 0)
            return -1;
    }
    if (type != TLS_CONTENT_HS || n < 16 || buf[0] != HS_FINISHED)
        return -1;
    transcript_add(buf, n);
    return 0;
}

int tls_trust_pin_sha256(const uint8_t pin[32]) {
    if (!pin || trust_pin_count >= TLS_PIN_MAX)
        return -1;
    memcpy(trust_pins[trust_pin_count], pin, 32);
    trust_pin_count++;
    return 0;
}

int tls_cert_verified(void) {
    return cert_verified;
}

int tls_hostname_matched(void) {
    return hostname_matched;
}

/*
 * Trust-on-first-use store: /etc/peak/tls-tofu holds "host:hex64" lines
 * mapping an SNI hostname to the SHA-256 of its Certificate message.
 * First contact records the digest; later contacts must match it.
 * Explicit pins (tls_trust_pin_sha256) always win when present.
 */
#define TOFU_PATH "/etc/peak/tls-tofu"
#define TOFU_MAX  8192

/* 1 = match, 0 = unknown host, -1 = MISMATCH (possible MITM). */
static int tofu_check(const char *host, const char *hexdigest) {
    static char buf[TOFU_MAX];
    size_t n = 0;
    if (!host || !host[0])
        return 0;
    if (vfs_read_file(TOFU_PATH, buf, sizeof(buf) - 1, &n) != 0 || n == 0)
        return 0;
    buf[n] = '\0';
    return tls_tofu_check_store(buf, host, hexdigest);
}

static void tofu_remember(const char *host, const char *hexdigest) {
    static char buf[TOFU_MAX];
    size_t n = 0;
    if (!host || !host[0])
        return;
    if (vfs_read_file(TOFU_PATH, buf, sizeof(buf) - 1, &n) != 0)
        n = 0;
    buf[n] = '\0';
    size_t need = strlen(host) + 1 + 64 + 1;
    if (n + need + 1 > sizeof(buf))
        return; /* store full — keep existing entries */
    size_t o = n;
    for (const char *s = host; *s; s++)
        buf[o++] = *s;
    buf[o++] = ':';
    for (const char *s = hexdigest; *s; s++)
        buf[o++] = *s;
    buf[o++] = '\n';
    vfs_write_file(TOFU_PATH, buf, o);
}

static int x509_names_match_sni(const uint8_t *cert, size_t cert_len, const char *sni_host) {
    int found_name = 0;
    int matched = 0;
    for (size_t i = 0; i + 6 < cert_len; i++) {
        if (cert[i] == 0x55 && cert[i + 1] == 0x04 && cert[i + 2] == 0x03) {
            size_t j = i + 3;
            while (j + 2 < cert_len && cert[j] != 0x0C)
                j++;
            if (j + 2 >= cert_len || cert[j] != 0x0C)
                continue;
            j++;
            size_t vlen = cert[j++];
            if (vlen >= 128 || j + vlen > cert_len)
                continue;
            char name[128];
            memcpy(name, cert + j, vlen);
            name[vlen] = '\0';
            found_name = 1;
            if (tls_hostname_matches_sni(name, sni_host))
                matched = 1;
        }
        if (cert[i] == 0x82 && i + 2 < cert_len) {
            size_t vlen = cert[i + 1];
            if (vlen > 0 && vlen < 128 && i + 2 + vlen <= cert_len) {
                char name[128];
                memcpy(name, cert + i + 2, vlen);
                name[vlen] = '\0';
                if (strchr(name, '.') && tls_hostname_matches_sni(name, sni_host)) {
                    found_name = 1;
                    matched = 1;
                }
            }
        }
    }
    if (matched)
        return 1;
    return found_name ? 0 : -1;
}

static int leaf_cert_from_msg(const uint8_t *cert_msg, size_t len, const uint8_t **leaf,
                              size_t *leaf_len) {
    if (!cert_msg || len < 10 || cert_msg[0] != HS_CERTIFICATE)
        return -1;
    size_t off = 4;
    if (off + 3 > len)
        return -1;
    size_t list_len = ((size_t)cert_msg[off] << 16) | ((size_t)cert_msg[off + 1] << 8) |
                      cert_msg[off + 2];
    off += 3;
    if (off + 3 > len || off + list_len > len)
        return -1;
    size_t cert_len = ((size_t)cert_msg[off] << 16) | ((size_t)cert_msg[off + 1] << 8) |
                      cert_msg[off + 2];
    off += 3;
    if (off + cert_len > len)
        return -1;
    *leaf = cert_msg + off;
    *leaf_len = cert_len;
    return 0;
}

static int verify_cert_hostname(const uint8_t *cert_msg, size_t len, const char *sni_host) {
    const uint8_t *leaf;
    size_t leaf_len;
    if (leaf_cert_from_msg(cert_msg, len, &leaf, &leaf_len) != 0)
        return -1;
    return x509_names_match_sni(leaf, leaf_len, sni_host);
}

/*
 * Verify the server Certificate message.
 * 1. If it matches an explicit trust pin → verified.
 * 2. Otherwise trust-on-first-use per SNI host: remember the digest on
 *    first contact, and fail closed if a known host's digest changes.
 * Full X.509 chain validation is out of scope for the in-guest client;
 * TOFU gives continuity (detects cert swaps) without a CA store.
 */
static int verify_cert_chain(const uint8_t *cert_msg, size_t len, const char *sni_host) {
    cert_fail_reason = NULL;
    hostname_matched = 0;
    hostname_parse_skipped = 0;
    if (!cert_msg || len < 10 || cert_msg[0] != HS_CERTIFICATE) {
        cert_fail_reason = "Malformed Certificate message";
        return 0;
    }
    uint8_t digest[32];
    sha256(cert_msg, len, digest);
    int trusted = 0;
    for (int i = 0; i < trust_pin_count; i++) {
        if (!memcmp(digest, trust_pins[i], 32)) {
            trusted = 1;
            break;
        }
    }
    if (!trusted) {
        char hexd[65];
        tls_hex_encode(digest, 32, hexd);
        int t = tofu_check(sni_host, hexd);
        if (t == 1)
            trusted = 1;
        else if (t < 0) {
            serial_write_str("tls: certificate changed for known host (rejecting)\n");
            cert_fail_reason = "Cert changed for known host; rm /etc/peak/tls-tofu to re-trust";
            return 0;
        } else {
            tofu_remember(sni_host, hexd);
            serial_write_str("tls: first contact — certificate remembered (tofu)\n");
            trusted = 1;
        }
    }
    if (!trusted)
        return 0;

    int hn = verify_cert_hostname(cert_msg, len, sni_host);
    if (hn == 1) {
        hostname_matched = 1;
        return 1;
    }
    if (hn == 0) {
        cert_fail_reason = "Certificate hostname mismatch";
        serial_write_str("tls: certificate hostname mismatch\n");
        return 0;
    }
    hostname_matched = 1;
    hostname_parse_skipped = 1;
    serial_write_str("tls: hostname parse skipped (tofu only)\n");
    return 1;
}

int tls_connect(uint32_t ip, uint16_t port, const char *sni_host, uint32_t timeout_ticks) {
    net_attempt_stats_note_tls();
    tls_close();
    last_err[0] = '\0';
    use_ems = 0;
    cert_verified = 0;
    hostname_matched = 0;
    hostname_parse_skipped = 0;
    if (net_tcp_connect(ip, port, timeout_ticks) != 0) {
        tls_set_err("TCP connect failed");
        return -1;
    }

    sha256_ctx_init(&transcript);
    client_seq = server_seq = 0;
    rx_app_len = 0;
    hs_reasm_len = 0;
    cipher_kind = CIPHER_AES128_GCM;

    uint8_t ch[640];
    size_t ch_len = 0;
    if (build_client_hello(ch, sizeof(ch), sni_host, &ch_len) != 0) {
        tls_set_err("ClientHello build failed");
        goto fail;
    }
    transcript_add(ch, ch_len);
    if (send_record(TLS_CONTENT_HS, ch, ch_len, 0) != 0) {
        tls_set_err("ClientHello send failed");
        goto fail;
    }

    uint8_t peer_pub[32];
    int got_sh = 0, got_ske = 0, got_done = 0, got_cert = 0;
    uint16_t cs = 0;
    uint64_t start = timer_ticks();

    while (!got_done && timer_ticks() - start < timeout_ticks) {
        uint8_t type;
        /* Static: 16 KiB does not fit on the 8 KiB kernel stack. */
        static uint8_t buf[16384];
        size_t n = 0;
        if (recv_record(&type, buf, sizeof(buf), &n, 200, 0) != 0)
            continue;
        if (type == TLS_CONTENT_ALERT) {
            tls_set_err("Server alert during handshake");
            goto fail;
        }
        if (type != TLS_CONTENT_HS)
            continue;

        if (hs_reasm_len + n > sizeof(hs_reasm)) {
            tls_set_err("Handshake buffer overflow");
            goto fail;
        }
        memcpy(hs_reasm + hs_reasm_len, buf, n);
        hs_reasm_len += n;

        size_t off = 0;
        while (off + 4 <= hs_reasm_len) {
            uint8_t hstype = hs_reasm[off];
            uint32_t hslen = rd24(hs_reasm + off + 1);
            if (off + 4 + hslen > hs_reasm_len)
                break;
            transcript_add(hs_reasm + off, 4 + hslen);

            if (hstype == HS_SERVER_HELLO) {
                if (parse_server_hello(hs_reasm + off, 4 + hslen, &cs) != 0) {
                    tls_set_err("ServerHello parse/cipher rejected");
                    goto fail;
                }
                got_sh = 1;
            } else if (hstype == HS_CERTIFICATE) {
                got_cert = 1;
                cert_verified = verify_cert_chain(hs_reasm + off, 4 + hslen, sni_host);
            } else if (hstype == HS_SERVER_KEY_EX) {
                if (parse_server_key_exchange(hs_reasm + off, 4 + hslen, peer_pub) != 0) {
                    tls_set_err("ServerKeyExchange parse failed");
                    goto fail;
                }
                got_ske = 1;
            } else if (hstype == HS_SERVER_HELLO_DONE) {
                got_done = 1;
            }
            off += 4 + hslen;
        }
        if (off > 0) {
            memmove(hs_reasm, hs_reasm + off, hs_reasm_len - off);
            hs_reasm_len -= off;
        }
        if (got_sh && got_ske && got_done)
            break;
    }
    if (!got_sh || !got_ske || !got_done) {
        tls_set_err("Timeout waiting for ServerHelloDone");
        goto fail;
    }
    /* Fail closed: missing Certificate or TOFU/pin mismatch → refuse session. */
    if (!got_cert || !cert_verified) {
        tls_set_err(cert_fail_reason ? cert_fail_reason
                                     : "TLS certificate unverified");
        serial_write_str("tls: reject unverified certificate\n");
        goto fail;
    }

    uint8_t priv[32], pub[32], premaster[32];
    if (crypto_random(priv, 32) != 0) {
        tls_set_err("RNG not ready for key generation");
        serial_write_str("tls: crypto RNG not ready\n");
        goto fail;
    }
    x25519_base(pub, priv);
    x25519(premaster, priv, peer_pub);

    uint8_t cke[40];
    cke[0] = HS_CLIENT_KEY_EX;
    wr24(cke + 1, 33);
    cke[4] = 32;
    memcpy(cke + 5, pub, 32);
    transcript_add(cke, 37);
    if (send_record(TLS_CONTENT_HS, cke, 37, 0) != 0) {
        tls_set_err("ClientKeyExchange send failed");
        goto fail;
    }

    derive_keys(premaster, cs);

    uint8_t ccs = 1;
    if (send_record(TLS_CONTENT_CCS, &ccs, 1, 0) != 0) {
        tls_set_err("CCS send failed");
        goto fail;
    }

    if (send_finished() != 0) {
        tls_set_err("Finished send failed");
        goto fail;
    }

    {
        uint8_t type;
        static uint8_t buf[8192]; /* NewSessionTicket can be large; keep off stack */
        size_t n = 0;
        start = timer_ticks();
        int got_ccs = 0;
        while (timer_ticks() - start < timeout_ticks) {
            if (!got_ccs) {
                if (recv_record(&type, buf, sizeof(buf), &n, 100, 0) != 0)
                    continue;
                if (type == TLS_CONTENT_ALERT) {
                    tls_set_err("Server alert after Finished (bad keys?)");
                    goto fail;
                }
                if (type == TLS_CONTENT_HS)
                    continue; /* NewSessionTicket etc. */
                if (type == TLS_CONTENT_CCS) {
                    got_ccs = 1;
                    continue;
                }
            } else {
                if (expect_finished(timeout_ticks) != 0) {
                    tls_set_err("Server Finished missing/decrypt failed");
                    goto fail;
                }
                tls_up = 1;
                serial_write_str(cipher_kind == CIPHER_CHACHA20
                                     ? (use_ems ? "tls: ok chacha20+ems\n" : "tls: ok chacha20\n")
                                     : (use_ems ? "tls: ok aes128-gcm+ems\n" : "tls: ok aes128-gcm\n"));
                return 0;
            }
        }
        tls_set_err("Timeout waiting for server CCS/Finished");
    }

fail:
    net_tcp_close();
    tls_up = 0;
    return -1;
}

int tls_ready(void) {
    return tls_up;
}

int tls_send(const void *data, size_t len) {
    if (!tls_up)
        return -1;
    const uint8_t *p = data;
    while (len) {
        size_t n = len > 1400 ? 1400 : len;
        if (send_record(TLS_CONTENT_APP, p, n, 1) != 0)
            return -1;
        p += n;
        len -= n;
    }
    return 0;
}

int tls_recv(void *buf, size_t cap, size_t *out_len, uint32_t timeout_ticks) {
    if (out_len)
        *out_len = 0;
    if (!tls_up)
        return -1;

    if (rx_app_len > 0) {
        size_t n = rx_app_len < cap ? rx_app_len : cap;
        memcpy(buf, rx_app, n);
        memmove(rx_app, rx_app + n, rx_app_len - n);
        rx_app_len -= n;
        if (out_len)
            *out_len = n;
        return 0;
    }

    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        uint8_t type;
        size_t n = 0;
        /* Decrypt straight into rx_app — it is sized for a max TLS record
         * (16384), so nothing is silently dropped. */
        if (recv_record(&type, rx_app, sizeof(rx_app), &n, 50, 1) != 0) {
            net_poll();
            continue;
        }
        if (type == TLS_CONTENT_ALERT) {
            /* close_notify (or fatal alert): session is over. Mark it so
             * callers can tell "stream ended" from "momentary stall". */
            tls_up = 0;
            return -1;
        }
        if (type != TLS_CONTENT_APP)
            continue;
        rx_app_len = n;
        size_t take = rx_app_len < cap ? rx_app_len : cap;
        memcpy(buf, rx_app, take);
        memmove(rx_app, rx_app + take, rx_app_len - take);
        rx_app_len -= take;
        if (out_len)
            *out_len = take;
        return 0;
    }
    return -1;
}

void tls_close(void) {
    if (tls_up) {
        uint8_t alert[2] = {1, 0};
        send_record(TLS_CONTENT_ALERT, alert, 2, 1);
    }
    net_tcp_close();
    tls_up = 0;
    cert_verified = 0;
    rx_app_len = 0;
    hs_reasm_len = 0;
    client_seq = server_seq = 0;
    /* Scrub session secrets (best effort). */
    extern void memzero_explicit(void *p, size_t n);
    memzero_explicit(client_random, sizeof(client_random));
    memzero_explicit(server_random, sizeof(server_random));
    memzero_explicit(master_secret, sizeof(master_secret));
    memzero_explicit(client_key, sizeof(client_key));
    memzero_explicit(server_key, sizeof(server_key));
    memzero_explicit(client_iv, sizeof(client_iv));
    memzero_explicit(server_iv, sizeof(server_iv));
}
