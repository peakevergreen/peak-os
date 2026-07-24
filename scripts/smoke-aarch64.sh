#!/usr/bin/env bash
# Headless aarch64 serial smoke.
# Prefer QEMU raspi3b (Pi load address + PL011); fall back to virt + virt UART.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
KERNEL=build/aarch64/kernel8.img
if [[ ! -f "$KERNEL" ]]; then
  make ARCH=aarch64 kernel8
fi

LOG=$(mktemp)
TIMEOUT_SEC=${PEAK_SMOKE_TIMEOUT:-45}
DTB=third_party/rpi-firmware/bcm2710-rpi-3-b.dtb

run_raspi3b() {
  local args=(-machine raspi3b -cpu cortex-a53 -m 1024 -nographic -kernel "$KERNEL")
  if [[ -f "$DTB" ]]; then
    args+=(-dtb "$DTB")
  fi
  qemu-system-aarch64 "${args[@]}"
}

run_virt() {
  # Load at Pi phys address; set PC; UART is pl011 at 0x09000000 (kernel detects "virt")
  qemu-system-aarch64 \
    -machine virt,gic-version=3 \
    -cpu cortex-a72 \
    -m 1024 \
    -nographic \
    -device loader,file="$KERNEL",addr=0x80000 \
    -device loader,addr=0x80000,cpu-num=0
}

# timeout(1) cannot execute shell functions — run them in an exported bash -c.
run_timed() {
  local fn=$1
  # KERNEL/DTB must be visible inside the timeout subshell.
  local wrapper="KERNEL=$(printf %q "$KERNEL"); DTB=$(printf %q "$DTB"); "
  wrapper+="$(declare -f run_raspi3b); $(declare -f run_virt); $fn"
  if command -v timeout >/dev/null 2>&1; then
    timeout "$TIMEOUT_SEC" bash -c "$wrapper"
  elif command -v gtimeout >/dev/null 2>&1; then
    gtimeout "$TIMEOUT_SEC" bash -c "$wrapper"
  else
    bash -c "$wrapper" &
    local qpid=$!
    (
      sleep "$TIMEOUT_SEC"
      kill "$qpid" 2>/dev/null || true
      sleep 1
      kill -9 "$qpid" 2>/dev/null || true
    ) &
    local wpid=$!
    wait "$qpid" 2>/dev/null || true
    kill "$wpid" 2>/dev/null || true
    wait "$wpid" 2>/dev/null || true
  fi
}

set +e
if qemu-system-aarch64 -machine help 2>/dev/null | grep -q raspi3b; then
  RUN=run_raspi3b
else
  RUN=run_virt
fi

run_timed "$RUN" >"$LOG" 2>&1
set -e

echo "---- serial log (tail) ----"
tail -n 80 "$LOG" || true

# Prefer shell / boot-complete markers (checklist + docs/rpi.md). Weaker early
# markers alone are not enough for smoke-aarch64 gate.
STRONG_RE='Boot complete|peak:/'
WEAK_RE='PeakOS booting|peak-rpi:|mmu on|rpi: soc'
if grep -qE "$STRONG_RE" "$LOG"; then
  echo "smoke-aarch64: saw Boot complete / peak:/"
  rm -f "$LOG"
  exit 0
fi
if grep -qE "$WEAK_RE" "$LOG"; then
  echo "smoke-aarch64: early boot only (missing Boot complete / peak:/) — fail"
  rm -f "$LOG"
  exit 1
fi

# If raspi3b silent, try virt once
if [[ "$RUN" == "run_raspi3b" ]]; then
  echo "note: raspi3b quiet — trying virt loader path"
  : >"$LOG"
  set +e
  run_timed run_virt >"$LOG" 2>&1
  set -e
  tail -n 40 "$LOG" || true
  if grep -qE "$STRONG_RE" "$LOG"; then
    echo "smoke-aarch64: saw Boot complete / peak:/ (virt)"
    rm -f "$LOG"
    exit 0
  fi
fi

echo "smoke-aarch64 FAILED (no Boot complete / peak:/)"
rm -f "$LOG"
exit 1
