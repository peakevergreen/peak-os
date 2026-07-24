#include "tls_internal.h"
#include "tls_session.h"
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

static int is_aes256_gcm(uint16_t cs) {
    return cs == CS_ECDHE_RSA_AES256_GCM || cs == CS_ECDHE_ECDSA_AES256_GCM;
}

static int is_sha384_suite(uint16_t cs) {
    return is_aes256_gcm(cs);
}

static void transcript_add(const uint8_t *hs_msg, size_t len) {
    sha256_ctx_update(&transcript, hs_msg, len);
    sha384_ctx_update(&transcript384, hs_msg, len);
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
    int is13_suite = (*cs_out == CS_TLS13_AES128_GCM || *cs_out == CS_TLS13_AES256_GCM ||
                      *cs_out == CS_TLS13_CHACHA20);
    if (!is13_suite && !is_aes128_gcm(*cs_out) && !is_aes256_gcm(*cs_out) && !is_chacha(*cs_out))
        return -1;

    use_ems = 0;
    tls13 = 0;
    int saw_version = 0;
    int saw_share = 0;
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
            if (et == 0x002b && el >= 2) {
                uint16_t ver = tls_rd16(p);
                if (ver == 0x0304) {
                    tls13 = 1;
                    saw_version = 1;
                }
            }
            if (et == 0x0033 && el >= 4) {
                uint16_t group = tls_rd16(p);
                uint16_t klen = tls_rd16(p + 2);
                if (group == 0x001d && klen == 32 && el >= 4 + 32) {
                    memcpy(tls13_server_pub, p + 4, 32);
                    saw_share = 1;
                }
            }
            p += el;
        }
    }
    if (tls13 && (!saw_version || !saw_share || !is13_suite))
        return -1;
    if (!tls13 && is13_suite)
        return -1;
    return 0;
}

/* Leaf DER saved from Certificate (for SKE signature verify). */
static uint8_t leaf_der[3072];
static size_t leaf_der_len;

/* Extract first uncompressed P-256 pubkey (0x04||X||Y) from DER leaf. */
static int leaf_p256_pub(const uint8_t *leaf, size_t leaf_len, uint8_t pub_xy[64]) {
    if (!leaf || leaf_len < 65)
        return -1;
    for (size_t i = 0; i + 65 <= leaf_len; i++) {
        if (leaf[i] != 0x04)
            continue;
        /* Prefer BIT STRING length 66 (0x00 || 0x04 || 64) pattern one byte back. */
        if (i >= 2 && leaf[i - 1] == 0x00 && leaf[i - 2] == 0x42) {
            memcpy(pub_xy, leaf + i + 1, 64);
            return 0;
        }
    }
    /* Fallback: first 0x04||64 with non-zero coordinates. */
    for (size_t i = 0; i + 65 <= leaf_len; i++) {
        if (leaf[i] != 0x04)
            continue;
        memcpy(pub_xy, leaf + i + 1, 64);
        int nz = 0;
        for (int j = 0; j < 64; j++)
            nz |= pub_xy[j];
        if (nz)
            return 0;
    }
    return -1;
}

static int der_ecdsa_sig_to_raw(const uint8_t *der, size_t der_len, uint8_t raw[64]) {
    /* SEQUENCE { INTEGER r, INTEGER s } */
    if (der_len < 8 || der[0] != 0x30)
        return -1;
    size_t off = 2;
    if (der[1] & 0x80)
        return -1; /* long form not needed for P-256 */
    if (der[1] + 2u > der_len)
        return -1;
    memset(raw, 0, 64);
    for (int part = 0; part < 2; part++) {
        if (off >= der_len || der[off++] != 0x02)
            return -1;
        if (off >= der_len)
            return -1;
        uint8_t ilen = der[off++];
        if (off + ilen > der_len || ilen == 0 || ilen > 33)
            return -1;
        const uint8_t *ip = der + off;
        size_t skip = 0;
        while (skip < ilen && ip[skip] == 0)
            skip++;
        size_t n = ilen - skip;
        if (n > 32)
            return -1;
        memcpy(raw + part * 32 + (32 - n), ip + skip, n);
        off += ilen;
    }
    return 0;
}

static int verify_ske_ecdsa(const uint8_t *params, size_t params_len,
                            const uint8_t *sig_der, size_t sig_len,
                            const uint8_t *leaf, size_t leaf_len) {
    uint8_t pub[64], raw[64], hash[32], signed_data[32 + 32 + 256];
    if (params_len > 256)
        return -1;
    if (leaf_p256_pub(leaf, leaf_len, pub) != 0)
        return -1;
    if (der_ecdsa_sig_to_raw(sig_der, sig_len, raw) != 0)
        return -1;
    memcpy(signed_data, client_random, 32);
    memcpy(signed_data + 32, server_random, 32);
    memcpy(signed_data + 64, params, params_len);
    sha256(signed_data, 64 + params_len, hash);
    if (p256_ecdsa_verify(raw, pub, hash, 32) != 0)
        return -1;
    return 0;
}

static int save_leaf_from_cert(const uint8_t *cert_msg, size_t len) {
    leaf_der_len = 0;
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
    if (off + cert_len > len || cert_len > sizeof(leaf_der))
        return -1;
    memcpy(leaf_der, cert_msg + off, cert_len);
    leaf_der_len = cert_len;
    return 0;
}

static int parse_server_key_exchange(const uint8_t *msg, size_t len, uint16_t *curve_out,
                                     uint8_t *peer_pub, size_t peer_cap, size_t *peer_len) {
    if (len < 4 || msg[0] != HS_SERVER_KEY_EX)
        return -1;
    uint32_t mlen = tls_rd24(msg + 1);
    if (mlen + 4 > len)
        return -1;
    const uint8_t *params = msg + 4;
    const uint8_t *p = params;
    const uint8_t *end = msg + 4 + mlen;
    if (p + 4 > end)
        return -1;
    if (p[0] != 3)
        return -1;
    uint16_t curve = tls_rd16(p + 1);
    p += 3;
    uint8_t plen = *p++;
    if (curve == 0x001d) {
        if (plen != 32 || p + 32 > end || peer_cap < 32)
            return -1;
        memcpy(peer_pub, p, 32);
        *peer_len = 32;
        p += 32;
    } else if (curve == 0x0017) {
        if (plen != 65 || p + 65 > end || peer_cap < 65 || p[0] != 0x04)
            return -1;
        memcpy(peer_pub, p, 65);
        *peer_len = 65;
        p += 65;
    } else {
        return -1;
    }
    size_t params_len = (size_t)(p - params);
    if (p + 4 > end)
        return -1;
    uint8_t hash_alg = *p++;
    uint8_t sig_alg = *p++;
    uint16_t sig_len = tls_rd16(p);
    p += 2;
    if (p + sig_len > end || sig_len == 0)
        return -1;
    const uint8_t *sig = p;

    if (hash_alg == 4 && sig_alg == 3) {
        /* ecdsa_secp256r1_sha256 */
        if (verify_ske_ecdsa(params, params_len, sig, sig_len, leaf_der, leaf_der_len) != 0)
            return -1;
    } else if ((hash_alg == 8 && sig_alg == 4) || (hash_alg == 4 && sig_alg == 1)) {
        /* rsa_pss_rsae_sha256 (0x0804) or rsa_pkcs1_sha256 (0x0401) */
        uint8_t signed_data[32 + 32 + 256];
        uint8_t digest[32];
        if (params_len > 256 || leaf_der_len == 0)
            return -1;
        memcpy(signed_data, client_random, 32);
        memcpy(signed_data + 32, server_random, 32);
        memcpy(signed_data + 64, params, params_len);
        sha256(signed_data, 64 + params_len, digest);
        int pss = (hash_alg == 8 && sig_alg == 4);
        if (rsa_verify_sha256(leaf_der, leaf_der_len, digest, 32, sig, sig_len, pss) != 0)
            return -1;
    } else {
        return -1; /* unsupported SKE signature scheme */
    }
    *curve_out = curve;
    return 0;
}

static void derive_keys(const uint8_t premaster[32], uint16_t cs) {
    int sha384 = is_sha384_suite(cs);
    if (use_ems) {
        if (sha384) {
            uint8_t session_hash[48];
            struct sha384_ctx tmp = transcript384;
            sha384_ctx_final(&tmp, session_hash);
            tls_prf_sha384(premaster, 32, "extended master secret",
                           session_hash, 48, master_secret, 48);
        } else {
            uint8_t session_hash[32];
            struct sha256_ctx tmp = transcript;
            sha256_ctx_final(&tmp, session_hash);
            tls_prf_sha256(premaster, 32, "extended master secret",
                           session_hash, 32, master_secret, 48);
        }
    } else {
        uint8_t seed[64];
        memcpy(seed, client_random, 32);
        memcpy(seed + 32, server_random, 32);
        if (sha384)
            tls_prf_sha384(premaster, 32, "master secret", seed, 64, master_secret, 48);
        else
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
    } else if (is_aes256_gcm(cs)) {
        cipher_kind = CIPHER_AES256_GCM;
        uint8_t key_block[72];
        tls_prf_sha384(master_secret, 48, "key expansion", seed2, 64, key_block, 72);
        memcpy(client_key, key_block, 32);
        memcpy(server_key, key_block + 32, 32);
        memcpy(client_iv, key_block + 64, 4);
        memcpy(server_iv, key_block + 68, 4);
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
    uint8_t verify[12];
    uint8_t msg[16];
    if (cipher_kind == CIPHER_AES256_GCM) {
        uint8_t hash[48];
        struct sha384_ctx tmp = transcript384;
        sha384_ctx_final(&tmp, hash);
        tls_prf_sha384(master_secret, 48, "client finished", hash, 48, verify, 12);
    } else {
        uint8_t hash[32];
        struct sha256_ctx tmp = transcript;
        sha256_ctx_final(&tmp, hash);
        tls_prf_sha256(master_secret, 48, "client finished", hash, 32, verify, 12);
    }
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
    uint8_t expect[12];
    if (cipher_kind == CIPHER_AES256_GCM) {
        uint8_t hash[48];
        struct sha384_ctx tmp = transcript384;
        sha384_ctx_final(&tmp, hash);
        tls_prf_sha384(master_secret, 48, "server finished", hash, 48, expect, 12);
    } else {
        uint8_t hash[32];
        struct sha256_ctx tmp = transcript;
        sha256_ctx_final(&tmp, hash);
        tls_prf_sha256(master_secret, 48, "server finished", hash, 32, expect, 12);
    }
    if (!crypto_memeq(expect, buf + 4, 12))
        return -1;
    transcript_add(buf, n);
    return 0;
}

int tls_connect(uint32_t ip, uint16_t port, const char *sni_host, uint32_t timeout_ticks) {
    net_attempt_stats_note_tls();
    tls_close();
    last_err[0] = '\0';
    last_err_code = TLS_E_OK;
    use_ems = 0;
    tls13 = 0;
    cert_verified = 0;
    hostname_matched = 0;
    hostname_parse_skipped = 0;
    if (net_tcp_connect(ip, port, timeout_ticks) != 0) {
        tls_set_err_code(TLS_E_TCP, "TCP connect failed");
        return -1;
    }

    sha256_ctx_init(&transcript);
    sha384_ctx_init(&transcript384);
    client_seq = server_seq = 0;
    rx_app_len = 0;
    hs_reasm_len = 0;
    cipher_kind = CIPHER_AES128_GCM;

    uint8_t ch[768];
    size_t ch_len = 0;
    {
        int ch_rc = tls_build_client_hello(ch, sizeof(ch), sni_host, &ch_len);
        if (ch_rc == -1) {
            tls_set_err_code(TLS_E_RNG, "RNG not ready (crypto domain)");
            goto fail;
        }
        if (ch_rc != 0) {
            tls_set_err_code(TLS_E_BUFFER, "ClientHello too large");
            goto fail;
        }
    }
    transcript_add(ch, ch_len);
    if (tls_send_record(TLS_CONTENT_HS, ch, ch_len, 0) != 0) {
        tls_set_err_code(TLS_E_HANDSHAKE, "ClientHello send failed");
        goto fail;
    }

    uint8_t peer_pub[65];
    size_t peer_pub_len = 0;
    uint16_t peer_curve = 0;
    int got_sh = 0, got_ske = 0, got_done = 0, got_cert = 0;
    uint16_t cs = 0;
    uint64_t start = timer_ticks();
    uint32_t hs_records = 0;

    while (!got_done && timer_ticks() - start < timeout_ticks) {
        uint8_t type;
        size_t n = 0;
        size_t room = sizeof(hs_reasm) - hs_reasm_len;
        if (room == 0) {
            tls_set_err_code(TLS_E_DOS, "Handshake buffer overflow");
            goto fail;
        }
        /* Stage HS bytes directly into hs_reasm (avoids a second 16 KiB BSS buf). */
        if (tls_recv_record(&type, hs_reasm + hs_reasm_len, room, &n, 200, 0) != 0)
            continue;
        if (type == TLS_CONTENT_ALERT) {
            tls_set_err_code(TLS_E_ALERT, "Server alert during handshake");
            goto fail;
        }
        if (type != TLS_CONTENT_HS)
            continue;
        if (++hs_records > TLS_HS_RECORD_MAX) {
            tls_set_err_code(TLS_E_DOS, "Handshake record budget exceeded");
            goto fail;
        }

        hs_reasm_len += n;

        size_t off = 0;
        while (off + 4 <= hs_reasm_len) {
            uint8_t hstype = hs_reasm[off];
            uint32_t hslen = tls_rd24(hs_reasm + off + 1);
            if (hslen > TLS_HS_MSG_MAX) {
                tls_set_err_code(TLS_E_DOS, "Handshake message too large");
                goto fail;
            }
            if (off + 4 + hslen > hs_reasm_len)
                break;
            transcript_add(hs_reasm + off, 4 + hslen);

            if (hstype == HS_SERVER_HELLO) {
                if (parse_server_hello(hs_reasm + off, 4 + hslen, &cs) != 0) {
                    tls_set_err_code(TLS_E_HANDSHAKE, "ServerHello parse/cipher rejected");
                    goto fail;
                }
                got_sh = 1;
                if (tls13) {
                    /* Consume remaining buffered cleartext (should be none) and
                     * continue under TLS 1.3. Transcript already includes SH. */
                    off += 4 + hslen;
                    if (off > 0) {
                        memmove(hs_reasm, hs_reasm + off, hs_reasm_len - off);
                        hs_reasm_len -= off;
                    }
                    if (tls13_handshake_after_sh(cs, sni_host, timeout_ticks) != 0)
                        goto fail;
                    return 0;
                }
            } else if (hstype == HS_CERTIFICATE) {
                got_cert = 1;
                if (save_leaf_from_cert(hs_reasm + off, 4 + hslen) != 0) {
                    tls_set_err_code(TLS_E_HANDSHAKE, "Certificate leaf extract failed");
                    goto fail;
                }
                cert_verified = tls_verify_cert_chain(hs_reasm + off, 4 + hslen, sni_host);
            } else if (hstype == HS_SERVER_KEY_EX) {
                if (parse_server_key_exchange(hs_reasm + off, 4 + hslen, &peer_curve,
                                              peer_pub, sizeof(peer_pub),
                                              &peer_pub_len) != 0) {
                    tls_set_err_code(TLS_E_VERIFY, "ServerKeyExchange signature/parse failed");
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
        tls_set_err_code(TLS_E_TIMEOUT, "Timeout waiting for ServerHelloDone");
        goto fail;
    }
    /* Fail closed: missing Certificate or TOFU/pin mismatch → refuse session. */
    if (!got_cert || !cert_verified) {
        tls_set_err_code(TLS_E_CERT, cert_fail_reason ? cert_fail_reason
                                     : "TLS certificate unverified");
        goto fail;
    }

    uint8_t priv[32], premaster[32];
    if (crypto_random(priv, 32) != 0) {
        tls_set_err_code(TLS_E_RNG, "RNG not ready for key generation");
        goto fail;
    }

    uint8_t cke[80];
    size_t cke_len = 0;
    if (peer_curve == 0x0017) {
        uint8_t pub[65];
        if (p256_keygen(priv, pub) != 0) {
            tls_set_err_code(TLS_E_HANDSHAKE, "P-256 keygen failed");
            goto fail;
        }
        if (p256_ecdh(premaster, priv, peer_pub) != 0) {
            tls_set_err_code(TLS_E_HANDSHAKE, "P-256 ECDH failed");
            goto fail;
        }
        cke[0] = HS_CLIENT_KEY_EX;
        tls_wr24(cke + 1, 66);
        cke[4] = 65;
        memcpy(cke + 5, pub, 65);
        cke_len = 70;
    } else {
        uint8_t pub[32];
        x25519_base(pub, priv);
        x25519(premaster, priv, peer_pub);
        cke[0] = HS_CLIENT_KEY_EX;
        tls_wr24(cke + 1, 33);
        cke[4] = 32;
        memcpy(cke + 5, pub, 32);
        cke_len = 37;
    }
    transcript_add(cke, cke_len);
    if (tls_send_record(TLS_CONTENT_HS, cke, cke_len, 0) != 0) {
        tls_set_err_code(TLS_E_HANDSHAKE, "ClientKeyExchange send failed");
        goto fail;
    }

    derive_keys(premaster, cs);

    uint8_t ccs = 1;
    if (tls_send_record(TLS_CONTENT_CCS, &ccs, 1, 0) != 0) {
        tls_set_err_code(TLS_E_HANDSHAKE, "CCS send failed");
        goto fail;
    }

    if (send_finished() != 0) {
        tls_set_err_code(TLS_E_HANDSHAKE, "Finished send failed");
        goto fail;
    }

    {
        uint8_t type;
        size_t n = 0;
        start = timer_ticks();
        int got_ccs = 0;
        while (timer_ticks() - start < timeout_ticks) {
            if (!got_ccs) {
                if (tls_recv_record(&type, hs_reasm, sizeof(hs_reasm), &n, 100, 0) != 0)
                    continue;
                if (type == TLS_CONTENT_ALERT) {
                    tls_set_err_code(TLS_E_ALERT, "Server alert after Finished (bad keys?)");
                    goto fail;
                }
                if (type == TLS_CONTENT_HS) {
                    /* NewSessionTicket (type 4): lifetime(4) + ticket. */
                    if (n >= 10 && hs_reasm[0] == 4) {
                        uint32_t hslen = tls_rd24(hs_reasm + 1);
                        if (4 + hslen <= n && hslen >= 6) {
                            uint16_t tlen = tls_rd16(hs_reasm + 8);
                            if (10 + tlen <= 4 + hslen && tlen > 0 &&
                                tlen <= TLS_SESSION_TICKET_MAX) {
                                struct tls_session_meta meta = {.cipher = cs, .tls13 = 0};
                                tls_session_put(sni_host, hs_reasm + 10, tlen, &meta);
                            }
                        }
                    }
                    continue;
                }
                if (type == TLS_CONTENT_CCS) {
                    got_ccs = 1;
                    continue;
                }
            } else {
                if (expect_finished(timeout_ticks) != 0) {
                    tls_set_err_code(TLS_E_VERIFY, "Server Finished verify failed");
                    goto fail;
                }
                tls_up = 1;
                serial_log(SERIAL_LOG_INFO,
                           cipher_kind == CIPHER_CHACHA20
                               ? (use_ems ? "tls: ok chacha20+ems\n" : "tls: ok chacha20\n")
                               : (use_ems ? "tls: ok aes128-gcm+ems\n" : "tls: ok aes128-gcm\n"));
                return 0;
            }
        }
        tls_set_err_code(TLS_E_TIMEOUT, "Timeout waiting for server CCS/Finished");
    }

fail:
    net_tcp_close();
    tls_up = 0;
    tls_scrub_secrets();
    return -1;
}
