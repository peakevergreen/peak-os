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
Local JS demo: `peak://demo` (no network). HTTPS trust is pin or TOFU plus
hostname match when the leaf certificate can be parsed (`tls_hostname_matched`).

## Stack

- PCI → Intel e1000
- Ethernet + ARP cache + IPv4 + ICMP echo reply
- UDP + DNS (A) + DHCP client
- TCP client and TCP listen/accept
- HTTP/1.0 GET client with redirect following
- Container static HTTP server (GET/HEAD)
- **TLS 1.2**: ECDHE (X25519) + AES-128-GCM or ChaCha20-Poly1305

## Limits

- Small connection table (`NET_TCP_MAX`)
- Weak RNG (timer-based) — not for real security
- Certificate verification is pins + trust-on-first-use (`/etc/peak/tls-tofu`);
  no X.509 chain validation. A changed cert for a known host fails closed —
  `rm /etc/peak/tls-tofu` to re-trust after a legitimate rotation
- Bridged mode is platform-specific (macOS vmnet); Linux tap/bridge is not wired yet

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `ifconfig` shows `fallback` | DHCP timed out; confirm bridged iface / DHCP server |
| LAN curl times out | Guest listening? `ctr ps` shows `Up/listen`. Same subnet? AP client isolation? |
| QEMU refuses vmnet | Run with permissions; set `PEAK_NET_IFACE` to the Wi‑Fi/Ethernet device |
| Smoke tests | Keep `PEAK_NET_MODE=user` (default) |

See also [docs/containers.md](containers.md).
