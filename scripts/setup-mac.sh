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

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -d third_party/limine/.git ]]; then
  echo "==> Fetching Limine binary release"
  mkdir -p third_party
  rm -rf third_party/limine
  git clone https://github.com/limine-bootloader/limine.git \
    --branch=v8.6.1-binary --depth=1 third_party/limine
fi

echo "==> Building Limine host tool"
make -C third_party/limine

if command -v vagrant >/dev/null 2>&1; then
  echo "==> Vagrant found — installing vagrant-qemu plugin (optional)"
  vagrant plugin install vagrant-qemu || true
else
  echo "==> Vagrant not installed (optional)."
  echo "    brew install --cask vagrant"
  echo "    then: vagrant plugin install vagrant-qemu"
fi

# Ensure LLVM tools are preferred when Homebrew llvm is keg-only
if [[ -d /opt/homebrew/opt/llvm/bin ]]; then
  echo ""
  echo "Add LLVM to your PATH for this session:"
  echo '  export PATH="/opt/homebrew/opt/llvm/bin:$PATH"'
fi

echo ""
echo "Setup complete. Next:"
echo "  make iso"
echo "  ./scripts/run-qemu.sh"
echo "  # or: ./scripts/vagrant-up.sh"
