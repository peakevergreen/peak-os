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

CI runs `mkmanifest.py` after the x86 ISO build and requires `build/SHA256SUMS` (Pi images also write sums via `mkpiimg.py`).

## Signing ceremony (S8)

```bash
make iso                                    # or pi-image
python3 scripts/mkmanifest.py               # refresh SHA256SUMS if needed
python3 scripts/sign-release.py             # Ed25519 → build/SHA256SUMS.sig
PEAK_RELEASE_PUB=build/peak-release.pub \
  python3 scripts/verify-release.py         # CI / release gate
```

- Private key: `build/peak-release.key` (Ed25519) or `build/peak-release.hmac-key` (HMAC fallback on older OpenSSL); never commit
- Detached seal: `build/SHA256SUMS.sig` (+ `SHA256SUMS.sig.type`)
- Prefer OpenSSL 3 Ed25519 when available; HMAC-SHA256 is an acceptable host seal until loader embed lands

Optional CI step: after `SHA256SUMS` exists, run `verify-release.py` when `PEAK_RELEASE_PUB` is configured in secrets.

## Loader verify (software)

Both [boot/bios/main32.c](../boot/bios/main32.c) and [boot/uefi/efi_main.c](../boot/uefi/efi_main.c) call `boot_verify_kernel()` **before** `boot_elf_load`. The helper:

1. Computes SHA256 over the loaded kernel image in memory (`boot/common/sha256.c`).
2. Looks for an expected digest in `peak.conf` (`kernel_sha256=<64 hex>`) and/or a `SHA256SUMS` stub on the boot volume (`/boot/SHA256SUMS` on ISO, `\EFI\PEAK\SHA256SUMS` on ESP).
3. **Fail closed** when `verify_required=1` in `peak.conf` and no matching digest is present.

Default installs keep `verify_required` off and log `verify: skip (no expected digest)` — behavior unchanged. Release ISOs may ship `build/SHA256SUMS` (from `mkmanifest.py`) under `/boot/` when present at `make iso` time.

Host tests in `tests/host/test_boot.c` cover the digest helper and verify policy.

Until Ed25519/HMAC is embedded in the loader, detached `SHA256SUMS.sig` verification remains a host-side release gate (`verify-release.py`). BIOS software verify is not a hardware root of trust.

## UEFI Secure Boot

1. Sign `BOOTX64.EFI` with a test PK/KEK/db
2. Enroll keys in OVMF vars
3. `PEAK_FIRMWARE=uefi PEAK_SECURE_BOOT=1 ./scripts/smoke-qemu.sh`

## A/B updates

ESP layout (sketch via `scripts/peak-ab-esp.sh`):

```
\EFI\PEAK\A\KERNEL.ELF
\EFI\PEAK\B\KERNEL.ELF
\EFI\PEAK\boot.slot   # active + attempt counter + good flag
```

Write inactive → flush → flip → boot → mark `good=1`; else roll back on attempt budget.

## Recovery

Hybrid ISO remains the recovery media. Optional recovery EFI entry may reset PeakFS header only.

See [security-model.md](security-model.md).
