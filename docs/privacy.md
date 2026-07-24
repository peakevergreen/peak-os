# Peak OS privacy guarantees

Privacy is a first-class product property, not an accident of being unfinished.

## Promises

1. **No telemetry** — Peak does not phone home. There is no analytics channel.
2. **Local-only agent** — `peak-agent` runs in-guest. No host serial agent / SSH bridge.
3. **Network-idle GUI** — Opening the desktop does not initiate DNS, DHCP beyond boot policy, TLS, or HTTP.
4. **Explicit persistence** — Saving PeakFS is opt-in (Save disk / profile). Private mode is RAM-only.
5. **Explicit network disclosure** — Outbound client and LAN listen require capability grants / consent.
6. **Identifier minimization** — Prefer randomized locally-administered MAC when enabled; freeze browser UA; do not log SNI/URLs in production builds.
7. **COM1 diagnostics** — Serial may print boot/status markers. It must not print seeds, canaries, passphrases, TLS master secrets, or full randomized addresses in release builds.

## Persistence profiles

| Profile | Persists |
|---------|----------|
| `private` | Nothing (RAM VFS only) |
| `workspace` | `/home` tree |
| `full` | `/home`, `/etc/peak`, opted-in `/var/peak` |

Agent memory, net grants, and browser storage are session-scoped unless the user opts in.

## Network defaults

- Outbound: denied until grant (session or origin)
- Listeners: localhost-only; LAN expose needs separate consent
- Kill switch: Settings → Network short-circuits client/listen paths

## Secure deletion (honest limits)

`vfs_unlink` + heap zeroing + rewriting a PeakDisk image are **best effort**. Peak does not claim DoD wipe, TRIM-guaranteed erase, or protection against a hostile host that snapshots the qcow2/raw disk.

## Session lock (honest)

Desktop idle lock is a **privacy cover**, not authentication. Anyone with console
access can press Enter to resume. It does not encrypt VFS, revoke capabilities, or
replace a password. See [security-model.md](security-model.md).

## Kill switch

Enabling the network kill switch (Settings → Privacy, or
`privacy kill-switch on --confirm`) blocks outbound client and listen paths until
turned off. Enabling requires an explicit confirm click/flag so it is hard to
toggle by accident.

## UI surfaces

Settings → General: plain-language storage summary (disk present, what persists).  
Settings → Privacy: persist profile (private / workspace / full), network kill switch
(double-click to enable), clear session (revokes grants, caps, clipboard).  
Settings → Network: link info, trust-on-first-use toggle, forget saved TLS certificates.

CLI: `privacy persist private|workspace|full`, `privacy net-allow`,
`privacy kill-switch on --confirm` / `privacy kill-switch off`. See [CLI.md](CLI.md).

See [security-model.md](security-model.md).
