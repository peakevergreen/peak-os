#!/usr/bin/env bash
# A/B ESP slot helper sketch for Peak S8 (UEFI path).
# Does not flash hardware — prepares directory layout for mkesp / installer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/esp-ab}"
SLOT="${PEAK_BOOT_SLOT:-A}"

mkdir -p "$OUT/EFI/PEAK/A" "$OUT/EFI/PEAK/B" "$OUT/EFI/BOOT"
KERNEL="${KERNEL:-$ROOT/build/x86_64/kernel.elf}"
EFI="${EFI:-$ROOT/build/x86_64/boot/BOOTX64.EFI}"

if [[ -f "$KERNEL" ]]; then
  cp -f "$KERNEL" "$OUT/EFI/PEAK/A/KERNEL.ELF"
  cp -f "$KERNEL" "$OUT/EFI/PEAK/B/KERNEL.ELF"
fi
if [[ -f "$EFI" ]]; then
  cp -f "$EFI" "$OUT/EFI/BOOT/BOOTX64.EFI"
fi

# boot.slot: active=<A|B> attempts=<n> good=<0|1>
printf 'active=%s\nattempts=0\ngood=0\n' "$SLOT" >"$OUT/EFI/PEAK/boot.slot"
echo "ok: A/B ESP sketch at $OUT (active=$SLOT)"
echo "    Update flow: write inactive → flush → flip active → boot → set good=1"
