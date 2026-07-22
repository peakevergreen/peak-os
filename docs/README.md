# Peak OS documentation

| Doc | Topic |
|-----|--------|
| [from-scratch.md](from-scratch.md) | Boot stack, freestanding design, purity goals |
| [ROADMAP.md](ROADMAP.md) | What’s next (no completed-phase history) |
| [CLI.md](CLI.md) | Shell and `/bin` utilities |
| [rpi.md](rpi.md) | Raspberry Pi ARM64: build, flash, firmware, matrix |
| [network.md](network.md) | In-guest IPv4, DHCP, DNS, TCP, TLS, HTTP |
| [containers.md](containers.md) | Peak `ctr` / demo containers |
| [browser-js.md](browser-js.md) | Browser + Peak JS subset |
| [sysmon.md](sysmon.md) | System monitor CLI/GUI (incl. compose/present timing) |
| [agent-protocol.md](agent-protocol.md) | In-guest peak-agent |
| [peakvec.md](peakvec.md) | Local vector index + blobstore |
| [security-model.md](security-model.md) | Capabilities, VFS policy, threat model |
| [privacy.md](privacy.md) | Privacy posture |
| [csprng.md](csprng.md) | Randomness / CSPRNG |
| [verified-boot.md](verified-boot.md) | Verified boot notes (not shipped) |

Also see at the repo root:

- [ARCHITECTURE.md](../ARCHITECTURE.md) — kernel layout and HAL
- [CONTRIBUTING.md](../CONTRIBUTING.md) — setup and MR checks
- [CHANGELOG.md](../CHANGELOG.md) — release notes
- [SECURITY.md](../SECURITY.md) — vulnerability reporting
- [CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md) — community standards

## Hardware / smoke checklists

| Checklist | Use |
|-----------|-----|
| [scripts/gui-stress-checklist.md](../scripts/gui-stress-checklist.md) | Commercial GFX / desktop present stress (1080p @ scale 3) |
| [scripts/pi3-hw-checklist.md](../scripts/pi3-hw-checklist.md) | Pi 3 HDMI/USB/persist acceptance |
| [scripts/lan-web-checklist.md](../scripts/lan-web-checklist.md) | LAN web container |
| [scripts/security-checklist.md](../scripts/security-checklist.md) | Security smoke |
