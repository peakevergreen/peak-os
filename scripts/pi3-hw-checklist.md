# Pi 3 hardware acceptance checklist

Primary physical gate for Peak OS ARM64. Use a freshly built image from a clean checkout.

Code prerequisites (landed in-tree for this gate):
- [x] DWC2 hub enum + split xfer + hotplug poll (`kernel/drivers/usb/dwc2.c`)
- [x] PeakDisk atomic publish (payload then header) + SDHCI CMD13 flush
- [x] QEMU aarch64 smoke markers + PeakFS x86 roundtrip smoke

## Prepare

- [ ] `make ARCH=aarch64 doctor`
- [ ] `make ARCH=aarch64 pi-image`
- [ ] `make ARCH=aarch64 pi-image-check`
- [ ] Verify `build/SHA256SUMS` matches `build/peak-os-rpi-arm64.img`
- [ ] Flash with `make ARCH=aarch64 flash-pi DEVICE=…` or Imager/Etcher/`dd`

## HDMI + USB (release-blocking)

- [ ] Pi 3B / 3B+ (or 3A+/Zero 2 W + powered hub): HDMI + wired USB keyboard + mouse attached
- [ ] Boots to console then desktop without host intervention
- [ ] Keyboard modifiers and typing work in shell and GUI
- [ ] Mouse buttons and wheel work; pointer moves
- [ ] Hub enumeration / hotplug (unplug-replug) recovers
- [ ] EDID path works; if not, `hdmi_safe=1` in `config.txt` recovers a usable mode

## Persistence + power

- [ ] Writable PeakFS survives reboot (save disk → reboot → data present)
- [ ] Clean reboot/shutdown does not corrupt PeakFS envelope (`PEAKDSK1`/`PEAKDSK2` at partition LBA1; image also has `PEAKFS01` at partition start from `mkpiimg`)

## Headless

- [ ] With HDMI disconnected, UART console still boots (no hang on missing FB)
- [ ] Shell usable over serial

## Optional same image

- [ ] Zero 2 W / Pi 4 / Pi 5: UART + FB + SD at minimum; HID/Ethernet/Wi-Fi per [docs/rpi.md](../docs/rpi.md) matrix

## QEMU pre-check (not a silicon substitute)

- [ ] `make ARCH=aarch64 smoke-aarch64` reaches `Boot complete` / `peak:/`
- [ ] `make smoke-peakfs` (x86 IDE) two-pass save/restore
