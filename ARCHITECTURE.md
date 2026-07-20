# Peak OS Architecture (v0.1.0-mvp baseline)

Peak OS is a from-scratch **x86_64** hobby kernel aimed at becoming an **AI-first developer OS**. This document describes the frozen MVP baseline.

## Boot path

```
QEMU (ISO) → Limine → kernel_entry (boot.c)
  → serial + framebuffer
  → PMM (Limine memmap + HHDM)
  → console
  → PIC / IDT / timer / keyboard / mouse
  → in-kernel shell (CLI) or desktop (gui)
```

- Bootloader: Limine (BIOS/UEFI hybrid ISO)
- Kernel link address: `0xffffffff80000000` ([linker.ld](linker.ld))
- Limine requests: framebuffer, HHDM, memmap, stack size ([kernel/boot.c](kernel/boot.c))

## CLI

- Framebuffer text console mirrored to COM1 serial ([kernel/console.c](kernel/console.c), [kernel/serial.c](kernel/serial.c))
- Interactive shell with builtins: `help`, `clear`, `echo`, `uname`, `uptime`, `mem`, `gui`, `reboot` ([kernel/shell.c](kernel/shell.c))
- PS/2 keyboard via IRQ1 ([kernel/keyboard.c](kernel/keyboard.c))

## Desktop

- Software compositor on Limine framebuffer ([kernel/gui/desktop.c](kernel/gui/desktop.c))
- Taskbar + clock, mouse cursor, draggable Terminal window
- Esc returns to fullscreen CLI

## Host launch

- [scripts/setup-mac.sh](scripts/setup-mac.sh) — brew deps + Limine
- [scripts/run-qemu.sh](scripts/run-qemu.sh) — primary runner
- [scripts/vagrant-up.sh](scripts/vagrant-up.sh) / [Vagrantfile](Vagrantfile) — QEMU provider wrapper

## Explicit MVP limits

No VFS, heap allocator beyond PMM pages, processes, ELF userspace, networking, or agent runtime at the tagged MVP. Those land in the AI-first roadmap ([docs/ROADMAP.md](docs/ROADMAP.md)).
