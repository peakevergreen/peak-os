# Changelog

All notable changes to Peak OS are documented here. Version strings in the guest/README may advance before a matching git tag exists.

## 0.2.0-ai (current, untagged)

Baseline comparison: git tag `v0.1.0-mvp`.

### Boot and purity

- Peak BIOS + UEFI loaders; hybrid ISO; Limine / host serial bridges removed
- BootInfo v4 (HHDM, framebuffer, mmap, optional DTB, net config, entropy flags)
- `make purity` / CI purity gates
- CI honesty: hard-fail fuzz/manifest/doctor; require BOOTX64 + kernel.elf PHDR; tighten aarch64 smoke markers; clear `net_up` on DHCP fail
- CI gates: UEFI smoke, smoke-cli static gate (Pass 19–37 markers), host `-Werror`, real ELF fuzz, PeakFS QEMU roundtrip (`smoke_persist`)
- **Pass 38 (S9-lite):** smoke-cli enhancement-wave gates (incl. Passes 35–37), ROADMAP/CHANGELOG sync, security-checklist automation notes
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
- **Pass 48:** Images directory next/prev ([/] wheel), keyboard zoom/pan (0/1 fit/actual, Shift+arrows), status strip, drag-pan, Files open-with goto
- **Disks app (Pass 11):** PeakDisk status, VFS/PMM stats, confirmed disksave, context menu
- **Net Explorer (Pass 12):** link/IP/DNS pane, ping & nslookup, context menu
- **Net Control (Pass 13):** net-allow, kill switch, persist, DHCP renew, RNG; Settings deep-link
- **Pass 33:** Net Explorer/Control progress lines, copy local/resolved IP, Settings↔Net Control deep-link; Disks save confirm/progress clarity
- **Terminal (Pass 14):** scrollback indicator, select/copy/paste, per-window buffers, context menu
- **Terminal (Pass 28):** 512-line scrollback, find-in-buffer (Ctrl+F) with match highlight, copy-on-select toggle, UI-scale glyph sync
- **Settings (Pass 15):** explicit hit rects, context menus for Settings/Agent/Monitor
- **Browser + notify (Pass 16):** fetch progress bar, error retry CTA, browser context menu, toast damage thrift
- **Pass 30:** Browser back/forward chrome, VFS bookmarks (`/var/peak/bookmarks`), JS console panel, TLS/net error detail on error pages
- **Pass 32 (notify/clipboard/overlays):** toast history ring, dismiss control, clipboard previous slot, Alt+Tab hints, login splash
- **Monitor + CLI (Pass 18):** readable compose/present timing, improved `memory`/`audit` output
- **Pass 36:** peakvec stats CLI, blobstore integrity in df, recall UX
- **Pass 34:** Monitor/sysmon sparkline legend + ranges, memory/heap breakdown, task sort, export to `/tmp/sysmon.txt`, CLI/GUI parity via `sysmon_snapshot`
- **Pass 35:** Heap freelist stats + fragmentation/oom counters in `free`/sysmon; honest OOM toasts in GUI/CLI; `ps` age/wake/share columns; `copy_to_user` write-probe hardening
- **Pass 37:** Keyboard repeat tuning (PS/2 typematic + software repeat for USB), mouse acceleration lite, CLI scrollback search (Ctrl+F, 128 lines), clearer Ctrl+Alt+Esc CLI↔desktop hints
- **Pass 29:** Files confirm-delete arm, Home/End + Shift range select, hardened open-with; Notepad line numbers, Ctrl+F find, Ctrl+H replace lite
- **Pass 50:** Files Ctrl+C/X/V copy/cut/paste (VFS copy/rename), cut-cancel on Esc, context-menu Paste, status strip + empty-folder messaging
- Display: x86 VBlank probe, Pi mailbox pageflip; Monitor compose/present timing
- `SYS_peakgui` / guiproto buffer attach + damage
- CLI boot scroll stays on the front framebuffer (does not wipe via empty backbuffer)

### CLI /bin builtins

- Quote-aware argv split (`ask "…"`, `js -e '…'`); help table synced with registry
- `-h` / `--help` gap-fill on text/sys utils; docs/CLI.md inventory refresh
- New builtins: `printf`, `tee`, `test`/`[`, `yes` (bounded)
- **Pass 19:** `fold`, `rev`, `od`, `split`, `paste`, `nl`, `tac`, `xargs`
- **Pass 41:** VFS mode bits (default `0644`/`0755`); `chmod` (octal + `u+x`/`g-w`); `ls -l` and `stat` show mode
- **Pass 39:** `awk` lite — field split (`-F`), `$n`/`NR`/`NF`, `/pat/ { print }`
- **Pass 47:** `zip`/`unzip` — PEAKZIP1 multi-file archive (store/RLE per entry; 64 KiB cap)
- **Pass 44:** `uname -a/-s/-n/-m/-r` from BootInfo platform/ABI + `$HOSTNAME`
- **Pass 46:** `jq` lite — `.key`, `.[]`, `keys`, `length`, compact JSON print (8 KiB)
- **Pass 40:** `sed` depth — line addresses (`N`, `N,M`), `s///g` global replace, `y///` transliterate
- **Pass 42:** VFS symlinks, `ln -s`/`readlink`, resolve-on-open (`PEAK_ELOOP`), ls/stat link hints
- **Pass 43:** `ls -lh`, `du -h`, `df -h` — human-readable KiB/MiB sizes in file utils
- **Pass 20:** `hostname`, `uptime`, `whoami`, `id`, `cal`, `gzip`/`gunzip` (PEAKGZ1), `timeout`, `watch`
- **Pass 21:** `!n` history, aliases (`/var/peak/aliases`), `cd -`, Tab path/`/bin` completion, clearer redirect/pipe errors
- **Pass 24:** `tlsinfo` (WebPKI root count, pins/TOFU, last TLS error); `tls_err_name()` / alert desc mapping; `webpki_root_sha256()` for root digest listing
- **Pass 26:** agent tools `fs.grep`, `net.ping` (privacy-gated); richer `sys.info`; readable `policy` CLI; aligned audit/memory tail formatting
- **Pass 27:** Start menu typeahead filter; edge snap preview (left/right/top maximize); polished shortcuts help overlay; accent focus ring on titlebar
- **Pass 32:** toast history ring + click dismiss; clipboard history ring with `clipboard_get_previous`; Alt+Tab label polish; login splash polish
- **Pass 22:** VFS errno hygiene (`unlink`/`rmdir`/`stat`/`normalize`/`mkdir`), `peak_strerror(EBUSY)`, `stat`/`df` backing hints
- **Pass 25:** ctr build log line numbers, 0-COPY / 256 KiB cap failures, path sandbox messages name escaped paths; clearer `ctr build` log output
- **Pass 23:** dig-style `nslookup`/`host`, clearer `ifconfig` route/DNS, `wget`/`curl` TLS detail + progress note, `traceroute` lite
- Hash/pager tools: `sha256sum`, `md5sum`, `base64`, `less`/`more`, `time`
- Net diag: `nslookup`, `host`, `nc` (TCP connect); Pass 23 adds dig-style DNS output, `traceroute` staged probe, richer `wget`/`curl` TLS errors
- Shell history: persistent `/var/peak/history`, Up/Down/Ctrl-P/N recall, `history`, `!!`, exit-status prompt, errno-style path errors
- Host tests: `test_libpeak`, `test_shell_split`, `test_console_scroll`, `test_cli_crypto`, `test_agent_tools`, `test_desktop_titles`, `test_img_decode`
- VFS: `vfs_last_error`, `PEAK_EISDIR` on directory write; Pass 22 errno cases in `test_vfs`; Pass 41 mode bits + `vfs_chmod`

### Agent and PeakVec

- In-guest planner (`ask`), capabilities, audit, GUI write approval
- Agent tools expanded: fs.stat, fs.mkdir, fs.rm, fs.search, fs.grep, sys.info, net.ping, mem.recall, audit.tail; AGENT_TOOLS_MAX 16; fs.exec allowlist widened
- Policy seed uses `fs.exec` (no phantom `proc.exec` deny)
- Agent GUI uses theme colors, scrollable transcript, write-approval toasts, search-aware summarize
- **Pass 49:** Agent GUI — 256-char input, scrollable transcript with tool/audit lines, clearer write-approval prompt (Y/N + path)
- PeakVec embeddings + recall; streamed PeakFS / blobstore
- **Pass 36:** peakvec_stats, blobstore_stats/check, peakvec builtin

### Browser and network

- Peak JS bytecode VM, DOM/CSS subset, `peak://demo`
- In-guest IPv4 DHCP/DNS/TCP/TLS/HTTP; e1000 on PC; LAN web-demo containers
- **Pass 24 (TLS/WebPKI polish):** `tls_err_name()` + RFC alert names; `[tag]` prefixes on structured TLS errors; `tlsinfo` CLI; `webpki_root_sha256()` root listing; ECH full HPKE remains NYI (scaffold fail-closed only)
- **Pass 30 (Browser + Peak JS UX):** back/forward toolbar + **b**/**f** keys; VFS bookmarks bar + context menu; JS console panel (`console.log` capture); TLS/net error pages use `tls_last_error()` + `net_last_error()`

### Raspberry Pi

- aarch64 HAL, `kernel8.img`, reproducible SD image + flash helpers
- Software FB + polled DWC2 HID path implemented; **Pi 3 hardware sign-off pending**
- Pi 4/5 net, xHCI, Wi‑Fi, GPU accel: not ready

### Security (Phases S0–S8)

- CSPRNG, NX/W^X, user-copy hardening, capabilities, encrypted PeakFS modes, ASLR/KASLR/canaries where applicable
- Verified boot: loader digest verify + optional manifest HMAC (`verify_sig`); host signing ceremony shipped; UEFI Secure Boot enrollment still optional

## 0.1.0-mvp

- Initial public MVP freeze (`v0.1.0-mvp`)
