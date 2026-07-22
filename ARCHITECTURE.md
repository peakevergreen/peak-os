# Peak OS architecture

Freestanding kernel with Peak-authored boot on **x86_64** and **aarch64 (Raspberry Pi)**.

## Boot

### x86_64

1. **BIOS:** El Torito `boot/peak-bios.bin` → E820 + VBE + ISO9660 kernel load → long mode → `kernel_entry(BootInfo)`.
2. **UEFI:** `EFI/BOOT/BOOTX64.EFI` → GOP + file load → ExitBootServices → same kernel entry.

### aarch64 (Raspberry Pi)

1. VideoCore firmware loads FAT `kernel8.img` + DTB (`config.txt`).
2. `boot/rpi` shim: EL2→EL1 if needed, 4K MMU + HHDM, mailbox/SimpleFB, BootInfo v4 (`dtb_phys`).
3. `kernel_entry(BootInfo)` — secondary CPUs parked (uniprocessor v1).

### BootInfo

Canonical ABI: `boot/include/peak_boot.h` (single definition). Kernel code includes
`kernel/include/peak_boot.h`, which is a thin wrapper that `#include`s the boot header
so the layout cannot drift.

`peak_bootinfo` v4: magic/version, HHDM, framebuffer, mmap, kernel/stack, optional `dtb_phys` / `dtb_size`, `peak_net_config`, boot entropy seed + quality flags, optional KASLR slide pages. `rsdp_phys` unused on Pi.

Higher-half kernel at `0xffffffff80000000`; HHDM at `0xffff800000000000`.

Linker scripts: `kernel/arch/{x86_64,aarch64}/linker.ld` (Makefile `-T`). Root `linker.ld` is a deprecated stub only.

## HAL

| Layer | Role |
|-------|------|
| `kernel/arch/{x86_64,aarch64}/` | CPU, IRQ, timer, FPU, context switch |
| `kernel/platform/{pc,rpi}/` | Board bring-up, power, DMA cache ops |
| `blockdev` / `netdev` / `irq` | Storage, NIC, interrupt registration |

x86 leaf sources (`gdt`, `idt`, `pic`, `ata`, FPU/RTC/sound, `isr.S`, `context.S`) live
under `kernel/arch/x86_64/`. Port I/O helpers are in `kernel/include/x86_io.h` (pulled in
by `util.h` on x86 only). Prefer new CPU-specific code under `kernel/arch/<triple>/`.

Empty placeholders `kernel/drivers/gpio/` and `kernel/drivers/sound/` await future
platform leaf drivers; GPIO/sound for Pi currently live under `kernel/platform/rpi/`.

Portable subsystems: `fb_*`, keyboard/mouse queues, `timer_ticks`, net above L2, VFS/PeakFS, GUI/JS.

## Kernel

Framebuffer console (soft-fail if no FB), PMM/VMM/heap (PMM to 16 GiB), VFS + streamed PeakFS persist (ATA on PC, SDHCI partition on Pi), blobstore (block-backed objects + LRU cache), PeakVec, syscalls (`int 0x80` / `svc`), ELF builtins, scheduler, peak-agent, multi-window desktop.

## I/O (Pi)

- **BCM2837:** BCM local IRQs, PL011 UART, DWC2 HID with hub enum/split/hotplug, SDHCI (CMD13 flush), mailbox FB. USB LAN bulk: not ready.
- **BCM2711:** GIC, SDHCI; GENET and PCIe → VL805 xHCI are staged stubs (not registered ready).
- **BCM2712:** GIC; PCIe → RP1 USB/Ethernet/GPIO staged; high MMIO often unmapped.

Wi-Fi: SDIO path stub (not registered ready). DMA drivers use explicit cache maintenance on BCM. See [docs/rpi.md](docs/rpi.md).

## Serial

**COM1** (x86) or PL011/UART (Pi) for diagnostics. No host agent/ssh serial bridges. Release builds must not print seeds, canaries, or passphrases.

## Security / privacy

Kerckhoffs + capability least-privilege + Moving Target Defense. Local-first, no telemetry. See [docs/security-model.md](docs/security-model.md) and [docs/privacy.md](docs/privacy.md).

See [docs/ROADMAP.md](docs/ROADMAP.md), [docs/rpi.md](docs/rpi.md), [docs/from-scratch.md](docs/from-scratch.md).
