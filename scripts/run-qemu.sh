#!/usr/bin/env bash
# Boot Peak OS in QEMU (works on Apple Silicon via TCG).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${ROOT}/build/peak-os.iso"

if [[ ! -f "$ISO" ]]; then
  echo "ISO not found. Building..."
  make -C "$ROOT" iso
fi

QEMU="${QEMU:-qemu-system-x86_64}"
if ! command -v "$QEMU" >/dev/null 2>&1; then
  echo "qemu-system-x86_64 not found. Run ./scripts/setup-mac.sh"
  exit 1
fi

# Prefer Cocoa window on macOS; fall back to default display.
DISPLAY_ARGS=(-display cocoa)
if [[ "$(uname -s)" != "Darwin" ]]; then
  DISPLAY_ARGS=(-display gtk)
fi

echo "Launching Peak OS..."
echo "  CLI is on the framebuffer; serial is mirrored here."
echo "  Type 'help' or 'gui'. Quit QEMU with Ctrl-C / close window."

exec "$QEMU" \
  -machine q35 \
  -m 256M \
  -smp 1 \
  -cdrom "$ISO" \
  -boot d \
  -serial stdio \
  -no-reboot \
  "${DISPLAY_ARGS[@]}" \
  "$@"
