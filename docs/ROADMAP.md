# Peak OS roadmap — what’s next

Direction: stay on the from-scratch kernel. Agents, tools, and project context are **system primitives**. Inference stays an **in-guest** planner (no host LLM bridge by default).

Shipped baseline and history: [CHANGELOG.md](../CHANGELOG.md). Architecture: [ARCHITECTURE.md](../ARCHITECTURE.md).

## Near term

### Raspberry Pi

- Complete **Pi 3** HDMI / USB HID / PeakFS persist acceptance from a clean checkout — [scripts/pi3-hw-checklist.md](../scripts/pi3-hw-checklist.md), [rpi.md](rpi.md)
- Finish **USB LAN / GENET / RP1 GEM** datapaths and **SDIO Wi‑Fi** association (after hub path is silicon-validated)
- Pi 5 high MMIO / peri bring-up when mapped; xHCI rings still absent
- Enable aarch64 **userspace ELF** (`eret` entry) when ready

### Security (Phase S remainder)

- **S8:** verified boot, signed releases, A/B rollback — [verified-boot.md](verified-boot.md)
- **S9:** release acceptance beyond CI smoke (signed artifacts, continuous fuzz corpus)

### Browser / JS

- Full **ring-3** script isolation (validated DOM/net handles)
- ES modules / `async`/`await` depth + more public-site fixtures

### Userspace & networking

- Deeper ring-3 `/bin/sh` ELF workload + per-process fds
- **virtio-net** (replace e1000) + richer socket API

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
- S8 verified boot / signed release ceremony
- PeakDisk passphrase KDF (header-key remains experimental)
- USB LAN bulk datapath (needs silicon-proven hub path first)

## North star

```
userspace shell ──► peak-agent (local) ──► tools (fs/exec)
```

Primitives: **workspace**, **agent** (capability bits), **action log**, **session memory** under `/var/peak/`.

See also: [security-model.md](security-model.md), [agent-protocol.md](agent-protocol.md), [browser-js.md](browser-js.md), [rpi.md](rpi.md).
