# Peak OS roadmap — what’s next

Direction: stay on the from-scratch kernel. Agents, tools, and project context are **system primitives**. Inference stays an **in-guest** planner (no host LLM bridge by default).

Shipped baseline and history: [CHANGELOG.md](../CHANGELOG.md). Architecture: [ARCHITECTURE.md](../ARCHITECTURE.md).

## Enhancement wave (Passes 19–38)

**Shipped on `main` (Passes 19–38):** CLI text/sys tools, shell history/aliases, VFS errno hygiene, net diag + `tlsinfo`, container build clarity, agent tools (`fs.grep`, `net.ping`), desktop chrome through Monitor/sysmon export, browser/notify/clipboard polish, Net Explorer/Control/Disks UX, heap/scheduler hardening, PeakVec/blobstore stats, input/console polish, and S9-lite smoke-cli lock-in.

**Pass 38 (S9-lite):** `smoke-cli.sh` gates for Pass 19–37 markers, host-test coverage, ROADMAP/CHANGELOG sync, security-checklist automation notes — release acceptance without a separate signing ceremony.


## Enhancement wave 3 (Passes 59–78) — shipped

**Shipped on `main` (Passes 59–78):** CLI grep/find/sort depth, pipe buffer, net POST/AAAA, desktop UX (start menu through Files/Notepad a11y), TLS 1.3 PSK binder, streaming PeakFS export, browser downloads/bookmarks, agent `fs.tree`/`sys.ps`, container ENV/EXPOSE depth, and Wave 3 smoke-cli lock-in (Pass 78).

**Pass 78 (Wave 3 lock-in):** `smoke-cli.sh` gates for Passes 59–77 markers, ROADMAP/CHANGELOG sync, security-checklist automation notes — Wave 3 release acceptance.

## Enhancement wave 4 (Passes 79–98) — shipped

**Shipped on `main` (Passes 79–97):** join/comm, head/tail/timeout, crypto pack, mktemp/install, text buffers, HTTP body policy, DNS negcache, browser ring-3, JS for-await, Files DnD open-with, theme preview, help shortcuts, Monitor export, Net TLS table, Disks save UX, PeakVec namespace, Agent search, nc/traceroute depth, heap/sched GUI fairness.

**Pass 98 (Wave 4 lock-in):** `smoke-cli.sh` gates for Passes 79–97 markers, ROADMAP/CHANGELOG sync, security-checklist CI note — Wave 4 release acceptance.

## Enhancement wave 5 (Passes 99–118) — shipped

**Shipped on `main` (Passes 99–117):** CLI docs sync, shell scripting depth, grep/find/xargs, shuf/cksum/xxd, pgrep/dmesg, diff/patch/sha, tar/dd/gzip caps, curl/ping, HTTP chunked/H2 POST, DNS CNAME/dnsflush, blob-aware cp, blobstore reclaim, Files DnD ghost, Help/theme UX, Browser polish, Notepad/Monitor export, Agent mem.store/peakvec.query, ctr WORKDIR/Range, heap/sched starvation counters.

**Pass 118 (Wave 5 lock-in):** `smoke-cli.sh` gates for Passes 99–117 markers, ROADMAP/CHANGELOG sync, security-checklist CI note — Wave 5 release acceptance.

## Enhancement wave 6 (Passes 119–138) — shipped

**Shipped on `main` (Passes 119–137):** join/comm depth, awk/sed/jq depth, shell read/unset + pipe caps, fmt/column/expand/unexpand, stat/du/df depth, ln/chmod/install depth, ss netstat-lite, tlsinfo HSTS/resume, nc timeout/listen, zip/tar caps/listing, Start/taskbar UX, window snap HUD, clipboard picker, Files multi-select, Images viewer, Terminal scrollback search, Settings a11y/scale, idle-lock minutes, Privacy grant list.

**Pass 138 (Wave 6 lock-in):** `smoke-cli.sh` gates for Passes 119–137 markers, ROADMAP/CHANGELOG sync, security-checklist CI note — Wave 6 release acceptance.

## Enhancement wave 7 (Passes 139–158) — shipped

**Shipped on `main` (Passes 139–157):** JS Promise allSettled/race; WebAPI fetch headers isolation; browser console/spinner honesty; DOM querySelector CSS depth; HTTP redirect/cookie jar; TLS alert hostname UX; PeakDisk autosave/dirty; PeakVec multi-bucket ANN; Agent GUI approval/export; policy catalog/deny reasons; ctr CMD/HEALTHCHECK + index listing; VFS errno paths; 64 KiB IO caps; timeout/watch yield; nproc/uptime/free depth; less/more pager; man/help categories; Net Explorer filter; Disks save UX; fuzz parser corpus gates.

**Pass 158 (Wave 7 lock-in):** `smoke-cli.sh` gates for Passes 139–157 markers, ROADMAP/CHANGELOG sync, security-checklist CI note — Wave 7 release acceptance.

## Enhancement wave 2 (Passes 39–58)

**Shipped on `main` (Passes 39–58):** `awk`/`sed`/`jq`/`zip` CLI depth; VFS `chmod`, symlinks, human-readable sizes; `uname` flags; `less` pager search; Images and Agent GUI UX; files clipboard path ops; browser tab chrome; HTTP/2 client, WebAPI stubs, TLS session resume, PeakVec query, and expanded agent tools catalog — plus S9 remainder smoke-cli lock-in.

**Pass 58 (S9 remainder):** `smoke-cli.sh` gates for Pass 39–49 + Wave 2 subsystem markers, host-test coverage, ROADMAP/CHANGELOG sync, security-checklist automation notes, fuzz corpus growth — Wave 2 release acceptance without a separate signing ceremony.

## Near term

### Raspberry Pi

- Complete **Pi 3** HDMI / USB HID / PeakFS persist acceptance from a clean checkout — [scripts/pi3-hw-checklist.md](../scripts/pi3-hw-checklist.md), [rpi.md](rpi.md)
  - Software preflight landed (HID wheel, FB NC pageflip map, Save UX, strict `smoke-aarch64`); **silicon boxes still open**
- Finish **USB LAN / GENET / RP1 GEM** datapaths and **SDIO Wi‑Fi** association (after hub path is silicon-validated)
- Pi 5 high MMIO / peri bring-up when mapped; xHCI rings still absent
- [x] Enable aarch64 **userspace ELF** (`eret` to EL0t) when ready
- [x] Deeper ring-3 `/bin/sh` ELF workload + per-process fds (`PROC_FD_MAX` 32)

### Security (Phase S remainder)

- [x] **S8:** verified boot, signed releases, A/B rollback — [verified-boot.md](verified-boot.md)
  - Signing ceremony (`sign-release.py` / `verify-release.py`) + A/B ESP sketch landed; loader-embedded verify still remaining
- [x] **S9-lite (Pass 38):** smoke-cli Pass 19–37 markers, host tests, checklist sync — CI `host-tests` job
- [x] **S9 remainder (Pass 58):** smoke-cli Wave 2 gates (Passes 39–49 + subsystem markers), fuzz corpus growth — CI `host-tests` job
- **S9 ceremony:** signed release ceremony on tagged builds (loader-embedded verify still remaining)

### Browser / JS

- Full **ring-3** script isolation (validated DOM/net handles)
- [x] ES modules / `async`/`await` depth + more public-site fixtures
- [x] **Interactive render bar (BR-1…14):** non-empty H2 bodies, external CSS, layout depth, images, forms/events, history — [browser-js.md](browser-js.md), [scripts/browser-render-checklist.md](../scripts/browser-render-checklist.md)

### Userspace & networking

- [x] Deeper ring-3 `/bin/sh` ELF workload + per-process fds (`PROC_FD_MAX` 32)
- [x] **virtio-net** (preferred over e1000 on QEMU) + richer socket API (`net_tcp_fd_peer` / `local` / `shutdown`)
- [x] TLS 1.2/1.3 client, WebPKI, HTTP/2 ALPN — [network.md](network.md)
- ECH: fail-closed when required without config (`tls_ech_*`); full HPKE outer/inner ClientHello still open

### Passes 35–37 (shipped)

- [x] **Pass 35:** heap fragmentation / OOM honesty, scheduler fairness in `ps`, usercopy edge hardening
- [x] **Pass 36:** PeakVec recall UX, blobstore integrity, stats CLI
- [x] **Pass 37:** keyboard repeat, mouse acceleration, console scrollback search

### Agent / storage

- Optional opt-in remote LLM over TLS (**never** default)
- PeakVec ANN when corpora grow
- VFS large-file back-end on blobstore (beyond PeakVec)

### Desktop GFX (only if needed)

Software FB compositor is in tree; stress bar: [scripts/gui-stress-checklist.md](../scripts/gui-stress-checklist.md). Defer unless Monitor/`compose_us` shows a new bottleneck:

- Hardware cursor plane
- Triple-buffer on x86
- Occlusion culling / Wayland / multi-monitor / GPU accel

## Explicitly deferred until after Pi 3 gate

Do not start these until [scripts/pi3-hw-checklist.md](../scripts/pi3-hw-checklist.md) HDMI/USB/PeakFS items are green on silicon:

- Exception recovery / fault containment beyond log+halt
- Pi 4/5 xHCI rings, GENET datapath, Wi‑Fi association, high-MMIO maps
- SMP (secondaries stay parked)
- S8 verified boot / signed release ceremony (partial: host sign/verify + A/B sketch; loader embed TBD)
- PeakDisk passphrase KDF (PEAKDSK3); PEAKDSK2 header-key retired on load
- USB LAN bulk datapath (needs silicon-proven hub path first)

## North star

```
userspace shell ──► peak-agent (local) ──► tools (fs/exec)
```

Primitives: **workspace**, **agent** (capability bits), **action log**, **session memory** under `/var/peak/`.

See also: [security-model.md](security-model.md), [agent-protocol.md](agent-protocol.md), [browser-js.md](browser-js.md), [rpi.md](rpi.md).
