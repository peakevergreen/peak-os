# Contributing to Peak OS

## Setup (macOS)

```bash
./scripts/setup-mac.sh
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
make doctor
make iso
./scripts/run-qemu.sh
# Raspberry Pi image:
make ARCH=aarch64 doctor
make ARCH=aarch64 pi-image
```

Linux: Clang/LLD with `aarch64-unknown-none-elf`, `llvm-objcopy`, Make, Python 3, QEMU (x86_64 + aarch64), `xorriso` (ISO).

## Checks before a merge request

**Default GitHub CI** (`.github/workflows/ci.yml`) runs roughly:

- `make doctor` / ISO + Pi image builds
- `make test-host`
- `./scripts/purity-check.sh`
- `make smoke-bios`
- `make ARCH=aarch64 smoke-aarch64`

**Local / pre-release** (not in default CI):

```bash
make smoke-uefi          # needs OVMF (Homebrew qemu)
make ARCH=aarch64 pi-image pi-image-check
./scripts/smoke-cli.sh
```

Pi 3 hardware (release gate): flash `build/peak-os-rpi-arm64.img`, verify HDMI desktop + USB keyboard/mouse, PeakFS persist, headless UART. See [docs/rpi.md](docs/rpi.md) and [scripts/pi3-hw-checklist.md](scripts/pi3-hw-checklist.md).

Desktop GFX stress: [scripts/gui-stress-checklist.md](scripts/gui-stress-checklist.md).

## Layout

| Path | Role |
|------|------|
| `boot/` | Peak BIOS + UEFI + `boot/rpi` shim |
| `kernel/arch/` | x86_64 and aarch64 CPU backends |
| `kernel/platform/` | `pc` and `rpi` board code |
| `kernel/` | Shared kernel, net, GUI, builtins |
| `kernel/agent.c` | In-guest peak-agent (local planner) |
| `scripts/` | QEMU, SD image, flash, purity helpers |
| `docs/` | Roadmap, CLI, network, containers, Pi |

Use conventional commits (`feat`, `fix`, `chore`, `docs`, `refactor`, `test`).
Public CI is GitHub Actions (`.github/workflows/ci.yml`); a `.gitlab-ci.yml` is kept in sync for GitLab mirrors.

Please follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report security issues per [SECURITY.md](SECURITY.md).

License: MIT ([LICENSE](LICENSE)). Keep the copyright notice when redistributing Peak-authored code. Raspberry Pi boot firmware is a documented binary exception — see [docs/rpi.md](docs/rpi.md).
