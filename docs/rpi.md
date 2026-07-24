# Peak OS on Raspberry Pi (ARM64)

Flash-and-boot support for Raspberry Pi ARM64 boards. Primary physical gate: **Raspberry Pi 3** with HDMI + USB keyboard/mouse.

## Supported boards

| Family | SoC | Notes |
|--------|-----|--------|
| Pi 3B / 3B+ / 3A+, Zero 2 W, CM3 / CM3+ | BCM2837 | Primary hardware gate; DWC2 hub+HID; USB LAN bulk bind for SMSC VID (ready only after MAC read) |
| Pi 4B / 400, CM4 / CM4S | BCM2711 | Core platform support; PCIe/VL805 xHCI and GENET are staged |
| Pi 5 / 500 / 500+, CM5 | BCM2712 + RP1 | Core platform support; RP1 PCIe, USB, and Ethernet are staged |

One SD image boots all listed models via device tree. No interactive installer.

## Build (host)

```bash
./scripts/setup-mac.sh   # or Linux: clang/lld, qemu-system-aarch64, python3
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"   # macOS Homebrew LLVM
make ARCH=aarch64 doctor
make ARCH=aarch64 pi-image
make ARCH=aarch64 pi-image-check
```

Artifact: `build/peak-os-rpi-arm64.img` (FAT boot + PeakFS partition + `SHA256SUMS`).

Firmware (VideoCore `bootcode.bin` / `start*.elf` / DTBs) is fetched by `make firmware-fetch` into `third_party/rpi-firmware` (gitignored). See [firmware policy](#firmware-policy).

## Flash

```bash
# Inspect first — refuses mounted/system disks; requires typed confirmation
make ARCH=aarch64 flash-pi DEVICE=/dev/diskN

# Or: Raspberry Pi Imager / Balena Etcher / dd
#   sudo dd if=build/peak-os-rpi-arm64.img of=/dev/rdiskN bs=4m status=progress
```

## First boot acceptance (Pi 3 gate)

1. Insert microSD; attach HDMI, wired USB keyboard and mouse (use a powered hub on 3A+/Zero 2 W).
2. Power on and verify console, software-FB desktop, responsive shell, and polled boot-protocol keyboard/mouse input.
3. Save PeakFS from the desktop/CLI; reboot and verify persistence.
4. Headless: UART on GPIO 14/15 (`enable_uart=1` in `config.txt`); missing HDMI must not hang boot.

Safe HDMI (`boot/rpi/config.txt`): uncomment `hdmi_safe=1` if EDID fails.
These remain hardware acceptance checks, not completed hardware claims.

## Emulation

```bash
make ARCH=aarch64 run-aarch64-virt   # QEMU virt (CI)
make ARCH=aarch64 smoke-aarch64
# Optional board-ish: qemu-system-aarch64 -M raspi3b -kernel build/aarch64/kernel8.img …
```

QEMU does **not** substitute for Pi 3 HDMI/USB hardware sign-off. No Pi 5 machine in QEMU.

### Display notes

- On boot, serial logs `display: vblank=0/1 pageflip=0/1`.
- **Pi pageflip** (`pageflip=1`) is the real double-buffer flip path (mailbox virtual offset).
- On x86 QEMU, `vblank=1` only means the VGA IS1 retrace bit toggled during probe — it is **not** proof of tear-free hardware. Soft damage presents never wait on VBlank; full presents may.

## Firmware policy

| Blob | Role | Policy |
|------|------|--------|
| VideoCore `bootcode.bin`, `start*.elf`, `fixup*.dat`, DTBs | Boot | Pinned fetch + checksums; not vendored in git |
| CYW434xx Wi-Fi (and optional BT) | Runtime | Documented binary exception; load from bootfs |

PeakOS kernel and Peak-authored drivers remain open source.

## Capability matrix (v1)

| Feature | Pi 3 | Pi 4 | Pi 5 |
|---------|------|------|------|
| UART / timer / MMU | yes | yes (GIC timer ack) | MMU yes; SoC MMIO above 4 GiB not mapped yet |
| Software FB desktop | implemented; silicon sign-off pending | implemented; silicon sign-off pending | deferred until high MMIO mapped |
| SD PeakFS partition | implemented; silicon sign-off pending | implemented; unverified | deferred (SDHCI base >4 GiB) |
| USB HID | hub+HID enum/split/hotplug; silicon sign-off pending | unavailable: PCIe/xHCI rings missing | unavailable: RP1 + high MMIO unmapped |
| Ethernet | SMSC USB LAN bulk bind; silicon verify pending | unavailable: GENET rings/PHY missing | unavailable: RP1 GEM stub |
| Wi-Fi | unavailable: SDIO/firmware loading stub | unavailable: SDIO/firmware loading stub | unavailable: SDIO/firmware loading stub |
| Audio | unavailable: beep is a no-op | unavailable: beep is a no-op | unavailable: beep is a no-op |
| SMP | secondary CPUs parked | parked | parked |
| GPU acceleration | unavailable; software FB only | unavailable; software FB only | unavailable; software FB only |

Boot page tables identity-map 0–4 GiB only. BCM2712 peripheral windows sit above 4 GiB, so Pi 5 platform init skips GPIO/SDHCI/USB/net until those ranges are mapped (`platform_mmio_mapped()` is 0). Pi 4/5 PCIe discovery and xHCI controller reset remain diagnostic staging only. Stub Ethernet and Wi-Fi implementations are not registered as ready network devices.

QEMU `-M raspi3b` is expected to reach boot markers (`Boot complete` / `peak:/`) with mailbox FB and DWC2 host init; CI `smoke-aarch64` enforces those markers (not bare `Pk`). Use real Pi 3 silicon for HDMI + USB keyboard/mouse acceptance ([scripts/pi3-hw-checklist.md](../scripts/pi3-hw-checklist.md)).

aarch64 userspace ELF execution is intentionally gated until a real `eret` entry path exists in `kernel/elf.c`; ring-3 binaries are not launched on Pi builds yet.

## Layout

| Path | Role |
|------|------|
| `boot/rpi/` | Boot shim, `config.txt` |
| `kernel/arch/aarch64/` | EL1, MMU, exceptions, timer, FPSIMD |
| `kernel/platform/rpi/` | DT/SoC, UART, mailbox, SDHCI, GPIO, PCIe glue |
| `kernel/drivers/usb/` | USB core, DWC2, xHCI |
| `scripts/mkpiimg.py` | Reproducible SD image |
| `scripts/flash-pi.sh` | Guarded flasher |

## CM carriers

Compute Modules discover peripherals from the device tree supplied by the carrier’s firmware. Same kernel image; ensure the matching DTB is present on the FAT partition.
