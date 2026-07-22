#!/usr/bin/env bash
# Ensure Peak OS has no Limine runtime, COM2/COM3 bridges, or host proxy tools.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail=0

if ! command -v rg >/dev/null 2>&1; then
  echo "FAIL: ripgrep (rg) is required for purity-check"
  exit 1
fi

check_absent_code() {
  local pat="$1"
  local msg="$2"
  set +e
  rg -n -g '!third_party/**' -g '!build/**' -g '!.git/**' \
      -g '*.c' -g '*.h' -g '*.S' -g '*.ld' -g 'Makefile' \
      "$pat" . >/tmp/peak-purity.out 2>/dev/null
  local rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    echo "FAIL: $msg"
    cat /tmp/peak-purity.out | head -20
    fail=1
  elif [[ $rc -eq 1 ]]; then
    echo "ok: $msg"
  else
    echo "FAIL: rg error while checking: $msg"
    fail=1
  fi
}

check_absent_code 'limine\.h|#include <limine|LIMINE_|third_party/limine' 'no Limine in build/sources'
check_absent_code 'peak-host-proxy|0x2F8|0x3E8|agent_poll_host' 'no host bridge code'
check_absent_code 'COM2|COM3' 'no COM2/COM3 in code'

if [[ -d third_party/limine ]]; then
  echo "FAIL: third_party/limine directory still present"
  fail=1
else
  echo "ok: no third_party/limine directory"
fi

for f in scripts/peak-host-proxy.py scripts/peak-ssh scripts/peak-scp limine.conf docs/ssh.md; do
  if [[ -e "$f" ]]; then
    echo "FAIL: stale file $f"
    fail=1
  else
    echo "ok: removed $f"
  fi
done

if [[ -f build/peak-os.iso ]]; then
  if xorriso -indev build/peak-os.iso -find / -name '*limine*' 2>/dev/null | grep -qi limine; then
    echo "FAIL: ISO contains limine artifacts"
    fail=1
  else
    echo "ok: ISO has no limine paths"
  fi
  if ! xorriso -indev build/peak-os.iso -find / -name 'peak-bios.bin' 2>/dev/null | grep -q peak-bios; then
    echo "FAIL: ISO missing peak-bios.bin"
    fail=1
  else
    echo "ok: ISO contains peak-bios.bin"
  fi
  if ! xorriso -indev build/peak-os.iso -find / -name 'BOOTX64.EFI' 2>/dev/null | grep -q BOOTX64; then
    echo "FAIL: ISO missing BOOTX64.EFI"
    fail=1
  else
    echo "ok: ISO contains BOOTX64.EFI"
  fi
else
  echo "note: skip ISO purity (build/peak-os.iso absent)"
fi

# aarch64 Pi image (optional artifact)
if [[ -f build/peak-os-rpi-arm64.img ]]; then
  if ! python3 scripts/pi-image-check.py build/peak-os-rpi-arm64.img; then
    echo "FAIL: pi-image-check"
    fail=1
  else
    echo "ok: pi-image-check"
  fi
  if [[ -f build/aarch64/kernel8.img ]]; then
    echo "ok: kernel8.img present"
  else
    echo "FAIL: aarch64 kernel8.img missing beside Pi image"
    fail=1
  fi
else
  echo "note: skip Pi image purity (build/peak-os-rpi-arm64.img absent)"
fi

if [[ "$fail" -ne 0 ]]; then
  echo "purity check FAILED"
  exit 1
fi
echo "purity check passed"
