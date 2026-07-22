#!/usr/bin/env bash
# Install Peak OS build/run dependencies on macOS (Apple Silicon or Intel).
set -euo pipefail

echo "==> Peak OS setup (macOS)"

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required. Install from https://brew.sh"
  exit 1
fi

echo "==> Installing qemu, xorriso, llvm, lld, nasm, make"
brew install qemu xorriso llvm lld nasm make

echo "==> aarch64 Raspberry Pi builds use the same LLVM (clang -target aarch64-unknown-none-elf)"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Remove any leftover Limine vendor tree from older checkouts
if [[ -d third_party/limine ]]; then
  echo "==> Removing legacy third_party/limine"
  rm -rf third_party/limine
fi

if [[ -d /opt/homebrew/opt/llvm/bin ]]; then
  echo ""
  echo "Add LLVM to your PATH for this session:"
  echo '  export PATH="/opt/homebrew/opt/llvm/bin:$PATH"'
fi

echo ""
echo "Setup complete. Next:"
echo "  make doctor"
echo "  make iso"
echo "  ./scripts/run-qemu.sh"
echo "  PEAK_FIRMWARE=uefi ./scripts/run-qemu.sh"
echo ""
echo "Raspberry Pi (ARM64):"
echo "  make doctor ARCH=aarch64 PLATFORM=rpi"
echo "  make ARCH=aarch64 pi-image"
echo "  make ARCH=aarch64 pi-image-check"
echo "  make ARCH=aarch64 flash-pi DEVICE=/dev/diskN"
