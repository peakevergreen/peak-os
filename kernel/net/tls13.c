/*
 * TLS 1.3 client handshake (RFC 8446 subset): after ServerHello with
 * selected_version 0x0304, derive handshake keys, verify CertVerify + Finished,
 * send client Finished, install application traffic keys.
 */
#include "tls_internal.h"
#include "random.h"
#include "serial.h"
#include "timer.h"
#include "util.h"

static void transcript_hash(uint8_t *out) {
    if (tls13_sha384) {
        struct sha384_ctx tmp = transcript384;
        sha384_ctx_final(&tmp, out);
    } else {
        struct sha256_ctx tmp = transcript;
        sha256_ctx_final(&tmp, out);
    }
}

static void transcript_add(const uint8_t *msg, size_t len) {
    sha256_ctx_update(&transcript, msg, len);
    sha384_ctx_update(&transcript384, msg, len);
}

static int traffic_keys_from_secret(const uint8_t *secret, size_t secret_len, uint8_t *key_out,
                                    size_t key_len, uint8_t iv_out[12]) {
    if (tls13_hkdf_expand_label(tls13_sha384, secret, secret_len, "key", NULL, 0, key_out,
                                key_len) != 0)
        return -1;
    if (tls13_hkdf_expand_label(tls13_sha384, secret, secret_len, "iv", NULL, 0, iv_out, 12) != 0)
        return -1;
    return 0;
}

static int install_hs_keys(void) {
    size_t key_len = (cipher_kind == CIPHER_AES128_GCM) ? 16 : 32;
    if (traffic_keys_from_secret(tls13_client_hs_traffic, tls13_hash_len, client_key, key_len,
                                 client_iv) != 0)
        return -1;
    if (traffic_keys_from_secret(tls13_server_hs_traffic, tls13_hash_len, server_key, key_len,
                                 server_iv) != 0)
        return -1;
    client_seq = server_seq = 0;
    return 0;
}

static int install_app_keys(void) {
    size_t key_len = (cipher_kind == CIPHER_AES128_GCM) ? 16 : 32;
    if (traffic_keys_from_secret(tls13_client_app_traffic, tls13_hash_len, client_key, key_len,
                                 client_iv) != 0)
        return -1;
    if (traffic_keys_from_secret(tls13_server_app_traffic, tls13_hash_len, server_key, key_len,
                                 server_iv) != 0)
        return -1;
    client_seq = server_seq = 0;
    return 0;
}

static int derive_handshake_secrets(const uint8_t *shared, size_t shared_len) {
    uint8_t empty_hash[48];
    uint8_t zeros[48];
    memset(zeros, 0, sizeof(zeros));
    if (tls13_sha384) {
        sha384((const uint8_t *)"", 0, empty_hash);
    } else {
        sha256((const uint8_t *)"", 0, empty_hash);
    }
    /* EarlySecret = HKDF-Extract(0, 0) */
    if (tls13_sha384)
        hkdf_extract_sha384(zeros, 48, zeros, 48, tls13_early_secret);
    else
        hkdf_extract_sha256(zeros, 32, zeros, 32, tls13_early_secret);

    uint8_t derived[48];
    if (tls13_derive_secret(tls13_sha384, tls13_early_secret, tls13_hash_len, "derived",
                            empty_hash, tls13_hash_len, derived, tls13_hash_len) != 0)
        return -1;
    if (tls13_sha384)
        hkdf_extract_sha384(derived, 48, shared, shared_len, tls13_handshake_secret);
    else
        hkdf_extract_sha256(derived, 32, shared, shared_len, tls13_handshake_secret);

    uint8_t th[48];
    transcript_hash(th);
    if (tls13_derive_secret(tls13_sha384, tls13_handshake_secret, tls13_hash_len,
                            "c hs traffic", th, tls13_hash_len, tls13_client_hs_traffic,
                            tls13_hash_len) != 0)
        return -1;
    if (tls13_derive_secret(tls13_sha384, tls13_handshake_secret, tls13_hash_len,
                            "s hs traffic", th, tls13_hash_len, tls13_server_hs_traffic,
                            tls13_hash_len) != 0)
        return -1;
    return 0;
}

static int derive_app_secrets(void) {
    uint8_t empty_hash[48];
    uint8_t zeros[48];
    memset(zeros, 0, sizeof(zeros));
    if (tls13_sha384)
        sha384((const uint8_t *)"", 0, empty_hash);
    else
        sha256((const uint8_t *)"", 0, empty_hash);

    uint8_t derived[48];
    if (tls13_derive_secret(tls13_sha384, tls13_handshake_secret, tls13_hash_len, "derived",
                            empty_hash, tls13_hash_len, derived, tls13_hash_len) != 0)
        return -1;
    if (tls13_sha384)
        hkdf_extract_sha384(derived, 48, zeros, 48, tls13_master_secret);
    else
        hkdf_extract_sha256(derived, 32, zeros, 32, tls13_master_secret);

    uint8_t th[48];
    transcript_hash(th);
    if (tls13_derive_secret(tls13_sha384, tls13_master_secret, tls13_hash_len, "c ap traffic", th,
                            tls13_hash_len, tls13_client_app_traffic, tls13_hash_len) != 0)
        return -1;
    if (tls13_derive_secret(tls13_sha384, tls13_master_secret, tls13_hash_len, "s ap traffic", th,
                            tls13_hash_len, tls13_server_app_traffic, tls13_hash_len) != 0)
        return -1;
    return 0;
}

static int leaf_p256_pub(const uint8_t *leaf, size_t leaf_len, uint8_t pub_xy[64]) {
    if (!leaf || leaf_len < 65)
        return -1;
    for (size_t i = 0; i + 65 <= leaf_len; i++) {
        if (leaf[i] != 0x04)
            continue;
        if (i >= 2 && leaf[i - 1] == 0x00 && leaf[i - 2] == 0x42) {
            memcpy(pub_xy, leaf + i + 1, 64);
            return 0;
        }
    }
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
    if (der_len < 8 || der[0] != 0x30 || (der[1] & 0x80))
        return -1;
    size_t off = 2;
    memset(raw, 0, 64);
    for (int part = 0; part < 2; part++) {
        if (off >= der_len || der[off++] != 0x02 || off >= der_len)
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

static int save_leaf_tls13(const uint8_t *msg, size_t len, uint8_t *leaf, size_t *leaf_len,
                           size_t leaf_cap) {
    /* Certificate: request_context, certificate_list */
    if (len < 8 || msg[0] != HS_CERTIFICATE)
        return -1;
    uint32_t mlen = tls_rd24(msg + 1);
    if (mlen + 4 > len)
        return -1;
    const uint8_t *p = msg + 4;
    const uint8_t *end = msg + 4 + mlen;
    if (p >= end)
        return -1;
    uint8_t ctx_len = *p++;
    if (p + ctx_len + 3 > end)
        return -1;
    p += ctx_len;
    uint32_t list_len = tls_rd24(p);
    p += 3;
    if (p + list_len > end || list_len < 3)
        return -1;
    uint32_t cert_len = tls_rd24(p);
    p += 3;
    if (p + cert_len + 2 > end || cert_len > leaf_cap)
        return -1;
    memcpy(leaf, p, cert_len);
    *leaf_len = cert_len;
    return 0;
}

static int verify_cert_verify(const uint8_t *msg, size_t len, const uint8_t *leaf, size_t leaf_len) {
    if (len < 8 || msg[0] != HS_CERT_VERIFY)
        return -1;
    uint32_t mlen = tls_rd24(msg + 1);
    if (mlen + 4 > len || mlen < 4)
        return -1;
    const uint8_t *p = msg + 4;
    uint16_t scheme = tls_rd16(p);
    p += 2;
    uint16_t sig_len = tls_rd16(p);
    p += 2;
    if (4 + sig_len != mlen)
        return -1;

    uint8_t th[48];
    transcript_hash(th);

    /* context = 64 * 0x20 || "TLS 1.3, server CertificateVerify" || 0x00 || transcript_hash */
    uint8_t signed_content[64 + 33 + 1 + 48];
    size_t sc = 0;
    memset(signed_content, 0x20, 64);
    sc = 64;
    static const char ctx[] = "TLS 1.3, server CertificateVerify";
    memcpy(signed_content + sc, ctx, 33);
    sc += 33;
    signed_content[sc++] = 0;
    memcpy(signed_content + sc, th, tls13_hash_len);
    sc += tls13_hash_len;

    uint8_t digest[48];
    if (scheme == 0x0403) {
        /* ecdsa_secp256r1_sha256 — hash is SHA-256 of signed_content */
        sha256(signed_content, sc, digest);
        uint8_t pub[64], raw[64];
        if (leaf_p256_pub(leaf, leaf_len, pub) != 0)
            return -1;
        if (der_ecdsa_sig_to_raw(p, sig_len, raw) != 0)
            return -1;
        return p256_ecdsa_verify(raw, pub, digest, 32) == 0 ? 0 : -1;
    }
    if (scheme == 0x0804 || scheme == 0x0805) {
        /* rsa_pss_rsae_sha256 / sha384 */
        if (scheme == 0x0805) {
            sha384(signed_content, sc, digest);
            /* rsa_verify_sha256 only does SHA-256 digest length — for sha384 use
             * truncated verify via hashing to 32 is wrong. Require sha256 PSS. */
            return -1;
        }
        sha256(signed_content, sc, digest);
        return rsa_verify_sha256(leaf, leaf_len, digest, 32, p, sig_len, 1) == 0 ? 0 : -1;
    }
    return -1;
}

static int verify_finished(const uint8_t *msg, size_t len, const uint8_t *base_key) {
    if (len < 4 || msg[0] != HS_FINISHED)
        return -1;
    uint32_t mlen = tls_rd24(msg + 1);
    if (mlen + 4 > len || mlen != tls13_hash_len)
        return -1;
    uint8_t finished_key[48];
    if (tls13_hkdf_expand_label(tls13_sha384, base_key, tls13_hash_len, "finished", NULL, 0,
                                finished_key, tls13_hash_len) != 0)
        return -1;
    uint8_t th[48];
    transcript_hash(th);
    uint8_t expect[48];
    if (tls13_sha384)
        hmac_sha384(finished_key, tls13_hash_len, th, tls13_hash_len, expect);
    else
        hmac_sha256(finished_key, tls13_hash_len, th, tls13_hash_len, expect);
    int ok = crypto_memeq(expect, msg + 4, tls13_hash_len);
    memzero_explicit(finished_key, sizeof(finished_key));
    return ok ? 0 : -1;
}

static int send_client_finished(void) {
    uint8_t finished_key[48];
    if (tls13_hkdf_expand_label(tls13_sha384, tls13_client_hs_traffic, tls13_hash_len, "finished",
                                NULL, 0, finished_key, tls13_hash_len) != 0)
        return -1;
    uint8_t th[48];
    transcript_hash(th);
    uint8_t verify[48];
    if (tls13_sha384)
        hmac_sha384(finished_key, tls13_hash_len, th, tls13_hash_len, verify);
    else
        hmac_sha256(finished_key, tls13_hash_len, th, tls13_hash_len, verify);
    uint8_t msg[4 + 48];
    msg[0] = HS_FINISHED;
    tls_wr24(msg + 1, (uint32_t)tls13_hash_len);
    memcpy(msg + 4, verify, tls13_hash_len);
    size_t mlen = 4 + tls13_hash_len;
    transcript_add(msg, mlen);
    return tls_send_record(TLS_CONTENT_HS, msg, mlen, 1);
}

int tls13_handshake_after_sh(uint16_t cs, const char *sni_host, uint32_t timeout_ticks) {
    if (cs == CS_TLS13_AES128_GCM) {
        cipher_kind = CIPHER_AES128_GCM;
        tls13_sha384 = 0;
        tls13_hash_len = 32;
    } else if (cs == CS_TLS13_AES256_GCM) {
        cipher_kind = CIPHER_AES256_GCM;
        tls13_sha384 = 1;
        tls13_hash_len = 48;
    } else if (cs == CS_TLS13_CHACHA20) {
        cipher_kind = CIPHER_CHACHA20;
        tls13_sha384 = 0;
        tls13_hash_len = 32;
    } else {
        tls_set_err_code(TLS_E_HANDSHAKE, "TLS 1.3 cipher rejected");
        return -1;
    }

    uint8_t shared[32];
    x25519(shared, tls13_priv, tls13_server_pub);
    if (derive_handshake_secrets(shared, 32) != 0) {
        tls_set_err_code(TLS_E_HANDSHAKE, "TLS 1.3 handshake secret derive failed");
        return -1;
    }
    if (install_hs_keys() != 0) {
        tls_set_err_code(TLS_E_HANDSHAKE, "TLS 1.3 handshake key install failed");
        return -1;
    }

    uint8_t leaf[3072];
    size_t leaf_len = 0;
    int got_ee = 0, got_cert = 0, got_cv = 0, got_fin = 0;
    uint64_t start = timer_ticks();
    uint32_t hs_records = 0;
    hs_reasm_len = 0;

    while (!got_fin && timer_ticks() - start < timeout_ticks) {
        uint8_t type;
        size_t n = 0;
        size_t room = sizeof(hs_reasm) - hs_reasm_len;
        if (room == 0) {
            tls_set_err_code(TLS_E_DOS, "TLS 1.3 handshake buffer overflow");
            return -1;
        }
        if (tls_recv_record(&type, hs_reasm + hs_reasm_len, room, &n, 200, 1) != 0)
            continue;
        if (type == TLS_CONTENT_CCS)
            continue;
        if (type == TLS_CONTENT_ALERT) {
            tls_set_err_code(TLS_E_ALERT, "TLS 1.3 server alert");
            return -1;
        }
        if (type != TLS_CONTENT_HS)
            continue;
        if (++hs_records > TLS_HS_RECORD_MAX) {
            tls_set_err_code(TLS_E_DOS, "TLS 1.3 handshake record budget exceeded");
            return -1;
        }
        hs_reasm_len += n;

        size_t off = 0;
        while (off + 4 <= hs_reasm_len) {
            uint8_t hstype = hs_reasm[off];
            uint32_t hslen = tls_rd24(hs_reasm + off + 1);
            if (hslen > TLS_HS_MSG_MAX) {
                tls_set_err_code(TLS_E_DOS, "TLS 1.3 handshake message too large");
                return -1;
            }
            if (off + 4 + hslen > hs_reasm_len)
                break;
            const uint8_t *hs = hs_reasm + off;
            size_t hs_total = 4 + hslen;

            if (hstype == HS_ENCRYPTED_EXT) {
                got_ee = 1;
                if (hslen >= 2) {
                    uint16_t ext_total = tls_rd16(hs + 4);
                    const uint8_t *ep = hs + 6;
                    const uint8_t *eend = hs + 4 + hslen;
                    if (ep + ext_total <= eend) {
                        const uint8_t *ee = ep + ext_total;
                        while (ep + 4 <= ee) {
                            uint16_t et = tls_rd16(ep);
                            ep += 2;
                            uint16_t el = tls_rd16(ep);
                            ep += 2;
                            if (ep + el > ee)
                                break;
                            if (et == 0x0010)
                                tls_alpn_set_from_ext(ep, el);
                            ep += el;
                        }
                    }
                }
                transcript_add(hs, hs_total);
            } else if (hstype == HS_CERTIFICATE) {
                if (save_leaf_tls13(hs, hs_total, leaf, &leaf_len, sizeof(leaf)) != 0) {
                    tls_set_err_code(TLS_E_CERT, "TLS 1.3 Certificate parse failed");
                    return -1;
                }
                /* Build a synthetic 1.2-shaped Certificate message for TOFU digest. */
                uint8_t synth[4 + 3 + 3 + 3072];
                size_t body = 3 + 3 + leaf_len;
                synth[0] = HS_CERTIFICATE;
                tls_wr24(synth + 1, (uint32_t)body);
                tls_wr24(synth + 4, (uint32_t)(3 + leaf_len));
                tls_wr24(synth + 7, (uint32_t)leaf_len);
                memcpy(synth + 10, leaf, leaf_len);
                cert_verified = tls_verify_cert_chain(synth, 10 + leaf_len, sni_host);
                if (!cert_verified) {
                    tls_set_err_code(TLS_E_CERT, cert_fail_reason ? cert_fail_reason : "TLS 1.3 cert unverified");
                    return -1;
                }
                got_cert = 1;
                transcript_add(hs, hs_total);
            } else if (hstype == HS_CERT_VERIFY) {
                if (!got_cert) {
                    tls_set_err_code(TLS_E_HANDSHAKE, "TLS 1.3 CertificateVerify before Certificate");
                    return -1;
                }
                if (verify_cert_verify(hs, hs_total, leaf, leaf_len) != 0) {
                    tls_set_err_code(TLS_E_VERIFY, "TLS 1.3 CertificateVerify failed");
                    return -1;
                }
                got_cv = 1;
                transcript_add(hs, hs_total);
            } else if (hstype == HS_FINISHED) {
                if (!got_ee || !got_cert || !got_cv) {
                    tls_set_err_code(TLS_E_HANDSHAKE, "TLS 1.3 Finished before required messages");
                    return -1;
                }
                if (verify_finished(hs, hs_total, tls13_server_hs_traffic) != 0) {
                    tls_set_err_code(TLS_E_VERIFY, "TLS 1.3 server Finished verify failed");
                    return -1;
                }
                transcript_add(hs, hs_total);
                got_fin = 1;
            } else {
                /* Ignore NewSessionTicket etc. before Finished unexpectedly */
                transcript_add(hs, hs_total);
            }
            off += hs_total;
        }
        if (off > 0) {
            memmove(hs_reasm, hs_reasm + off, hs_reasm_len - off);
            hs_reasm_len -= off;
        }
    }
    if (!got_fin) {
        tls_set_err_code(TLS_E_TIMEOUT, "TLS 1.3 timeout waiting for Finished");
        return -1;
    }

    /* Optional middlebox CCS */
    uint8_t ccs = 1;
    (void)tls_send_record(TLS_CONTENT_CCS, &ccs, 1, 0);

    if (send_client_finished() != 0) {
        tls_set_err_code(TLS_E_HANDSHAKE, "TLS 1.3 client Finished send failed");
        return -1;
    }
    if (derive_app_secrets() != 0 || install_app_keys() != 0) {
        tls_set_err_code(TLS_E_HANDSHAKE, "TLS 1.3 app key derive failed");
        return -1;
    }

    tls_up = 1;
    serial_log(SERIAL_LOG_INFO,
               cipher_kind == CIPHER_CHACHA20
                   ? "tls: ok tls1.3 chacha20\n"
                   : (cipher_kind == CIPHER_AES256_GCM ? "tls: ok tls1.3 aes256-gcm\n"
                                                       : "tls: ok tls1.3 aes128-gcm\n"));
    return 0;
}
