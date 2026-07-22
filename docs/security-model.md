# Peak OS security model

Peak OS security follows **Kerckhoffs's principle**: assume every algorithm, layout rule, policy format, and defense mechanism is public. Only short-lived keys and seeds are secret. **Moving Target Defense** (ASLR, KASLR, canaries) makes exploitation less reliable, but the system must still contain damage if randomization fails.

## Durable controls

1. Deny-by-default capabilities and minimal authority
2. Separate address spaces and fault containment
3. Non-executable and non-writable code (NX / W^X)
4. Authenticated encryption and trustworthy key generation
5. Verified software provenance and rollback
6. Local-first behavior, no telemetry, explicit persistence and network disclosure

Obscurity ("no designed attacks exist yet") is never counted as a control.

## Assets

| Asset | Location |
|-------|----------|
| Kernel integrity | ELF image, loaders, BootInfo ABI |
| User workspace | `/home/dev/workspace` |
| Agent policy / audit / memory | `/etc/peak`, `/var/peak` |
| TLS private keys / TOFU pins | RAM; `/etc/peak/tls-tofu` |
| PeakFS persist blob | ATA / SD PeakDisk |
| Clipboard / session secrets | RAM only by default |

## Attackers

| Attacker | In scope |
|----------|----------|
| Malicious / buggy ring-3 process | Yes |
| Malicious browser JS / page content | Yes |
| Network eavesdropper / MITM | Yes (TLS + TOFU / pins; no X.509 chain validation) |
| Layout-guessing exploit after a memory bug | Yes (containment + MTD) |
| Compromised agent prompt / tool abuse | Yes (path policy + approval) |
| Unsigned / tampered boot image | Planned (Phase S8; not shipped) |
| Hardware side channels / physical attacker with bus access | Non-goal (documented) |
| Hostile hypervisor / QEMU host | Non-goal |

## Trust boundaries

```
firmware / Peak loader
        │ BootInfo (+ future verified kernel; S8)
        ▼
     kernel ring-0 ── capabilities / VMM / CSPRNG
        │ syscalls (validated copy / handles)
        ▼
   ring-3 process / browser tab / agent session
```

## Security modes

| Mode | Behavior |
|------|----------|
| `release` | Cryptographic RNG required; TLS keygen fails closed without entropy |
| `dev-insecure` | May allow degraded RNG for QEMU bring-up; must print a visible warning; excluded from release ISO gates |
| `private` | Ephemeral: no PeakFS persist of secrets; session-scoped net grants |
| `workspace` | Persist `/home` only |
| `full` | Persist home + policy + opt-in var trees |

## Randomness inventory

Secrets that must come from the kernel CSPRNG (`kernel/random.c`):

- TLS `client_random` and X25519 private keys
- Stack canaries
- ASLR / KASLR slides
- PeakDisk salts / nonces / volume keys
- Allocator cookies (when enabled)

Rule: the *scheme* is documented; only the *seed* is secret. Never log seeds, canaries, or randomized bases.

## Current gaps (honest)

Still open (not claimed done):

- **Verified boot / signed releases / A/B rollback** — design notes in [verified-boot.md](verified-boot.md); not shipped (Phase S8)
- **Full ring-3 isolation for browser DOM/net** — in-kernel JS today; validated handles are next (Phase 10)
- **Release acceptance beyond CI** — host tests + bios/aarch64 smoke land; signed artifacts and continuous fuzz are not gates yet (Phase S9 partial)
- Session lock is convenience idle UI, not authentication

Already addressed earlier in the privacy-first program (see [CHANGELOG.md](../CHANGELOG.md)): CSPRNG + TLS fail-closed, NX/W^X, user-copy / SMEP-SMAP where applicable, capabilities + audit, sandboxed agent/browser/ctr policies, encrypted PeakFS modes, ASLR/KASLR/canaries. Remaining work is listed under Security in [ROADMAP.md](ROADMAP.md).

### TLS trust scope (honest)

The in-guest TLS 1.2 client verifies server certificates with **explicit SHA-256
pins** and/or **trust-on-first-use** per SNI host (`/etc/peak/tls-tofu`), plus
optional leaf hostname matching when parsing succeeds. **Full X.509 chain
validation is intentionally out of scope** — there is no embedded CA store, no
path building, and no CRL/OCSP. That is a deliberate trade-off for a small
freestanding stack: TOFU detects certificate swaps for known hosts; pins cover
known-good digests; neither replaces WebPKI or enterprise PKI policy.

## Degraded entropy

If boot cannot gather trusted entropy:

1. Set `PEAK_BOOT_FLAG_ENTROPY_WEAK` / runtime degraded flag
2. Refuse TLS key generation and encrypted PeakFS unlock in release mode
3. Still allow local desktop / offline tools
4. Keep mixing interrupt timing for eventual recovery

See [privacy.md](privacy.md) and [csprng.md](csprng.md).
