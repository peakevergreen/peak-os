#!/usr/bin/env bash
# Run Peak OS aarch64 kernel under QEMU.
# Default: raspi3b (matches Pi load address 0x80000 + PL011).
# Override: PEAK_QEMU_MACHINE=virt for CI-style virt board (loader @ 0x80000).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${PEAK_KERNEL:-$ROOT/build/aarch64/kernel8.img}"
MACHINE="${PEAK_QEMU_MACHINE:-raspi3b}"
DTB="${PEAK_DTB:-$ROOT/third_party/rpi-firmware/bcm2710-rpi-3-b.dtb}"

if [[ ! -f "$KERNEL" ]]; then
  echo "Missing $KERNEL — build with: make ARCH=aarch64 kernel8"
  exit 1
fi

if [[ "$MACHINE" == "virt" ]]; then
  # Do NOT use -kernel here: QEMU virt defaults to ~0x40000000, but Peak
  # kernel8.img expects physical PC at 0x80000 (see start.S / linker.ld).
  exec qemu-system-aarch64 \
    -machine virt,gic-version=3 \
    -cpu cortex-a72 \
    -m 1024 \
    -nographic \
    -device loader,file="$KERNEL",addr=0x80000 \
    -device loader,addr=0x80000,cpu-num=0 \
    "$@"
fi

args=(-machine raspi3b -cpu cortex-a53 -m 1024 -nographic -kernel "$KERNEL")
if [[ -f "$DTB" ]]; then
  args+=(-dtb "$DTB")
fi
exec qemu-system-aarch64 "${args[@]}" "$@"
