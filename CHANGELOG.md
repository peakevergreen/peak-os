# Changelog

All notable changes to Peak OS are documented here. Version strings in the guest/README may advance before a matching git tag exists.

## 0.2.0-ai (current, untagged)

- **Pass 139:** JS `Promise.allSettled`/`Promise.race` lite; fixture + host tests

- **Pass 138:** Wave 6 lock-in — smoke-cli gates Passes 119–137, ROADMAP/CHANGELOG Wave 6 shipped, security-checklist CI note

- **Pass 128:** raise ZIP/TAR file caps to 48; `zip -l` listing; tar list honesty footer

- **Pass 127:** `nc -w` timeout flag; 800ms listen window default; documented limits

- **Pass 126:** `tlsinfo` HSTS/resume/ticket PSK unavailable field depth

- **Pass 125:** `ss` netstat-lite over kernel TCP conn table

- **Pass 124:** `ln -sf` force symlink; `chmod` symbolic usage; `install -m` mode

- **Pass 123:** `stat -c` lite format; `du -s` summary; `df -h` capacity honesty strings

- **Pass 122:** `fmt`/`column`/`expand`/`unexpand` lite text layout tools

- **Pass 137:** Privacy grant list UI with revoke-one; session clear; docs update

- **Pass 136:** Idle lock minutes setting; honest "Enter unlocks (not a password)" copy

- **Pass 135:** Settings double focus rings; scale live preview before apply

- **Pass 134:** Terminal scrollback search match n/m; clear-scrollback action

- **Pass 133:** Images fit/1:1/zoom status line with PPM/BMP format label; folder next/prev

- **Pass 132:** Files Ctrl+click multi-select toggle; batch delete confirm toast

- **Pass 131:** Clipboard 4-slot picker overlay; notification history clear-all button

- **Pass 130:** Ctrl+Alt+Shift+arrow window nudge; snap zone HUD overlay while dragging

- **Pass 129:** Start menu filter match count; taskbar hover preview chip with window title

- **Pass 121:** shell `read`/`unset` builtins; pipe stages 6 / argv 24 caps

- **Pass 120:** awk `BEGIN`/`END`/vars; sed `q`/`=`; jq nested `.key.keys` depth

- **Pass 119:** `join` `-1`/`-2`/`-t` field/delimiter; `comm` `-1`/`-2`/`-3` column suppress

- **Pass 118:** Wave 5 lock-in — smoke-cli gates Passes 99–117, ROADMAP/CHANGELOG Wave 5 shipped, security-checklist CI note

- **Pass 117:** ctr WORKDIR + dir COPY; HTTP Range; heap freelist steal + ps starvation counters

- **Pass 116:** Agent `mem.store` + `peakvec.query`; expanded `fs.exec` allowlist (approvals unchanged)

- **Pass 115:** Notepad find n/m + replace-all; Monitor timestamped export footer

- **Pass 114:** Browser download path toast, bookmark +N overflow chip, isolation status line

- **Pass 113:** Help substring filter; Settings theme preview/apply labels

- **Pass 112:** Files drag ghost chip, drop-target highlight, Esc cancel, mismatch toasts

- **Pass 111:** blobstore delete reclaims pages (tip rewind + free-list reuse); host `test_blobstore` coverage

- **Pass 110:** blob-aware VFS copy (`vfs_read_at`/`vfs_write_at` streaming); `cp --promote-blob`

- **Pass 109:** DNS CNAME follow (4 hops) + `dnsflush` CLI

- **Pass 108:** HTTP chunked body decode; HTTP/2 POST body lite

- **Pass 107:** `curl`/`wget` `-H`/`-I`; `ping -c N` (max 10)

- **Pass 106:** `tar -t`/`-v`; raise `dd`/`gzip` caps to 32 KiB with printed limits

- **Pass 105:** `diff -u`, `patch` lite, `sha*sum -c` check mode

- **Pass 104:** `pgrep`/`pidof` + `dmesg` lite (scrollback ring, `-n`)

- **Pass 103:** `shuf`, `cksum` (CRC32), `xxd` — new `/bin` tools

- **Pass 102:** `xargs -n`/`-I`/`-0`; `find -print0`/`-exec` bounded (8 invocations)

- **Pass 101:** `grep -c`/`-l`/`-o` + `-A`/`-B` context lite (≤32 lines)

- **Pass 100:** `expr` + shell `&&`/`||` lists and `2>`/`2>>` lite redirects

- **Pass 99:** CLI.md sync — document `mktemp`/`install`/`sha1sum`/`basenc`/`reboot`/`disksave`

- **Pass 98:** Wave 4 lock-in — smoke-cli gates Passes 79–97, ROADMAP/CHANGELOG Wave 4 shipped, security-checklist CI note; restore host `test_tls` link of `tls_trust`/`tls_psk`; fix Pass 79/87 smoke markers

- **Pass 97:** heap/sched fairness under GUI load; ps share column label

- **Pass 96:** nc listen/connect flag clarity; traceroute hop timeout/loss honesty

- **Pass 95:** Agent GUI transcript search/filter + longer tool-result panes

- **Pass 94:** PeakVec namespace CLI + Agent recall lines in transcript

- **Pass 93:** Disks save progress, last-error, capacity/free clarity

- **Pass 92:** Richer connection table + TLS session/resume status in Net Explorer/Control

- **Pass 91:** Monitor graph polish + one-click export snapshot to VFS

- **Pass 90:** Help overlay searchable shortcuts + per-app focus hint

- **Pass 89:** Settings theme chrome mock preview (titlebar/taskbar/toast) before apply

- **Pass 88:** Drag Files row onto Notepad/Images to open

- **Pass 87:** JS for-await lite + module import depth fixture

- **Pass 86:** Browser ring-3 isolation scaffold + enforce fail-closed DOM/net

- **Pass 85:** DNS NXDOMAIN fresh vs cached-negative UX in host/nslookup

- **Pass 84:** wget/curl HTTP body 32 KiB + structured truncation meta

- **Pass 83:** awk/sed/jq 32 KiB caps; jq `select` / `|` pipe lite

- **Pass 82:** `mktemp` under `/tmp`; `install -D` parent-create lite

- **Pass 81:** `sha1sum`, `basenc --base32`; hash I/O up to 64 KiB

- **Pass 80:** `head`/`tail` `-c`; honest `timeout` wall limit (124, no fake success message)

- **Pass 79:** `join` (field-1) + `comm` three-column lite

- **Pass 78:** Wave 3 lock-in — smoke-cli gates Passes 59–77, ROADMAP/CHANGELOG Wave 3 shipped, security-checklist CI note

- **Pass 77:** ctr build ENV meta + EXPOSE default port on `ctr run`; `X-Peak-Env` on static HTTP

- **Pass 76:** Agent tools `fs.tree` + `sys.ps` with planner intent hooks

- **Pass 75:** Browser Save to Downloads (VFS `/home/dev/Downloads`) + bookmark remove in context menu

- **Pass 74:** VFS streaming PeakFS export (`vfs_export_ramdisk_stream`), chunked blob reads, 32 MiB cap + `vfs_export_last_error`

- **Pass 73:** TLS 1.3 PSK HKDF/HMAC binder; session cache resumption master + ticket nonce; `tlsinfo -s` resume fields

Baseline comparison: git tag `v0.1.0-mvp`.

### Boot and purity

- Peak BIOS + UEFI loaders; hybrid ISO; Limine / host serial bridges removed
- BootInfo v4 (HHDM, framebuffer, mmap, optional DTB, net config, entropy flags)
- `make purity` / CI purity gates
- CI honesty: hard-fail fuzz/manifest/doctor; require BOOTX64 + kernel.elf PHDR; tighten aarch64 smoke markers; clear `net_up` on DHCP fail
- CI gates: UEFI smoke, smoke-cli static gate (Pass 19–37 markers), host `-Werror`, real ELF fuzz, PeakFS QEMU roundtrip (`smoke_persist`)
- **Pass 38 (S9-lite):** smoke-cli enhancement-wave gates (incl. Passes 35–37), ROADMAP/CHANGELOG sync, security-checklist automation notes
- **Pass 58 (S9 remainder):** smoke-cli Wave 2 gates (Passes 39–49 + subsystem markers), ROADMAP Wave 2 shipped, security-checklist sync, TLS fuzz in `fuzz-elf-smoke.sh`
- **Pass 59:** `grep -i`/`-n`/`-v`/`-r`, multi-file `path:` prefixes, host `test_cli_grep`
- **Pass 60:** `find -type f|d`, `-iname`, `-maxdepth`, host `test_cli_find`
- **Pass 61:** `sort` `-r`/`-n`/`-u`, `uniq` `-c`, `wc` `-l`/`-w`/`-c`, host `test_cli_sortflags`
- **Pass 62:** `dd` lite (`if=`/`of=`/`bs`/`count`, 8 KiB cap); `sync` calls `blockdev_flush` when a block device is present
- **Pass 63:** `file` command — magic sniff for ELF/PPM/BMP/PEAKZIP1/PEAKGZ1/text vs binary
- **Pass 64:** `date +%s` / `+%Y-%m-%d` (RTC); `printf` documents `\n` `\t` `\\` escapes
- **Pass 65:** Shell pipe/redirect capture raised to 32 KiB; truncation warning on pipe overflow
- **Pass 72:** Files/Notepad keyboard focus rings (Tab/arrows/Enter) matching Settings a11y pattern
- **Pass 71:** Terminal in-window tab strip (up to 4 tabs); click/+ or Ctrl+Shift+T
- **Pass 70:** Ctrl+Alt+arrow keyboard window snap (left/right/maximize)
- **Pass 69:** Notification history panel (Ctrl+Shift+H, Start → Alerts); Ctrl+Shift+V pastes previous clipboard in Terminal/Notepad
- **Pass 68:** Files breadcrumb segments clickable to change directory
- **Pass 67:** Start menu mouse hover updates selection; wheel scrolls clamped app list
- **Pass 66:** `wget`/`curl` `-X POST` + `-d` body; `host`/`nslookup` `-6` AAAA and `-x` PTR (honest IPv4-only routing note)
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
- **Pass 51:** Browser tab strip labels (title/URL fit), per-tab close **x**, last-closed restore lite (**Shift+T** / context menu)
- **Pass 48:** Images directory next/prev ([/] wheel), keyboard zoom/pan (0/1 fit/actual, Shift+arrows), status strip, drag-pan, Files open-with goto
- **Pass 50:** Files copy/cut/paste path ops (Ctrl+C/X/V), status strip, empty-folder paste hints
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
- **Pass 56:** PeakVec IVF-lite explain (`peakvec_query_ex`), `peakvec query` CLI (`--explain`/`--timing`), coarse bucket stats, sysmon `peakvec_us` on query
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
- **Pass 59:** `grep` depth — `-i`/`-n`/`-v`/`-r`, multi-file `path:` prefixes, recursive VFS walk
- **Pass 60:** `find` depth — `-type f|d`, case-insensitive `-iname`, `-maxdepth` walk limit
- **Pass 61:** `sort` `-r`/`-n`/`-u`, `uniq` `-c`, `wc` field flags `-l`/`-w`/`-c`
- **Pass 62:** `dd` lite (`if=`/`of=`/`bs`/`count`, 8 KiB cap); `sync` calls `blockdev_flush` when a block device is present
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
- **Pass 53:** HTTP/2 client depth — HPACK response headers, body limit metadata (`HTTP2_BODY_MAX`), `wget`/`curl` `-i` + truncation warnings
- Hash/pager tools: `sha256sum`, `md5sum`, `base64`, `less`/`more`, `time`
- Net diag: `nslookup`, `host`, `nc` (TCP connect); Pass 23 adds dig-style DNS output, `traceroute` staged probe, richer `wget`/`curl` TLS errors
- Shell history: persistent `/var/peak/history`, Up/Down/Ctrl-P/N recall, `history`, `!!`, exit-status prompt, errno-style path errors
- Host tests: `test_libpeak`, `test_shell_split`, `test_console_scroll`, `test_cli_crypto`, `test_agent_tools`, `test_desktop_titles`, `test_img_decode`
- VFS: `vfs_last_error`, `PEAK_EISDIR` on directory write; Pass 22 errno cases in `test_vfs`; Pass 41 mode bits + `vfs_chmod`

### Agent and PeakVec

- In-guest planner (`ask`), capabilities, audit, GUI write approval
- Agent tools expanded: fs.stat, fs.mkdir, fs.rm, fs.search, fs.grep, sys.info, net.ping, mem.recall, audit.tail; AGENT_TOOLS_MAX 16; fs.exec allowlist widened
- **Pass 57:** Agent tools wave-2 — `fs.diff` (bounded line hunks), privacy-gated `net.fetch` (2 KiB body cap), planner intent hooks for diff/fetch
- Policy seed uses `fs.exec` (no phantom `proc.exec` deny)
- Agent GUI uses theme colors, scrollable transcript, write-approval toasts, search-aware summarize
- **Pass 49:** Agent GUI — 256-char input, scrollable transcript with tool/audit lines, clearer write-approval prompt (Y/N + path)
- PeakVec embeddings + recall; streamed PeakFS / blobstore
- **Pass 36:** peakvec_stats, blobstore_stats/check, peakvec builtin

### Browser and network

- Peak JS bytecode VM, DOM/CSS subset, `peak://demo`
- In-guest IPv4 DHCP/DNS/TCP/TLS/HTTP; e1000 on PC; LAN web-demo containers
- **Pass 55 (TLS session resume):** bounded ticket cache LRU; TLS 1.2 `session_ticket` offer + NST capture; TLS 1.3 PSK-lite (`pre_shared_key` + stub binder); `tlsinfo -s` session cache listing
- **Pass 24 (TLS/WebPKI polish):** `tls_err_name()` + RFC alert names; `[tag]` prefixes on structured TLS errors; `tlsinfo` CLI; `webpki_root_sha256()` root listing; ECH full HPKE remains NYI (scaffold fail-closed only)
- **Pass 53 (HTTP/2 client depth):** HPACK response headers synthesized into HTTP/1.x; `http2_last_meta()` body totals/truncation; `wget`/`curl` show HTTP/2 status, Content-Type, `-i` headers, 8192-byte limit warnings
- **Pass 30 (Browser + Peak JS UX):** back/forward toolbar + **b**/**f** keys; VFS bookmarks bar + context menu; JS console panel (`console.log` capture); TLS/net error pages use `tls_last_error()` + `net_last_error()`
- **Pass 51 (Browser tab chrome):** tab strip title/URL labels with fetch indicator; per-tab **x** close affordance; last-closed restore lite (**Shift+T**, context menu)
- **Pass 54 (Web API depth):** `Response.json()` on fetch responses; VFS-backed `localStorage` under `/var/peak/localStorage/`; fail-closed matrix in `webapi_stubs.c`; host `test_webapi` coverage

### Raspberry Pi

- aarch64 HAL, `kernel8.img`, reproducible SD image + flash helpers
- Software FB + polled DWC2 HID path implemented; **Pi 3 hardware sign-off pending**
- Pi 4/5 net, xHCI, Wi‑Fi, GPU accel: not ready

### Security (Phases S0–S8)

- CSPRNG, NX/W^X, user-copy hardening, capabilities, encrypted PeakFS modes, ASLR/KASLR/canaries where applicable
- Verified boot: loader digest verify + optional manifest HMAC (`verify_sig`); host signing ceremony shipped; UEFI Secure Boot enrollment still optional

## 0.1.0-mvp

- Initial public MVP freeze (`v0.1.0-mvp`)
