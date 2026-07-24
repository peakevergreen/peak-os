#!/usr/bin/env bash
# Validate host toolchain for Peak OS builds.
set -euo pipefail
ARCH="${1:-x86_64}"
PLATFORM="${2:-pc}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail=0

need() {
  local c="$1"
  if command -v "$c" >/dev/null 2>&1; then
    echo "ok: $c ($(command -v "$c"))"
  else
    echo "FAIL: missing $c"
    fail=1
  fi
}

echo "==> Peak OS doctor ARCH=$ARCH PLATFORM=$PLATFORM"

# Homebrew LLVM (llvm-objcopy / ld.lld) is often not on default PATH.
for d in /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin; do
  if [[ -d "$d" ]]; then
    export PATH="$d:$PATH"
  fi
done

need clang
need ld.lld
need llvm-objcopy
need make
need python3

if [[ "$ARCH" == "x86_64" ]]; then
  need xorriso
  need qemu-system-x86_64
  # lld-link is required for Peak UEFI (BOOTX64.EFI)
  if command -v lld-link >/dev/null 2>&1; then
    echo "ok: lld-link ($(command -v lld-link))"
  else
    echo "FAIL: missing lld-link (UEFI link)"
    fail=1
  fi
elif [[ "$ARCH" == "aarch64" ]]; then
  need qemu-system-aarch64
  echo "==> Probe aarch64 freestanding compile"
  TMP=$(mktemp /tmp/peak-doctor-XXXX.c)
  echo 'void _start(void){}' >"$TMP"
  if clang -target aarch64-unknown-none-elf -ffreestanding -c "$TMP" -o /tmp/peak-doctor.o 2>/dev/null; then
    echo "ok: aarch64-unknown-none-elf clang target"
  else
    echo "FAIL: clang cannot target aarch64-unknown-none-elf"
    fail=1
  fi
  rm -f "$TMP" /tmp/peak-doctor.o
  if qemu-system-aarch64 -machine help 2>/dev/null | grep -q virt; then
    echo "ok: QEMU virt machine"
  else
    echo "FAIL: QEMU missing virt machine"
    fail=1
  fi
  if qemu-system-aarch64 -machine help 2>/dev/null | grep -q raspi3b; then
    echo "ok: QEMU raspi3b machine (optional board smoke)"
  else
    echo "note: QEMU raspi3b not available (optional)"
  fi
fi

if [[ "$fail" -ne 0 ]]; then
  echo "doctor FAILED"
  echo "Run scripts/setup-mac.sh (macOS) or install clang/lld/qemu/python3 (Linux)."
  exit 1
fi
echo "doctor passed"
