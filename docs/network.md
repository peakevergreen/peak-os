# Peak networking

Peak has an **in-guest** IPv4 stack on QEMU `e1000`, with DHCP (fallback to
static), TCP client + **TCP listen**, HTTP/HTTPS client, and a TLS 1.2 client
written in Peak C (no OpenSSL / BearSSL / host fetch).

## Address configuration

Bootloaders parse [`boot/peak.conf`](../boot/peak.conf) into BootInfo:

| Key | Meaning |
|-----|---------|
| `net_mode` | `dhcp_fallback` (default), `static`, or `dhcp_only` |
| `net_ip` / `net_mask` / `net_gw` / `net_dns` | Static / fallback addresses |
| `dhcp_timeout_ticks` | DHCP wait (~100 Hz ticks; default `300`) |

`ifconfig` prints the active mode (`dhcp`, `static`, or `fallback`) with explicit
**default route** and **nameserver** lines (A-cache TTL ~30s).

Guest DNS / TCP helpers: `nslookup` / `host` (dig-style A record output via
`net_dns_resolve`), `traceroute` (staged local→gateway→DNS→destination TCP :80
probes; no ICMP TTL), `nc` (TCP connect, optional send + short recv), `tlsinfo`
(WebPKI root count, pin/TOFU mode, last TLS error code/name, optional `-r` root
SHA-256 list and `-m pattern host` hostname match probe).

**Desktop GUI:** Start menu → **Net Explorer** shows link/IP/DNS, runs ping (TCP :80 probe) and nslookup into a result pane. **Net Control** manages session net-allow, kill switch (double confirm), persist profile, DHCP renew, and RNG readiness; Settings → Network tab includes an **Open Net Control** shortcut.

## QEMU modes

### User-net (default — CI / smoke)

```bash
./scripts/run-qemu.sh
# or PEAK_NET_MODE=user ./scripts/run-qemu.sh
```

```text
-netdev user,id=net0
-device e1000,netdev=net0
```

Guest typically gets `10.0.2.15/24` via QEMU DHCP (or the configured fallback).
This address is **not** on your LAN.

### Bridged LAN (macOS vmnet) — other computers can connect

```bash
# List interfaces: networksetup -listallhardwareports
PEAK_NET_MODE=bridged PEAK_NET_IFACE=en0 ./scripts/run-qemu.sh
```

Requires QEMU with `vmnet-bridged` support and usually elevated permissions
(macOS may prompt). The guest should obtain a LAN DHCP lease; use `ifconfig`
inside Peak, then from another device:

```text
curl http://<guest-ip>:8080/
```

**Security:** the container HTTP demo is plaintext and unauthenticated. Only
expose it on a trusted LAN. Host/guest firewalls and AP isolation can still
block peer access.

## Guest tools

```text
ifconfig
ping example.com
traceroute example.com
nslookup example.com
wget https://example.com/
ctr build … && ctr run -p 8080 …
```

`wget` / `curl` print `fetching...` before the transfer, label **HTTP/2** when ALPN
`h2` is negotiated, show **Content-Type** (or full response headers with `-i`), and
warn when the body hits the **65536-byte (64 KiB)** client limit (`structured meta: truncated, received …`).
TLS failures surface `tls_last_error()` plus stable reject names (`fetch: tls-*`).

HTTPS trust is **WebPKI** (embedded roots + path build + hostname/time) by default.
**Pins** override; **TOFU** is opt-in via Settings (`tls_tofu=1` in `/etc/peak/display`).
Root DER files live under `certs/webpki/` (Mozilla/curl CA subset + Peak test root);
regenerate with `python3 scripts/gen-webpki-roots.py`. Path verify supports RSA-PKCS1-SHA256
and ECDSA P-256/P-384 (SHA-256/SHA-384).

## Stack

- PCI → Intel e1000
- Ethernet + ARP cache + IPv4 + ICMP echo reply
- UDP + DNS (A) + DHCP client
- TCP client and TCP listen/accept
- HTTP/1.0 GET client with redirect following (max **HTTP_REDIRECT_MAX** = 5 hops; honest error page)
- Session cookie jar lite (max 8 host entries; no Secure/HttpOnly/SameSite — see `http_cookie_jar_honesty()`)
- Container static HTTP server (GET/HEAD)
- **TLS 1.2 / 1.3**: ECDHE (X25519 or P-256 on 1.2) + AES-128/256-GCM or ChaCha20-Poly1305;
  ALPN `http/1.1`; TLS 1.3 via `supported_versions` + `key_share` (X25519)
- **Handshake auth**: ServerKeyExchange (1.2) / CertificateVerify (1.3) signatures verified;
  Finished `verify_data` checked against transcript PRF/HMAC
- **Crypto TUs** (Peak-authored + Apache-2.0 p256-m adapted for P-256 + MIT HACL*
  P-384): `crypto_hash.c` /
  `crypto_sha384.c` (SHA-256/384, HMAC, PRF), `crypto_hkdf.c` (HKDF / TLS 1.3 labels),
  `crypto_aead.c` (AES-GCM + ChaCha20-Poly1305), `crypto_x25519.c`, `crypto_p256.c`,
  `crypto_rsa.c` (RSA verify), `crypto.c` (RNG glue).
  Audit: every exported primitive is used by TLS, PeakDisk, or CSPRNG — no dead algos.

## TLS cipher / feature matrix

| Feature | TLS 1.2 | TLS 1.3 |
|---------|:-------:|:-------:|
| Offer / accept | yes | yes (`supported_versions`) |
| X25519 ECDHE | yes | yes (`key_share`) |
| P-256 ECDHE | yes | — |
| AES-128-GCM | `0xC02B`/`0xC02F` | `0x1301` |
| AES-256-GCM | `0xC02C`/`0xC030` | `0x1302` |
| ChaCha20-Poly1305 | `0xCCA8`/`0xCCA9` | `0x1303` |
| ALPN `http/1.1` | yes | yes |
| ALPN `h2` + HTTP/2 GET | yes (HPACK, 16 KiB frames, 64 KiB body store, WINDOW_UPDATE/PING) | yes |
| SKE / CertVerify | ECDSA-P256, RSA-PSS/PKCS1 | ECDSA-P256, RSA-PSS-SHA256 |
| Finished check | PRF verify_data | HMAC-finished |
| GREASE ClientHello | yes | yes |
| Session tickets / PSK | cache+offer (1.2 NST) | cache+offer (1.3 PSK-lite) |
| ECH | scaffold (fail-closed) | scaffold (fail-closed) |
| HTTP/2 ALPN `h2` | yes (HPACK headers, body limits) | yes |

Host goldens: `tests/host/test_tls.c` (`test_clienthello_goldens`) asserts suite order and
extensions. Optional live probe: `make smoke-tls-live` (soft-fail offline).

## Browser HTTPS UX

Dedicated TLS error pages (RNG / alert / expired / hostname mismatch / untrusted)
replace the generic handshake blob. On **Untrusted certificate**, the page offers
**Retry**, **Accept** (writes `/etc/peak/tls-tofu` for this host’s last cert digest),
and **Forget** (drops that TOFU entry). Accept’d hosts verify from the store even when
Settings → Network TOFU is off; opt-in TOFU still auto-remembers new hosts when enabled.
CLI mirrors: `tlsinfo -A` (Accept last failure), `tlsinfo -F [host]` (Forget).

Address-bar lock: `L` when verified HTTPS, `!` when HTTPS without full verify.
`fetch()` rejects with stable names: `fetch: tls-rng`, `tls-alert`, `tls-expired`,
`tls-mismatch`, `tls-untrusted`, `tls-handshake`. Specific `cert_fail` reasons
(expiry, hostname, chain signature) are preserved — not overwritten by a generic
WebPKI string.

WebPKI path building matches trust anchors by **SPKI** (cross-signed intermediates/
roots in the server chain) as well as full DER. QEMU prove notes and URL matrix:
`scripts/browser-https-fixlist.md`, `scripts/browser-https-diag.py`.

Active mixed content: `http://` subresources (`fetch`, `<script src>`) on `https://`
pages are blocked (`fetch: mixed-content`). HSTS-lite stores `max-age` hosts in
`/etc/peak/tls-hsts` and upgrades later plain HTTP navigations. Settings → Network
toggles TOFU and can clear pins / TOFU / HSTS.

## Encrypted Client Hello (ECH)

Peak ships an ECH **scaffold** (`tls_ech_*`): configs can be installed, and when
`tls_ech_set_required(1)` is set without a config the ClientHello build fails
closed (`ECH required but keys/config missing`). Full HPKE outer/inner ClientHello
encoding is not implemented yet — installing a config currently also fails closed
until HPKE lands (interop note).

## Limits

- Small connection table (`NET_TCP_MAX` = 16 concurrent client+server flows,
  `NET_LISTEN_MAX` = 8 passive listeners). When the table is full,
  `net_tcp_connect` / `net_tcp_listen` return `PEAK_EBUSY` — tools surface this
  as "connection table full" rather than hanging or dropping silently.
- DNS negative cache (10s) may report "cached failure" immediately after a
  timeout or empty response; wait or retry with a different name.
- Weak RNG (timer-based) — not for real security
- **Certificate trust**: pins → WebPKI (embedded roots + SPKI path build) → TOFU
  store (Accept / opt-in). DER X.509 parse covers SAN/validity/SPKI/
  BasicConstraints/KeyUsage/AKI/SKI. Validity enforced when RTC time is available.
  No OCSP/CRL yet.
- Bridged mode is platform-specific (macOS vmnet); Linux tap/bridge is not wired yet

## TLS hardening

- Constant-time tag / `verify_data` compares (`crypto_memeq`)
- Session secrets scrubbed on `tls_close` and handshake fail
- ClientHello GREASE (cipher, group, empty extension) per RFC 8701
- Handshake DoS budgets: max message `TLS_HS_MSG_MAX`, max records `TLS_HS_RECORD_MAX`
- Structured `tls_last_error_code()` alongside string `tls_last_error()`; `tls_err_name()` maps codes to short tags (`cert`, `alert`, …) and non-alert errors are prefixed `[tag]` in `tls_last_error()`
- `tlsinfo` CLI: trust summary (embedded WebPKI root count, pin count, TOFU toggle), session verify flags, bounded session ticket cache (`session_cache: used=N/4`), `-s` lists cached SNI/cipher/TLS version, `cert_fail` reason, last error; `-r` dumps root SHA-256 digests; `-m pattern host` exercises hostname matching; `-A` Accept last failed cert into TOFU; `-F [host]` Forget TOFU entry
- Session resume: TLS 1.2 offers cached tickets via `session_ticket`; TLS 1.3 PSK-lite offers `pre_shared_key` with HKDF/HMAC binder (RFC 8446); cached resumption master + ticket nonce when captured from NewSessionTicket. Post-handshake NewSessionTicket captured for both paths. ECH full HPKE remains NYI.

## Timeouts (100 Hz ticks)

Named budgets live in `kernel/net/net_internal.h` (`NET_*_TICKS`). Approximate
wall time assumes the kernel timer at 100 Hz:

| Constant | Ticks | ~Time | Used for |
|----------|------:|------:|----------|
| `NET_DHCP_TIMEOUT_DEFAULT` | 300 | 3s | DHCP DISCOVER / REQUEST |
| `NET_DNS_RESOLVE_TICKS` | 300 | 3s | DNS A lookup (http / tools) |
| `NET_DNS_CACHE_TTL_TICKS` | 3000 | 30s | Positive DNS A-cache TTL |
| `NET_DNS_NEG_TTL_TICKS` | 1000 | 10s | Negative DNS cache (timeout / empty) |
| `NET_ARP_RESOLVE_TICKS` | 200 | 2s | Next-hop MAC resolve |
| `NET_ARP_RETRY_TICKS` | 50 | 0.5s | ARP re-request interval |
| `NET_TCP_CONNECT_HTTP_TICKS` | 500 | 5s | Plaintext HTTP connect |
| `NET_TCP_SYN_RETRY_TICKS` | 100 | 1s | TCP SYN retransmit |
| `NET_TCP_RECV_SLICE_TICKS` | 100 | 1s | Per-recv poll slice |
| `NET_HTTP_IDLE_TCP_TICKS` | 800 | 8s | HTTP recv stall (TCP) |
| `NET_HTTP_IDLE_TLS_TICKS` | 1200 | 12s | HTTP recv stall (TLS) |
| `NET_TLS_HANDSHAKE_TICKS` | 1200 | 12s | TLS connect / handshake |
| `NET_TLS_RECORD_BODY_TICKS` | 1200 | 12s | Floor for TLS record body |

Blocking waits share `net_timed_out` / `net_poll_idle` (poll + HLT) rather than
tight spin loops; the stack remains synchronous (no async rewrite).

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `RNG not ready (crypto domain)` / HTTPS fails at ClientHello | Boot line or `ifconfig` `rng flags=` — need `CRYPTO` (HW RDRAND/RDSEED, EFI RNG, or **virtio-rng**). QEMU must pass `-device virtio-rng-pci` (`run-qemu.sh` / smoke do). `WEAK` alone is not enough in release. |
| `ifconfig` shows `fallback` | DHCP timed out; confirm bridged iface / DHCP server |
| LAN curl times out | Guest listening? `ctr ps` shows `Up/listen`. Same subnet? AP client isolation? |
| QEMU refuses vmnet | Run with permissions; set `PEAK_NET_IFACE` to the Wi‑Fi/Ethernet device |
| Smoke tests | Keep `PEAK_NET_MODE=user` (default) |

See also [docs/containers.md](containers.md).

### CLI DNS/HTTP depth (Pass 66)

- `wget` / `curl` accept `-X METHOD` and `-d body` (implicit POST when `-d` is set). Body uses `application/x-www-form-urlencoded`.
- `host` / `nslookup` accept `-6` (AAAA query) and `-x` (PTR reverse for dotted IPv4). Peak still routes **IPv4 only**; AAAA answers are diagnostic and may be shown even though the stack does not open IPv6 sockets.
