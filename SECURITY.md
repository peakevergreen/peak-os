# Security Policy

## Reporting a vulnerability

Please report security issues **privately** via GitHub Security Advisories for this repository (or a private email to the maintainers if Advisories are unavailable). Do not open a public issue with exploit details until a fix or advisory is published.

There is **no bug bounty** for Peak OS at this time.

## What to expect

- Peak is a from-scratch research / commercial-hobby OS. Treat it as **experimental**, not a hardened production system.
- We aim to acknowledge reports and triage severity against the documented threat model.

## Security model docs

| Doc | Topic |
|-----|--------|
| [docs/security-model.md](docs/security-model.md) | Capabilities, trust boundaries, gaps |
| [docs/privacy.md](docs/privacy.md) | Local-first / no telemetry posture |
| [docs/csprng.md](docs/csprng.md) | Entropy and DRBG |
| [docs/verified-boot.md](docs/verified-boot.md) | Verified boot plans (not shipped) |

## Important caveats

- **Session lock** on the desktop is idle convenience, not authentication.
- **Verified boot and signed releases** are roadmap items (Phase S8), not current controls.
- QEMU / hostile hypervisor attacks are **out of scope**.
- Release builds should refuse weak entropy for TLS / encrypted PeakFS; `dev-insecure` modes are for bring-up only.
