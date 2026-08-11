#!/usr/bin/env bash
# Shared apt install profiles for GitHub Actions + GitLab CI.
# Usage: ./scripts/ci-install-deps.sh <profile>
# Profiles: iso | rpi | host | smoke-qemu | smoke-peakfs | smoke-aarch64
set -euo pipefail

PROFILE="${1:?usage: ci-install-deps.sh <profile>}"

export DEBIAN_FRONTEND=noninteractive

# LLVM packages often install tools under /usr/lib/llvm-*/bin only.
for d in /usr/lib/llvm-*/bin; do
  if [ -d "$d" ]; then
    PATH="$d:$PATH"
  fi
done
export PATH

# Prefer non-interactive sudo (self-hosted pe-ci). Never hang on a password prompt.
root_cmd() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    sudo -n "$@"
  else
    return 1
  fi
}

have_cmds() {
  local c
  for c in "$@"; do
    command -v "$c" >/dev/null 2>&1 || return 1
  done
  return 0
}

apt_install() {
  if root_cmd apt-get update -qq && root_cmd apt-get install -y -qq "$@"; then
    return 0
  fi
  echo "ci-install-deps: apt install failed (need passwordless sudo or preinstalled tools)" >&2
  return 1
}

# Best-effort restore of cached .deb files into apt archives (no password prompt).
warm_apt_cache() {
  root_cmd mkdir -p /var/cache/apt/archives || true
  if [ -d "${HOME}/apt-archives" ] && [ "$(ls -A "${HOME}/apt-archives" 2>/dev/null || true)" ]; then
    root_cmd cp -a "${HOME}/apt-archives/." /var/cache/apt/archives/ || true
  fi
}

save_apt_cache() {
  mkdir -p "${HOME}/apt-archives"
  root_cmd cp -a /var/cache/apt/archives/*.deb "${HOME}/apt-archives/" 2>/dev/null || true
}

warm_apt_cache

case "$PROFILE" in
  iso)
    if have_cmds clang ld.lld llvm-objcopy xorriso make python3 rg qemu-system-x86_64; then
      echo "ci-install-deps: iso toolchain already present"
    else
      apt_install clang lld llvm xorriso make python3 ca-certificates \
        qemu-system-x86 ripgrep
    fi
    ;;
  rpi)
    if have_cmds clang ld.lld llvm-objcopy make python3 curl rg qemu-system-aarch64; then
      echo "ci-install-deps: rpi toolchain already present"
    else
      apt_install clang lld llvm make python3 ca-certificates curl \
        qemu-system-arm ripgrep
    fi
    ;;
  host)
    if have_cmds clang make python3 llvm-readelf xorriso rg; then
      echo "ci-install-deps: host toolchain already present"
    else
      apt_install clang make python3 llvm xorriso ripgrep
    fi
    ;;
  smoke-qemu)
    if have_cmds qemu-system-x86_64 make; then
      echo "ci-install-deps: smoke-qemu tools already present"
    else
      apt_install qemu-system-x86 ovmf make
    fi
    ;;
  smoke-peakfs)
    if have_cmds qemu-system-x86_64 clang ld.lld llvm-objcopy xorriso make; then
      echo "ci-install-deps: smoke-peakfs tools already present"
    else
      apt_install qemu-system-x86 clang lld llvm xorriso make
    fi
    ;;
  smoke-aarch64)
    if have_cmds qemu-system-aarch64; then
      echo "ci-install-deps: smoke-aarch64 tools already present"
    else
      apt_install qemu-system-arm
    fi
    ;;
  *)
    echo "unknown profile: $PROFILE" >&2
    exit 1
    ;;
esac

save_apt_cache
