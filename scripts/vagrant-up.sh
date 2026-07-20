#!/usr/bin/env bash
# Build Peak OS and bring it up via Vagrant (QEMU provider).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v vagrant >/dev/null 2>&1; then
  echo "Vagrant is not installed."
  echo "  brew install --cask vagrant"
  echo "  vagrant plugin install vagrant-qemu"
  echo ""
  echo "Falling back to QEMU directly..."
  exec ./scripts/run-qemu.sh
fi

make iso

# Peak OS has no SSH — vagrant up just boots the ISO graphically via QEMU.
export VAGRANT_DEFAULT_PROVIDER="${VAGRANT_DEFAULT_PROVIDER:-qemu}"

if ! vagrant plugin list 2>/dev/null | grep -q vagrant-qemu; then
  echo "Installing vagrant-qemu plugin..."
  vagrant plugin install vagrant-qemu || {
    echo "Could not install vagrant-qemu; using ./scripts/run-qemu.sh"
    exec ./scripts/run-qemu.sh
  }
fi

echo "Starting Peak OS via Vagrant (provider=qemu)..."
# Destroy any prior instance so the fresh ISO is used
vagrant destroy -f >/dev/null 2>&1 || true
exec vagrant up --provider=qemu
