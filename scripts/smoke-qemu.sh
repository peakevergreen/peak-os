#!/usr/bin/env bash
# QEMU smoke: boot with e1000 + disk, capture serial until shell prompt.
# PEAK_FIRMWARE=bios|uefi|both (default bios). "both" runs BIOS then UEFI in one job.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
# shellcheck source=scripts/smoke-common.sh
source "$ROOT/scripts/smoke-common.sh"

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

run_firmware_smoke() {
  local firmware=$1
  local serial_log=$2
  echo "==> QEMU smoke firmware=${firmware} (${TIMEOUT_SEC}s)"
  rm -f "$serial_log"

  if [[ "$firmware" == "uefi" ]]; then
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
    echo "    → $serial_log (OVMF + Peak ESP dir)"
    "$QEMU_BIN" \
      -machine q35 \
      -drive "if=pflash,format=raw,readonly=on,file=${OVMF_CODE}" \
      -drive "if=pflash,format=raw,file=${OVMF_VARS}" \
      -drive "file=fat:rw:${ESPDIR},format=raw,if=virtio" \
      -drive "file=$DISK,format=qcow2,if=ide" \
      -m 512 \
      -serial "file:$serial_log" \
      -display none \
      -no-reboot \
      -device e1000,netdev=n0 \
      -netdev user,id=n0 \
      >/dev/null 2>&1 &
  else
    echo "    → $serial_log (SeaBIOS + hybrid ISO)"
    "$QEMU_BIN" \
      -machine q35 \
      -cdrom "$ISO" \
      -drive "file=$DISK,format=qcow2,if=ide" \
      -m 512 \
      -serial "file:$serial_log" \
      -display none \
      -no-reboot \
      -device e1000,netdev=n0 \
      -netdev user,id=n0 \
      >/dev/null 2>&1 &
  fi

  local qpid=$!
  if ! smoke_wait_serial "$serial_log" 'peak:/' "$qpid" "$TIMEOUT_SEC"; then
    smoke_qemu_kill "$qpid"
    if [[ ! -f "$serial_log" ]]; then
      echo "FAIL: no serial log"
      exit 1
    fi
    echo "FAIL: no shell prompt within ${TIMEOUT_SEC}s"
    tail -40 "$serial_log"
    exit 1
  fi
  smoke_qemu_kill "$qpid"

  if [[ ! -f "$serial_log" ]]; then
    echo "FAIL: no serial log"
    exit 1
  fi
  smoke_check_x86_boot "$serial_log"
  echo "OK — QEMU smoke passed (firmware=${firmware})"
}

if [[ "$PEAK_FIRMWARE" == "both" ]]; then
  run_firmware_smoke bios "${SERIAL_LOG%.log}-bios.log"
  run_firmware_smoke uefi "${SERIAL_LOG%.log}-uefi.log"
else
  if [[ "$PEAK_FIRMWARE" == "uefi" ]]; then
    SERIAL_LOG="${SERIAL_LOG%.log}-uefi.log"
  fi
  run_firmware_smoke "$PEAK_FIRMWARE" "$SERIAL_LOG"
fi
exit 0
