# Changelog

All notable changes to Peak OS are documented here. Version strings in the guest/README may advance before a matching git tag exists.

## 0.2.0-ai (current, untagged)

Baseline comparison: git tag `v0.1.0-mvp`.

### Boot and purity

- Peak BIOS + UEFI loaders; hybrid ISO; Limine / host serial bridges removed
- BootInfo v4 (HHDM, framebuffer, mmap, optional DTB, net config, entropy flags)
- `make purity` / CI purity gates

### Desktop / Commercial GFX

- Software FB compositor: opaque move, honest damage, paced presents
- Per-window ARGB surfaces + budget; rubber-band resize; soft cursor
- Display: x86 VBlank probe, Pi mailbox pageflip; Monitor compose/present timing
- `SYS_peakgui` / guiproto buffer attach + damage
- CLI boot scroll stays on the front framebuffer (does not wipe via empty backbuffer)
- CLI line-edit clears trailing glyphs on shrink; `gui` hints use Ctrl+Alt+Esc

### CLI /bin builtins

- Quote-aware argv split (`ask "…"`, `js -e '…'`); help table synced with registry
- `-h` / `--help` gap-fill on text/sys utils; docs/CLI.md inventory refresh
- Host tests: `test_libpeak`, `test_shell_split`, `test_console_scroll`

### Agent and PeakVec

- In-guest planner (`ask`), capabilities, audit, GUI write approval
- PeakVec embeddings + recall; streamed PeakFS / blobstore

### Browser and network

- Peak JS bytecode VM, DOM/CSS subset, `peak://demo`
- In-guest IPv4 DHCP/DNS/TCP/TLS/HTTP; e1000 on PC; LAN web-demo containers

### Raspberry Pi

- aarch64 HAL, `kernel8.img`, reproducible SD image + flash helpers
- Software FB + polled DWC2 HID path implemented; **Pi 3 hardware sign-off pending**
- Pi 4/5 net, xHCI, Wi‑Fi, GPU accel: not ready

### Security (Phases S0–S7)

- CSPRNG, NX/W^X, user-copy hardening, capabilities, encrypted PeakFS modes, ASLR/KASLR/canaries where applicable
- Verified boot / signed releases: **not shipped** (S8)

## 0.1.0-mvp

- Initial public MVP freeze (`v0.1.0-mvp`)
