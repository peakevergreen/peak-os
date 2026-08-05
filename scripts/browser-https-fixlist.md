# Browser HTTPS fixlist (QEMU campaign notes)

Artifacts: `/tmp/peak-https-diag4/` (CLI matrix), `/tmp/peak-https-accept/` (Accept/Forget),
`/tmp/peak-https-accept/expired.txt` (honesty). Harness: `scripts/browser-https-diag.py`,
`scripts/browser-https-accept-prove.py`, `scripts/browser-https-gui-matrix.py`.
Fresh recheck (2026-08-04): `scripts/browser-https-diag.py` + `scripts/browser-render-matrix.py`
— live HTTPS M1–M6 / render matrix **not** green (empty body / handshake stall); plain HTTP OK.

## Matrix (user-net, `-machine pc`, virtio-rng, `-rtc base=2026-08-04T17:00:00`)

| ID | URL / case | Expected | Actual | Proof | Status |
|----|------------|----------|--------|-------|--------|
| B-HTTPS-00 | `wget` `%.7s` method | `GET url` | snprintf lacked `.` precision → method reject | Fixed in `kernel/util.c` | **Fixed** |
| B-CLI-01 | `date` / RTC | usable wall time | `2026-08-04 17:00:04` | diag | OK |
| B-CLI-02 | DNS `example.com` | A record | via `10.0.2.3` | diag | OK |
| B-CLI-03 | `http://example.com/` | HTTP 200 | 200, 559 bytes | diag | OK |
| B-HTTPS-M1 | `https://example.com/` | WebPKI OK + body | TLS Finished timeout / empty (`frames=0`); trust-path code Fixed separately | diag 2026-08-04 | **Partial** |
| B-HTTPS-M2 | `https://www.cloudflare.com/` | WebPKI OK + body | empty / `fetch: tls-handshake` (`frames=0`) | diag 2026-08-04 | **Partial** |
| B-HTTPS-M3 | `https://peakevergreen.com/` | WebPKI OK + body | empty / `fetch: tls-handshake` (`frames=0`) | diag 2026-08-04 | **Partial** |
| B-HTTPS-M4 | `https://www.google.com/` | WebPKI OK + body | empty / `fetch: tls-handshake` (`frames=0`) | diag 2026-08-04 | **Partial** |
| B-HTTPS-M5 | `https://letsencrypt.org/` | WebPKI OK + body | empty / `fetch: tls-handshake` (`frames=0`) | diag 2026-08-04 | **Partial** |
| B-HTTPS-M6 | `https://github.com/` | WebPKI OK + body | empty / `fetch: tls-handshake` (`frames=0`) | diag 2026-08-04 | **Partial** |
| B-HTTPS-01 | Fail-reason honesty | expired ≠ generic WebPKI | `fetch: tls-expired` on expired.badssl | expired.txt | **Fixed** |
| B-HTTPS-02 | RTC | usable for validity | OK with `-rtc` / CMOS | diag | OK |
| B-HTTPS-03/04 | WebPKI path | cross-signed roots + ECDSA | SPKI anchor match + BIT STRING EC point parse | webpki.c | **Fixed** |
| B-HTTPS-05 | Accept / Forget TOFU | Accept → trust; Forget → fail | `tlsinfo -A`/`-F` + error-page buttons | accept prove PASS | **Fixed** |
| B-HTTPS-06 | Desktop matrix | tabs/Retry/lock/mixed/HSTS | CLI + code paths; GUI dumps optional | see notes | **Partial** |
| B-HTTPS-H2 | HTTP/2 response body | non-empty HTML | Huffman HPACK + no silent fake-200 on unseen `:status`; client SETTINGS helper (`ENABLE_PUSH=0`); identity AE; progress recv timeout; per-request H1 ALPN fallback (HTTP/1.1). Live QEMU user-net CDNs still often yield HEADERS-timeout / empty H1 appdata (Peak JA3) — matrix not green yet | `scripts/browser-render-matrix.py` + `/tmp/peak-h2-frames/` | **Partial** |

## Root causes fixed

1. **Cross-signed trust anchors** — server chains ship roots with the same SPKI as embedded DERs but different issuer bytes; match anchors by SPKI, not only full DER hash.
2. **ECDSA SPKI probe** — scanning for `0x61 0x00 0x04` inside key material false-triggered P-384 and aborted P-256 verify; parse BIT STRING properly.
3. **TLS 1.2 cert fail-closed** — continue-to-SKE after WebPKI fail masked `tls-untrusted` with SKE errors; fail immediately on cert reject.
4. **Error-page TOFU** — Accept writes `/etc/peak/tls-tofu` for the last failed digest; store consulted even when global TOFU is off. Forget removes the host. CLI: `tlsinfo -A` / `tlsinfo -F [host]`.
5. **snprintf `%.Ns`** — freestanding snprintf now supports precision (unblocks wget method).

## Deferred

- Full mouse-driven GUI matrix dumps (relative PS/2 fragile); CLI covers trust/Accept/expired.

## BR-1 (HTTP/2 body path)

- Accept frames up to `HTTP2_MAX_FRAME` (16384); store body up to `HTTP2_BODY_MAX` (65536) on the heap.
- ACK PING; WINDOW_UPDATE after DATA + connection preface window; HEADERS+CONTINUATION until END_HEADERS.
- Huffman HPACK + integer coding (`kernel/net/hpack.c`); do not invent `:status` 200 when headers decoded without it.
- Client SETTINGS helper encodes `ENABLE_PUSH=0`, `MAX_CONCURRENT_STREAMS=100`, `INITIAL_WINDOW_SIZE=256KiB`, `MAX_FRAME_SIZE=16384`. Preface still sends empty SETTINGS on the wire — non-empty SETTINGS + Peak JA3 makes several CDNs go silent (zero frames).
- Request headers: `:method` `:path` `:scheme` `:authority` + UA + `accept: */*` + `accept-encoding: identity`.
- Recv timeout from last frame progress; RST/GOAWAY / `h2-trace` on wget.
- Per-request HTTP/1.1 ALPN fallback when H2 body is empty (not 204/304/205); resume disabled for that reconnect; H2 result restored if H1 is empty. Peak TLS H1 app-data path is still often empty on the same hosts.
