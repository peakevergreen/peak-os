# Verified boot and releases

## Goals

1. Detect tampering of kernel / loaders before execution
2. Publish reproducible release manifests with detached signatures
3. UEFI Secure Boot enrollment for `BOOTX64.EFI`
4. A/B ESP slots with automatic rollback (UEFI path)
5. Honest BIOS limits: software verify only; replaceable loader is not a hardware root

## Manifest

`scripts/mkmanifest.py` writes `build/SHA256SUMS` covering:

- `kernel.elf` / `kernel8.img`
- `peak-bios.bin`
- `BOOTX64.EFI`
- `peak-os.iso` / `peak-os-rpi-arm64.img`

CI runs `mkmanifest.py` after the x86 ISO build and requires `build/SHA256SUMS` (Pi images also write sums via `mkpiimg.py`). Detached Ed25519 signatures are the next wire-up (`*.sig` beside each artifact).

## Loader verify (software)

Both [boot/bios/main32.c](../boot/bios/main32.c) and [boot/uefi/efi_main.c](../boot/uefi/efi_main.c) should call a Peak verify helper before `boot_elf_load` once the signature primitive lands. Fail closed on mismatch.

Until then, purity + smoke + SHA256SUMS are the release gates.

## UEFI Secure Boot

1. Sign `BOOTX64.EFI` with a test PK/KEK/db
2. Enroll keys in OVMF vars
3. `PEAK_FIRMWARE=uefi PEAK_SECURE_BOOT=1 ./scripts/smoke-qemu.sh`

## A/B updates

ESP layout (planned):

```
\EFI\PEAK\A\KERNEL.ELF
\EFI\PEAK\B\KERNEL.ELF
\EFI\PEAK\boot.slot   # active + attempt counter
```

Write inactive → flush → flip → boot → mark success; else roll back.

## Recovery

Hybrid ISO remains the recovery media. Optional recovery EFI entry may reset PeakFS header only.

See [security-model.md](security-model.md).
