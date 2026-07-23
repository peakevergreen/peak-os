#include "tls_internal.h"
#include "net.h"
#include "serial.h"
#include "timer.h"
#include "util.h"

static int is_chacha(uint16_t cs) {
    return cs == CS_ECDHE_RSA_CHACHA20 || cs == CS_ECDHE_ECDSA_CHACHA20;
}

static int is_aes128_gcm(uint16_t cs) {
    return cs == CS_ECDHE_RSA_AES128_GCM || cs == CS_ECDHE_ECDSA_AES128_GCM;
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
    tls_wr16(out + o, 10);
    o += 2;
    tls_wr16(out + o, CS_ECDHE_ECDSA_CHACHA20);
    o += 2;
    tls_wr16(out + o, CS_ECDHE_RSA_CHACHA20);
    o += 2;
    tls_wr16(out + o, CS_ECDHE_ECDSA_AES128_GCM);
    o += 2;
    tls_wr16(out + o, CS_ECDHE_RSA_AES128_GCM);
    o += 2;
    tls_wr16(out + o, 0x00FF); /* TLS_EMPTY_RENEGOTIATION_INFO_SCSV */
    o += 2;

    out[o++] = 1;
    out[o++] = 0;

    size_t ext_len_at = o;
    o += 2;
    size_t ext_start = o;

    if (sni && sni[0]) {
        size_t sl = strlen(sni);
        tls_wr16(out + o, 0x0000);
        o += 2;
        tls_wr16(out + o, (uint16_t)(sl + 5));
        o += 2;
        tls_wr16(out + o, (uint16_t)(sl + 3));
        o += 2;
        out[o++] = 0;
        tls_wr16(out + o, (uint16_t)sl);
        o += 2;
        memcpy(out + o, sni, sl);
        o += sl;
    }

    tls_wr16(out + o, 0x000a);
    o += 2;
    tls_wr16(out + o, 4);
    o += 2;
    tls_wr16(out + o, 2);
    o += 2;
    tls_wr16(out + o, 0x001d);
    o += 2;

    tls_wr16(out + o, 0x000b);
    o += 2;
    tls_wr16(out + o, 2);
    o += 2;
    out[o++] = 1;
    out[o++] = 0;

    tls_wr16(out + o, 0x000d);
    o += 2;
    tls_wr16(out + o, 10);
    o += 2;
    tls_wr16(out + o, 8);
    o += 2;
    tls_wr16(out + o, 0x0403); /* ecdsa_secp256r1_sha256 */
    o += 2;
    tls_wr16(out + o, 0x0503); /* ecdsa_secp256r1_sha384 */
    o += 2;
    tls_wr16(out + o, 0x0804); /* rsa_pss_rsae_sha256 */
    o += 2;
    tls_wr16(out + o, 0x0401); /* rsa_pkcs1_sha256 */
    o += 2;

    /* extended_master_secret */
    tls_wr16(out + o, 0x0017);
    o += 2;
    tls_wr16(out + o, 0);
    o += 2;

    /* renegotiation_info (empty) */
    tls_wr16(out + o, 0xFF01);
    o += 2;
    tls_wr16(out + o, 1);
    o += 2;
    out[o++] = 0;

    tls_wr16(out + ext_len_at, (uint16_t)(o - ext_start));
    tls_wr24(out + 1, (uint32_t)(o - 4));
    if (o > cap)
        return -1;
    *out_len = o;
    return 0;
}

static int parse_server_hello(const uint8_t *msg, size_t len, uint16_t *cs_out) {
    if (len < 40 || msg[0] != HS_SERVER_HELLO)
        return -1;
    uint32_t mlen = tls_rd24(msg + 1);
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
    *cs_out = tls_rd16(p);
    p += 2;
    p++; /* compression */
    if (!is_aes128_gcm(*cs_out) && !is_chacha(*cs_out))
        return -1;

    use_ems = 0;
    if (p + 2 <= end) {
        uint16_t ext_total = tls_rd16(p);
        p += 2;
        const uint8_t *ext_end = p + ext_total;
        if (ext_end > end)
            ext_end = end;
        while (p + 4 <= ext_end) {
            uint16_t et = tls_rd16(p);
            p += 2;
            uint16_t el = tls_rd16(p);
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
    uint32_t mlen = tls_rd24(msg + 1);
    if (mlen + 4 > len)
        return -1;
    const uint8_t *p = msg + 4;
    const uint8_t *end = msg + 4 + mlen;
    if (p + 4 > end)
        return -1;
    if (p[0] != 3)
        return -1;
    uint16_t curve = tls_rd16(p + 1);
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
    tls_wr24(msg + 1, 12);
    memcpy(msg + 4, verify, 12);
    transcript_add(msg, 16);
    return tls_send_record(TLS_CONTENT_HS, msg, 16, 1);
}

static int expect_finished(uint32_t timeout_ticks) {
    uint8_t type;
    uint8_t buf[64];
    size_t n = 0;
    if (tls_recv_record(&type, buf, sizeof(buf), &n, timeout_ticks, 1) != 0)
        return -1;
    if (type == TLS_CONTENT_CCS) {
        if (tls_recv_record(&type, buf, sizeof(buf), &n, timeout_ticks, 1) != 0)
            return -1;
    }
    if (type != TLS_CONTENT_HS || n < 16 || buf[0] != HS_FINISHED)
        return -1;
    transcript_add(buf, n);
    return 0;
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
    if (tls_send_record(TLS_CONTENT_HS, ch, ch_len, 0) != 0) {
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
        if (tls_recv_record(&type, buf, sizeof(buf), &n, 200, 0) != 0)
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
            uint32_t hslen = tls_rd24(hs_reasm + off + 1);
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
                cert_verified = tls_verify_cert_chain(hs_reasm + off, 4 + hslen, sni_host);
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
    tls_wr24(cke + 1, 33);
    cke[4] = 32;
    memcpy(cke + 5, pub, 32);
    transcript_add(cke, 37);
    if (tls_send_record(TLS_CONTENT_HS, cke, 37, 0) != 0) {
        tls_set_err("ClientKeyExchange send failed");
        goto fail;
    }

    derive_keys(premaster, cs);

    uint8_t ccs = 1;
    if (tls_send_record(TLS_CONTENT_CCS, &ccs, 1, 0) != 0) {
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
                if (tls_recv_record(&type, buf, sizeof(buf), &n, 100, 0) != 0)
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
