# Peak OS

From-scratch **x86_64** hobby operating system: Limine bootloader, freestanding C kernel, interactive CLI, and a simple framebuffer desktop. Not Linux-compatible (no Linux ABI or userspace apps).

## Features (MVP)

- Boots via Limine (BIOS/UEFI hybrid ISO)
- Framebuffer text console + serial mirror
- Interactive shell: `help`, `clear`, `echo`, `uname`, `uptime`, `mem`, `gui`, `reboot`
- Desktop: wallpaper, taskbar/clock, mouse cursor, draggable Terminal window
- Launch on macOS with QEMU or Vagrant→QEMU

## Quick start (MacBook)

```bash
./scripts/setup-mac.sh
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"   # if llvm is keg-only
make iso
./scripts/run-qemu.sh
```

In the guest:

1. Type `help` at the `peak>` prompt
2. Type `gui` for the desktop (Esc returns to CLI)
3. Close the QEMU window or Ctrl-C to quit

### Vagrant

```bash
brew install --cask vagrant
vagrant plugin install vagrant-qemu
./scripts/vagrant-up.sh
```

Peak OS has no SSH — Vagrant only wraps QEMU to boot `build/peak-os.iso`. If the plugin is missing, `vagrant-up.sh` falls back to `run-qemu.sh`.

## Layout

```
kernel/           freestanding kernel (C + asm)
  gui/            desktop compositor + font
scripts/          setup / QEMU / Vagrant helpers
third_party/limine/   Limine binary release
limine.conf       boot menu
linker.ld         higher-half kernel link script
Vagrantfile       QEMU provider wrapper
```

## Build requirements

- `clang` / `ld.lld` (Homebrew `llvm` + `lld`)
- `xorriso`
- `qemu` (`qemu-system-x86_64`)
- Limine under `third_party/limine` (fetched by setup)

```bash
make          # → build/peak-os.iso
make clean
make run      # iso + qemu
```

## Architecture notes

- Higher-half kernel at `0xffffffff80000000`
- Limine requests: framebuffer, HHDM, memmap, stack size
- PIC + IDT; PIT timer; PS/2 keyboard & mouse
- Physical page allocator (bitmap) over Limine usable memory
- GUI is a software compositor on the Limine framebuffer (no GPU driver)

## Limits (intentional for MVP)

No filesystem, networking, ELF userspace, SMP, or Linux syscalls. Apple Silicon runs the x86_64 guest under QEMU TCG (correct, not native HVF).
