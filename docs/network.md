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

`ifconfig` prints the active mode (`dhcp`, `static`, or `fallback`).

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
wget https://example.com/
ctr build … && ctr run -p 8080 …
```

Browser: open a tab, type a URL, press Enter (no unsolicited TLS on `gui`).
Local JS demo: `peak://demo` (no network). HTTPS trust is **explicit SHA-256
pins** or **trust-on-first-use** (`/etc/peak/tls-tofu`) plus hostname match when
the leaf certificate can be parsed (`tls_hostname_matched`). There is **no X.509
chain validation** (no CA store, no path building, no CRL/OCSP) — by design for
this minimal in-guest client; TOFU/pins provide continuity, not WebPKI assurance.

## Stack

- PCI → Intel e1000
- Ethernet + ARP cache + IPv4 + ICMP echo reply
- UDP + DNS (A) + DHCP client
- TCP client and TCP listen/accept
- HTTP/1.0 GET client with redirect following
- Container static HTTP server (GET/HEAD)
- **TLS 1.2 / 1.3**: ECDHE (X25519 or P-256 on 1.2) + AES-128/256-GCM or ChaCha20-Poly1305;
  ALPN `http/1.1`; TLS 1.3 via `supported_versions` + `key_share` (X25519)
- **Handshake auth**: ServerKeyExchange (1.2) / CertificateVerify (1.3) signatures verified;
  Finished `verify_data` checked against transcript PRF/HMAC
- **Crypto TUs** (Peak-authored + Apache-2.0 p256-m adapted for P-256): `crypto_hash.c` /
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
| SKE / CertVerify | ECDSA-P256, RSA-PSS/PKCS1 | ECDSA-P256, RSA-PSS-SHA256 |
| Finished check | PRF verify_data | HMAC-finished |
| Session tickets / PSK | — | — (later) |
| ECH | — | — (later) |
| HTTP/2 ALPN `h2` | — | — (later) |

Host goldens: `tests/host/test_tls.c` (`test_clienthello_goldens`) asserts suite order and
extensions. Optional live probe: `make smoke-tls-live` (soft-fail offline).

## Limits

- Small connection table (`NET_TCP_MAX` = 16 concurrent, `NET_LISTEN_MAX` = 8)
- Exhausted slots return `PEAK_EBUSY` (no silent drop)
- Weak RNG (timer-based) — not for real security
- **Certificate trust is pins + TOFU only** (`/etc/peak/tls-tofu`); **full X.509
  chain validation is intentionally out of scope** (no CA bundle, no path
  validation). A changed cert for a known host fails closed —
  `rm /etc/peak/tls-tofu` to re-trust after a legitimate rotation
- Bridged mode is platform-specific (macOS vmnet); Linux tap/bridge is not wired yet

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
