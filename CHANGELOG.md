# Changelog

All notable changes to Peak OS are documented here. Version strings in the guest/README may advance before a matching git tag exists.

## 0.2.0-ai (current, untagged)

Baseline comparison: git tag `v0.1.0-mvp`.

### Boot and purity

- Peak BIOS + UEFI loaders; hybrid ISO; Limine / host serial bridges removed
- BootInfo v4 (HHDM, framebuffer, mmap, optional DTB, net config, entropy flags)
- `make purity` / CI purity gates
- CI honesty: hard-fail fuzz/manifest/doctor; require BOOTX64 + kernel.elf PHDR; tighten aarch64 smoke markers; clear `net_up` on DHCP fail
- CI gates: UEFI smoke, smoke-cli static gate, host `-Werror`, real ELF fuzz, PeakFS QEMU roundtrip (`smoke_persist`)
- PeakDisk atomic publish (payload→header) + SDHCI CMD13 flush; DWC2 hub enum/split/hotplug; `/bin/disksave`
- Quieter boot: status lines only (net IP folded into `e1000 (dhcp …)`; no chatter for JS/PeakVec/disk absence)
- **S8 loader verify:** kernel SHA-256 vs `peak.conf` / embedded `SHA256SUMS`; optional `verify_sig=1` HMAC-SHA256 of manifest (dev key stub); host `sign-release.py` / `verify-release.py` remain the ceremony gate

### Desktop / Commercial GFX

- Software FB compositor: opaque move, honest damage, paced presents
- Per-window ARGB surfaces + budget; rubber-band resize; soft cursor
- **Desktop shell (Pass 07):** Apps/System Start menu, labeled chrome, `MAX_WINS` 16, hit-tested context-menu framework (`ui_widgets`)
- **Files app (Pass 08):** name/size columns, breadcrumbs, scroll overflow, copy path, context menus, open-with hooks
- **Notepad app (Pass 09):** multi-line editor, VFS load/save, dirty flag, clipboard, context menu
- **Images app (Pass 10):** PPM/BMP decode, fit/1:1/zoom-pan viewer, host test_img_decode
- **Disks app (Pass 11):** PeakDisk status, VFS/PMM stats, confirmed disksave, context menu
- **Net Explorer (Pass 12):** link/IP/DNS pane, ping & nslookup, context menu
- **Net Control (Pass 13):** net-allow, kill switch, persist, DHCP renew, RNG; Settings deep-link
- **Terminal (Pass 14):** scrollback indicator, select/copy/paste, per-window buffers, context menu
- **Settings (Pass 15):** explicit hit rects, context menus for Settings/Agent/Monitor
- **Browser + notify (Pass 16):** fetch progress bar, error retry CTA, browser context menu, toast damage thrift
- **Monitor + CLI (Pass 18):** readable compose/present timing, improved `memory`/`audit` output
- Display: x86 VBlank probe, Pi mailbox pageflip; Monitor compose/present timing
- `SYS_peakgui` / guiproto buffer attach + damage
- CLI boot scroll stays on the front framebuffer (does not wipe via empty backbuffer)
- CLI line-edit clears trailing glyphs on shrink; `gui` hints use Ctrl+Alt+Esc

### CLI /bin builtins

- Quote-aware argv split (`ask "…"`, `js -e '…'`); help table synced with registry
- `-h` / `--help` gap-fill on text/sys utils; docs/CLI.md inventory refresh
- New builtins: `printf`, `tee`, `test`/`[`, `yes` (bounded)
- **Pass 19:** `fold`, `rev`, `od`, `split`, `paste`, `nl`, `tac`, `xargs`
- **Pass 20:** `hostname`, `uptime`, `whoami`, `id`, `cal`, `gzip`/`gunzip` (PEAKGZ1), `timeout`, `watch`
- **Pass 21:** `!n` history, aliases (`/var/peak/aliases`), `cd -`, Tab path/`/bin` completion, clearer redirect/pipe errors
- **Pass 24:** `tlsinfo` (WebPKI root count, pins/TOFU, last TLS error); `tls_err_name()` / alert desc mapping; `webpki_root_sha256()` for root digest listing
- **Pass 22:** VFS errno hygiene (`unlink`/`rmdir`/`stat`/`normalize`/`mkdir`), `peak_strerror(EBUSY)`, `stat`/`df` backing hints
- **Pass 25:** ctr build log line numbers, 0-COPY / 256 KiB cap failures, path sandbox messages name escaped paths; clearer `ctr build` log output
- Hash/pager tools: `sha256sum`, `md5sum`, `base64`, `less`/`more`, `time`
- Net diag: `nslookup`, `host`, `nc` (TCP connect)
- Shell history: persistent `/var/peak/history`, Up/Down/Ctrl-P/N recall, `history`, `!!`, exit-status prompt, errno-style path errors
- Host tests: `test_libpeak`, `test_shell_split`, `test_console_scroll`, `test_cli_crypto`, `test_agent_tools`, `test_desktop_titles`, `test_img_decode`
- VFS: `vfs_last_error`, `PEAK_EISDIR` on directory write; Pass 22 errno cases in `test_vfs`

### Agent and PeakVec

- In-guest planner (`ask`), capabilities, audit, GUI write approval
- Agent tools expanded: fs.stat, fs.mkdir, fs.rm, fs.search, sys.info, mem.recall, audit.tail; AGENT_TOOLS_MAX 16; fs.exec allowlist widened
- Policy seed uses `fs.exec` (no phantom `proc.exec` deny)
- Agent GUI uses theme colors, scrollable transcript, write-approval toasts, search-aware summarize
- PeakVec embeddings + recall; streamed PeakFS / blobstore

### Browser and network

- Peak JS bytecode VM, DOM/CSS subset, `peak://demo`
- In-guest IPv4 DHCP/DNS/TCP/TLS/HTTP; e1000 on PC; LAN web-demo containers
- **Pass 24 (TLS/WebPKI polish):** `tls_err_name()` + RFC alert names; `[tag]` prefixes on structured TLS errors; `tlsinfo` CLI; `webpki_root_sha256()` root listing; ECH full HPKE remains NYI (scaffold fail-closed only)

### Raspberry Pi

- aarch64 HAL, `kernel8.img`, reproducible SD image + flash helpers
- Software FB + polled DWC2 HID path implemented; **Pi 3 hardware sign-off pending**
- Pi 4/5 net, xHCI, Wi‑Fi, GPU accel: not ready

### Security (Phases S0–S8)

- CSPRNG, NX/W^X, user-copy hardening, capabilities, encrypted PeakFS modes, ASLR/KASLR/canaries where applicable
- Verified boot: loader digest verify + optional manifest HMAC (`verify_sig`); host signing ceremony shipped; UEFI Secure Boot enrollment still optional

## 0.1.0-mvp

- Initial public MVP freeze (`v0.1.0-mvp`)
