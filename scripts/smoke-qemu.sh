#!/usr/bin/env bash
# QEMU smoke: boot with e1000 + disk, capture serial until shell prompt.
# PEAK_FIRMWARE=bios|uefi (default bios)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PATH="/opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin:/opt/homebrew/bin:$PATH"

ISO="${ISO:-build/peak-os.iso}"
DISK="${DISK:-build/peak-smoke.qcow2}"
SERIAL_LOG="${SERIAL_LOG:-build/smoke-serial.log}"
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"
PEAK_FIRMWARE="${PEAK_FIRMWARE:-bios}"

if [[ ! -f "$ISO" ]]; then
  echo "==> building ISO"
  make iso
fi

mkdir -p build
if [[ ! -f "$DISK" ]]; then
  qemu-img create -f qcow2 "$DISK" 64M
fi

QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  if [[ "${PEAK_SMOKE_OPTIONAL:-}" == "1" ]]; then
    echo "SKIP: qemu-system-x86_64 not found (PEAK_SMOKE_OPTIONAL=1)"
    exit 0
  fi
  echo "FAIL: qemu-system-x86_64 not found"
  exit 1
fi


echo "==> QEMU smoke firmware=${PEAK_FIRMWARE} (${TIMEOUT_SEC}s)"
rm -f "$SERIAL_LOG"

if [[ "$PEAK_FIRMWARE" == "uefi" ]]; then
  OVMF_CODE="${OVMF_CODE:-}"
  if [[ -z "$OVMF_CODE" ]]; then
    for c in \
      /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
      /usr/share/OVMF/OVMF_CODE.fd; do
      if [[ -f "$c" ]]; then OVMF_CODE="$c"; break; fi
    done
  fi
  if [[ -z "${OVMF_CODE:-}" || ! -f "$OVMF_CODE" ]]; then
    echo "FAIL: UEFI firmware not found"
    exit 1
  fi
  SERIAL_LOG="${SERIAL_LOG%.log}-uefi.log"
  ESPDIR="${ESPDIR:-build/boot/espdir}"
  mkdir -p "$ESPDIR/EFI/BOOT" "$ESPDIR/EFI/PEAK"
  EFI_SRC=""
  for c in build/x86_64/boot/BOOTX64.EFI build/boot/BOOTX64.EFI; do
    [[ -f "$c" ]] && EFI_SRC="$c" && break
  done
  KERNEL_SRC=""
  for c in build/x86_64/kernel.elf build/kernel.elf; do
    [[ -f "$c" ]] && KERNEL_SRC="$c" && break
  done
  if [[ -z "$EFI_SRC" || -z "$KERNEL_SRC" ]]; then
    echo "FAIL: missing UEFI artifacts (BOOTX64.EFI / kernel.elf under build/x86_64/)"
    exit 1
  fi
  cp -f "$EFI_SRC" "$ESPDIR/EFI/BOOT/BOOTX64.EFI"
  cp -f "$KERNEL_SRC" "$ESPDIR/EFI/PEAK/KERNEL.ELF"
  cp -f boot/peak.conf "$ESPDIR/EFI/PEAK/PEAK.CONF"
  printf '\\EFI\\BOOT\\BOOTX64.EFI\r\n' > "$ESPDIR/startup.nsh"
  OVMF_VARS="${OVMF_VARS:-build/ovmf-vars.fd}"
  if [[ ! -f "$OVMF_VARS" ]]; then
    dd if=/dev/zero of="$OVMF_VARS" bs=1048576 count=4 status=none 2>/dev/null || \
      dd if=/dev/zero of="$OVMF_VARS" bs=1048576 count=4
  fi
  echo "    → $SERIAL_LOG (OVMF + Peak ESP dir)"
  rm -f "$SERIAL_LOG"
  "$QEMU_BIN" \
    -machine q35 \
    -drive "if=pflash,format=raw,readonly=on,file=${OVMF_CODE}" \
    -drive "if=pflash,format=raw,file=${OVMF_VARS}" \
    -drive "file=fat:rw:${ESPDIR},format=raw,if=virtio" \
    -drive "file=$DISK,format=qcow2,if=ide" \
    -m 512 \
    -serial "file:$SERIAL_LOG" \
    -display none \
    -no-reboot \
    -device e1000,netdev=n0 \
    -netdev user,id=n0 \
    >/dev/null 2>&1 &
  qpid=$!
else
  echo "    → $SERIAL_LOG (SeaBIOS + hybrid ISO)"
  rm -f "$SERIAL_LOG"
  "$QEMU_BIN" \
    -machine q35 \
    -cdrom "$ISO" \
    -drive "file=$DISK,format=qcow2,if=ide" \
    -m 512 \
    -serial "file:$SERIAL_LOG" \
    -display none \
    -no-reboot \
    -device e1000,netdev=n0 \
    -netdev user,id=n0 \
    >/dev/null 2>&1 &
  qpid=$!
fi

deadline=$((SECONDS + TIMEOUT_SEC))
ok=0
while (( SECONDS < deadline )); do
  if [[ -f "$SERIAL_LOG" ]] && grep -q 'peak:/' "$SERIAL_LOG" 2>/dev/null; then
    ok=1
    break
  fi
  if ! kill -0 "$qpid" 2>/dev/null; then
    break
  fi
  sleep 0.5
done

kill "$qpid" 2>/dev/null || true
wait "$qpid" 2>/dev/null || true

if [[ ! -f "$SERIAL_LOG" ]]; then
  echo "FAIL: no serial log"
  exit 1
fi

echo "==> checking serial boot markers"
grep -q "PeakOS booting" "$SERIAL_LOG" || grep -q "Peak BIOS loader\|Peak UEFI loader" "$SERIAL_LOG" || {
  echo "FAIL: no boot banner / loader"; tail -40 "$SERIAL_LOG"; exit 1;
}
grep -q "Physical memory" "$SERIAL_LOG" || { echo "FAIL: no PMM"; tail -40 "$SERIAL_LOG"; exit 1; }
grep -q "System monitor" "$SERIAL_LOG" || { echo "FAIL: no System monitor"; exit 1; }
grep -q "Network (e1000)" "$SERIAL_LOG" || grep -q "net: ipv4 ready" "$SERIAL_LOG" || {
  echo "FAIL: network did not come up"; tail -80 "$SERIAL_LOG"; exit 1;
}
if grep -q "net: ipv4 ready" "$SERIAL_LOG"; then
  grep -Eq "net: ipv4 ready \((dhcp|static|fallback)" "$SERIAL_LOG" || {
    echo "FAIL: missing address mode in net ready line"; tail -40 "$SERIAL_LOG"; exit 1;
  }
fi
grep -q "Boot complete" "$SERIAL_LOG" || { echo "FAIL: boot incomplete"; tail -40 "$SERIAL_LOG"; exit 1; }
grep -q "peak:/" "$SERIAL_LOG" || { echo "FAIL: no shell prompt"; tail -40 "$SERIAL_LOG"; exit 1; }
# Logo / no TLS noise at idle boot
grep -q "PEAK\|____" "$SERIAL_LOG" || true
if grep -qi "TLS certificate unverified\|reject unverified certificate" "$SERIAL_LOG"; then
  echo "FAIL: unsolicited TLS during boot"
  exit 1
fi

echo "OK — QEMU smoke passed (firmware=${PEAK_FIRMWARE}; ok=$ok)"
exit 0
