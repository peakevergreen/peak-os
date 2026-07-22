#!/usr/bin/env bash
# Shared apt install profiles for GitHub Actions + GitLab CI.
# Usage: ./scripts/ci-install-deps.sh <profile>
# Profiles: iso | rpi | host | smoke-qemu | smoke-peakfs | smoke-aarch64
set -euo pipefail

PROFILE="${1:?usage: ci-install-deps.sh <profile>}"

export DEBIAN_FRONTEND=noninteractive

apt_install() {
  if command -v sudo >/dev/null 2>&1 && [ "$(id -u)" -ne 0 ]; then
    sudo apt-get update -qq
    sudo apt-get install -y -qq "$@"
  else
    apt-get update -qq
    apt-get install -y -qq "$@"
  fi
}

case "$PROFILE" in
  iso)
    apt_install clang lld llvm xorriso make python3 ca-certificates \
      qemu-system-x86 ripgrep
    ;;
  rpi)
    apt_install clang lld llvm make python3 ca-certificates curl \
      qemu-system-arm ripgrep
    ;;
  host)
    apt_install clang make python3 llvm xorriso ripgrep
    ;;
  smoke-qemu)
    apt_install qemu-system-x86 ovmf make
    ;;
  smoke-peakfs)
    apt_install qemu-system-x86 clang lld llvm xorriso make
    ;;
  smoke-aarch64)
    apt_install qemu-system-arm
    ;;
  *)
    echo "unknown profile: $PROFILE" >&2
    exit 1
    ;;
esac
