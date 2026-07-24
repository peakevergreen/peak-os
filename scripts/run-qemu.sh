#!/usr/bin/env bash
# Boot Peak OS in QEMU (works on Apple Silicon via TCG).
#
#   PEAK_ZOOM=on|off     scale window to fit screen (default: on)
#   PEAK_FULLSCREEN=1    start fullscreen
#   PEAK_RES=WxH         rewrite boot/peak.conf resolution + rebuild ISO
#   PEAK_FIRMWARE=bios|uefi   firmware selection (default: bios)
#   PEAK_NET_MODE=user|bridged   networking (default: user)
#   PEAK_NET_IFACE=en0           host iface for bridged/vmnet (macOS)
#   PEAK_HTTP_PORT=8080          user-net: host port forwarded to guest :PORT
#
# LAN access (recommended): keep PEAK_NET_MODE=user. The guest's HTTP port is
# forwarded from this Mac, so other devices reach it at http://<mac-ip>:PORT/.
# vmnet-bridged gives the guest its own LAN IP but requires sudo and a wired
# interface. Do not ad-hoc add Apple's restricted networking entitlement.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${ROOT}/build/peak-os.iso"
PEAK_ZOOM="${PEAK_ZOOM:-on}"
PEAK_FULLSCREEN="${PEAK_FULLSCREEN:-0}"
PEAK_FIRMWARE="${PEAK_FIRMWARE:-bios}"
PEAK_NET_MODE="${PEAK_NET_MODE:-user}"

if [[ -n "${PEAK_RES:-}" ]]; then
  if [[ ! "$PEAK_RES" =~ ^[0-9]+x[0-9]+$ ]]; then
    echo "PEAK_RES must look like 1920x1080 (got: $PEAK_RES)"
    exit 1
  fi
  echo "==> Setting guest resolution to ${PEAK_RES}"
  conf="${ROOT}/boot/peak.conf"
  if grep -q '^resolution=' "$conf"; then
    sed -i.bak -E "s/^resolution=.*/resolution=${PEAK_RES}/" "$conf"
    rm -f "${conf}.bak"
  else
    echo "resolution=${PEAK_RES}" >> "$conf"
  fi
  make -C "$ROOT" iso
elif [[ ! -f "$ISO" ]]; then
  echo "ISO not found. Building..."
  make -C "$ROOT" iso
fi

QEMU="${QEMU:-qemu-system-x86_64}"
if ! command -v "$QEMU" >/dev/null 2>&1; then
  echo "qemu-system-x86_64 not found. Run ./scripts/setup-mac.sh"
  exit 1
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  COCOA_OPTS="zoom-to-fit=${PEAK_ZOOM}"
  if [[ "$PEAK_FULLSCREEN" == "1" || "$PEAK_FULLSCREEN" == "on" ]]; then
    COCOA_OPTS="${COCOA_OPTS},full-screen=on"
  fi
  DISPLAY_ARGS=(-display "cocoa,${COCOA_OPTS}")
else
  DISPLAY_ARGS=(-display gtk,zoom-to-fit=on)
fi

FW_DRIVE=""
if [[ "$PEAK_FIRMWARE" == "uefi" ]]; then
  OVMF_CODE="${OVMF_CODE:-}"
  if [[ -z "$OVMF_CODE" ]]; then
    for c in \
      /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
      /usr/share/OVMF/OVMF_CODE.fd \
      /usr/share/edk2/x64/OVMF_CODE.fd; do
      if [[ -f "$c" ]]; then OVMF_CODE="$c"; break; fi
    done
  fi
  if [[ -z "$OVMF_CODE" || ! -f "$OVMF_CODE" ]]; then
    echo "UEFI firmware not found. Set OVMF_CODE=..."
    exit 1
  fi
  FW_DRIVE="if=pflash,format=raw,readonly=on,file=${OVMF_CODE}"
  echo "  Firmware: UEFI (${OVMF_CODE})"
else
  echo "  Firmware: SeaBIOS (legacy)"
fi

NET_ARGS=()
if [[ "$PEAK_NET_MODE" == "bridged" ]]; then
  if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "PEAK_NET_MODE=bridged is currently wired for macOS vmnet-bridged."
    echo "On Linux, use a tap/bridge setup manually or stick to PEAK_NET_MODE=user."
    exit 1
  fi
  IFACE="${PEAK_NET_IFACE:-}"
  if [[ -z "$IFACE" ]]; then
    echo "PEAK_NET_MODE=bridged requires PEAK_NET_IFACE (e.g. en0)."
    echo "  networksetup -listallhardwareports"
    exit 1
  fi
  # Best-effort capability probe (some QEMU builds assert on -netdev help).
  if "$QEMU" -netdev help >/tmp/peak-qemu-netdev.txt 2>&1; then
    if ! grep -q 'vmnet-bridged' /tmp/peak-qemu-netdev.txt; then
      echo "This QEMU build lacks vmnet-bridged. Upgrade qemu via Homebrew, or use user-net."
      exit 1
    fi
  else
    echo "warning: could not probe QEMU netdev help; attempting vmnet-bridged anyway"
  fi
  # vmnet works either as root or with Apple's restricted entitlement. QEMU
  # does not have that entitlement, and adding it ad-hoc makes macOS kill the
  # process, so require root and leave the Homebrew binary untouched.
  if [[ "${EUID}" -ne 0 ]]; then
    echo "ERROR: bridged mode needs root to run macOS vmnet." >&2
    echo "  Retry: sudo PEAK_NET_MODE=bridged PEAK_NET_IFACE=${IFACE} $0" >&2
    exit 1
  fi
  IS_WIFI=0
  if networksetup -listallhardwareports 2>/dev/null \
       | grep -A2 'Hardware Port: Wi-Fi' | grep -qE "Device: ${IFACE}( |$)"; then
    IS_WIFI=1
  fi
  if [[ "$IS_WIFI" == "1" ]]; then
    echo "ERROR: ${IFACE} is Wi-Fi; vmnet-bridged stalls on this interface." >&2
    echo "  Use the LAN-reachable port forward instead:" >&2
    echo "    ./scripts/run-qemu.sh" >&2
    echo "  Then open http://<this-mac-lan-ip>:8080/ from another device." >&2
    exit 1
  fi
  NET_ARGS=(-netdev "vmnet-bridged,id=net0,ifname=${IFACE}" -device e1000,netdev=net0)
  echo "Launching Peak OS..."
  echo "  Display: ${DISPLAY_ARGS[*]}"
  echo "  CLI on framebuffer; COM1 serial mirrored here."
  echo "  NIC: e1000 vmnet-bridged via ${IFACE} (guest gets LAN DHCP)"
  echo "  After boot: ifconfig → curl http://<guest-ip>:8080/ from another device"
  echo "  Note: may require elevated macOS permissions; demo HTTP is unauthenticated."
elif [[ "$PEAK_NET_MODE" == "user" ]]; then
  # Forward a host port to the guest's HTTP container so other devices on the
  # LAN can reach it via this Mac's IP — no bridging/entitlement/root needed.
  # This is the reliable path on Wi-Fi Macs where vmnet-bridged misbehaves.
  HTTP_PORT="${PEAK_HTTP_PORT:-8080}"
  NET_ARGS=(-netdev "user,id=net0,hostfwd=tcp::${HTTP_PORT}-:${HTTP_PORT}" \
            -device e1000,netdev=net0)
  HOST_IP="$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo '<mac-lan-ip>')"
  echo "Launching Peak OS..."
  echo "  Display: ${DISPLAY_ARGS[*]}"
  echo "  CLI on framebuffer; COM1 serial mirrored here."
  echo "  NIC: e1000 user-net (DHCP/fallback 10.0.2.15)"
  echo "  Port forward: host :${HTTP_PORT} → guest :${HTTP_PORT}"
  echo "  In guest: ctr build peak/web:latest && ctr run -p ${HTTP_PORT} --name web peak/web:latest"
  echo "  From another device: curl http://${HOST_IP}:${HTTP_PORT}/"
else
  echo "PEAK_NET_MODE must be 'user' or 'bridged' (got: $PEAK_NET_MODE)"
  exit 1
fi

DISK="${ROOT}/build/peak-disk.img"
if [[ ! -f "$DISK" ]]; then
  echo "==> Creating ${DISK} (32MiB ATA persist image)"
  dd if=/dev/zero of="$DISK" bs=1m count=32 status=none 2>/dev/null || \
    dd if=/dev/zero of="$DISK" bs=1048576 count=32
fi

ARGS=("$QEMU" \
  -machine q35 \
  -m 256M \
  -smp 1 \
  -cdrom "$ISO" \
  -drive "file=${DISK},format=raw,if=ide" \
  -boot d \
  -serial stdio \
  -device virtio-rng-pci-transitional \
  "${NET_ARGS[@]}" \
  -no-reboot)
if [[ -n "$FW_DRIVE" ]]; then
  ARGS+=(-drive "$FW_DRIVE")
fi
ARGS+=("${DISPLAY_ARGS[@]}" "$@")
exec "${ARGS[@]}"