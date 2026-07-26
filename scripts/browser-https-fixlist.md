# Browser HTTPS fixlist (QEMU-proven)

Artifacts: `/tmp/peak-https-diag4/` (CLI matrix), `/tmp/peak-https-accept/` (Accept/Forget),
`/tmp/peak-https-accept/expired.txt` (honesty). Harness: `scripts/browser-https-diag.py`,
`scripts/browser-https-accept-prove.py`, `scripts/browser-https-gui-matrix.py`.

## Matrix (user-net, `-machine pc`, virtio-rng, `-rtc base=2026-07-26T17:00:00`)

| ID | URL / case | Expected | Actual | Proof | Status |
|----|------------|----------|--------|-------|--------|
| B-HTTPS-00 | `wget` `%.7s` method | `GET url` | snprintf lacked `.` precision → method reject | Fixed in `kernel/util.c` | **Fixed** |
| B-CLI-01 | `date` / RTC | usable wall time | `2026-07-26 17:00:04` | diag4 | OK |
| B-CLI-02 | DNS `example.com` | A record | via `10.0.2.3` | diag | OK |
| B-CLI-03 | `http://example.com/` | HTTP 200 | 200, 559 bytes | diag4 | OK |
| B-HTTPS-M1 | `https://example.com/` | WebPKI OK | HTTP/2 200 (trust path) | diag4 | **Fixed** |
| B-HTTPS-M2 | `https://www.cloudflare.com/` | WebPKI OK | HTTP/2 200 | diag4 | **Fixed** |
| B-HTTPS-M3 | `https://peakevergreen.com/` | WebPKI OK | HTTP/2 200 | diag4 | **Fixed** |
| B-HTTPS-M4 | `https://www.google.com/` | WebPKI OK | HTTP/2 200 | diag4 | **Fixed** |
| B-HTTPS-M5 | `https://letsencrypt.org/` | WebPKI OK | HTTP/2 200 | diag4 | **Fixed** |
| B-HTTPS-M6 | `https://github.com/` | WebPKI OK | HTTP/2 200 | diag4 | **Fixed** |
| B-HTTPS-01 | Fail-reason honesty | expired ≠ generic WebPKI | `fetch: tls-expired` on expired.badssl | expired.txt | **Fixed** |
| B-HTTPS-02 | RTC | usable for validity | OK with `-rtc` / CMOS | diag | OK |
| B-HTTPS-03/04 | WebPKI path | cross-signed roots + ECDSA | SPKI anchor match + BIT STRING EC point parse | webpki.c | **Fixed** |
| B-HTTPS-05 | Accept / Forget TOFU | Accept → trust; Forget → fail | `tlsinfo -A`/`-F` + error-page buttons | accept prove PASS | **Fixed** |
| B-HTTPS-06 | Desktop matrix | tabs/Retry/lock/mixed/HSTS | CLI + code paths; GUI dumps optional | see notes | **Partial** |
| B-HTTPS-H2 | HTTP/2 response body | non-empty HTML | Often `HTTP/2 200` with `0 bytes` (HEADERS without DATA) | diag4 | **Deferred** |

## Root causes fixed

1. **Cross-signed trust anchors** — server chains ship roots with the same SPKI as embedded DERs but different issuer bytes; match anchors by SPKI, not only full DER hash.
2. **ECDSA SPKI probe** — scanning for `0x61 0x00 0x04` inside key material false-triggered P-384 and aborted P-256 verify; parse BIT STRING properly.
3. **TLS 1.2 cert fail-closed** — continue-to-SKE after WebPKI fail masked `tls-untrusted` with SKE errors; fail immediately on cert reject.
4. **Error-page TOFU** — Accept writes `/etc/peak/tls-tofu` for the last failed digest; store consulted even when global TOFU is off. Forget removes the host. CLI: `tlsinfo -A` / `tlsinfo -F [host]`.
5. **snprintf `%.Ns`** — freestanding snprintf now supports precision (unblocks wget method).

## Deferred

- **HTTP/2 empty bodies** on many public sites after successful WebPKI (follow-up: DATA/window handling). Trust/verify path is green; page HTML may be empty until H2 body path is fixed.
- Full mouse-driven GUI matrix dumps (relative PS/2 fragile); CLI covers trust/Accept/expired.
