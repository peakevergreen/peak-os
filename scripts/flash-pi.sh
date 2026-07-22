#!/usr/bin/env bash
# Safely flash Peak OS image to a block device.
set -euo pipefail
DEVICE="${1:-}"
IMAGE="${2:-build/peak-os-rpi-arm64.img}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -z "$DEVICE" ]]; then
  echo "usage: $0 /dev/diskN [image]"
  echo "WARNING: this destroys all data on DEVICE."
  exit 2
fi
if [[ ! -f "$IMAGE" ]]; then
  echo "FAIL: image not found: $IMAGE"
  exit 1
fi
if [[ ! -e "$DEVICE" ]]; then
  echo "FAIL: device not found: $DEVICE"
  exit 1
fi

# Reject obviously dangerous targets
case "$DEVICE" in
  /dev/sda|/dev/nvme0n1|/dev/disk0|/dev/disk0s*)
    echo "FAIL: refusing system disk pattern $DEVICE"
    exit 1
    ;;
esac

if mount | grep -q "^$DEVICE"; then
  echo "FAIL: $DEVICE appears mounted; unmount first"
  exit 1
fi

SIZE=$(blockdev --getsize64 "$DEVICE" 2>/dev/null || diskutil info "$DEVICE" 2>/dev/null | awk -F'[()]' '/Disk Size/ {print $2}' | awk '{print $1}' || echo unknown)
echo "=========================================="
echo " DESTRUCTIVE WRITE"
echo "  device : $DEVICE"
echo "  size   : $SIZE"
echo "  image  : $IMAGE"
echo "=========================================="
read -r -p "Type the device path again to confirm: " confirm
if [[ "$confirm" != "$DEVICE" ]]; then
  echo "Aborted."
  exit 1
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  # macOS: rdisk for speed; diskutil unmountDisk
  RAW="${DEVICE/disk/rdisk}"
  diskutil unmountDisk "$DEVICE" || true
  sudo dd if="$IMAGE" of="$RAW" bs=4m status=progress
  sync
  if [[ "${PEAK_FLASH_VERIFY:-}" == "1" ]]; then
    echo "Verifying (read-back hash of first image-sized chunk)..."
    IMG_SZ=$(stat -f%z "$IMAGE")
    sudo dd if="$RAW" bs=1m count=$(( (IMG_SZ + 1048575)/1048576 )) 2>/dev/null | \
      head -c "$IMG_SZ" | shasum -a 256
    shasum -a 256 "$IMAGE"
  fi
else
  sudo dd if="$IMAGE" of="$DEVICE" bs=4M status=progress conv=fsync
  sync
fi
echo "Flash complete. Insert SD into Pi 3, connect HDMI + USB keyboard/mouse, power on."
