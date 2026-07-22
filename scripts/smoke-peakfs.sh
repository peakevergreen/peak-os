#!/usr/bin/env bash
# Two-pass QEMU PeakFS roundtrip using peak.conf smoke_persist=1.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin:/opt/homebrew/bin:${PATH:-}"

DISK="${DISK:-build/peak-smoke-persist.img}"
SERIAL1="${SERIAL1:-build/smoke-peakfs-save.log}"
SERIAL2="${SERIAL2:-build/smoke-peakfs-restore.log}"
TIMEOUT_SEC="${TIMEOUT_SEC:-90}"
CONF_BAK=$(mktemp)
ISO_CONF_MARKER="# peak-smoke-persist"

cleanup() {
  if [[ -f boot/peak.conf ]] && grep -q "$ISO_CONF_MARKER" boot/peak.conf 2>/dev/null; then
    mv "$CONF_BAK" boot/peak.conf
  else
    rm -f "$CONF_BAK"
  fi
}
trap cleanup EXIT

cp boot/peak.conf "$CONF_BAK"
if ! grep -q '^smoke_persist=' boot/peak.conf; then
  printf '\n%s\nsmoke_persist=1\n' "$ISO_CONF_MARKER" >> boot/peak.conf
fi

echo "==> rebuild ISO with smoke_persist=1"
rm -f build/peak-os.iso
make iso

rm -f "$DISK"
qemu-img create -f raw "$DISK" 64M >/dev/null

run_once() {
  local log=$1
  local want=$2
  rm -f "$log"
  qemu-system-x86_64 \
    -machine pc \
    -cdrom build/peak-os.iso \
    -drive "file=$DISK,format=raw,if=ide,cache=writethrough" \
    -m 512 \
    -serial "file:$log" \
    -display none \
    -no-reboot \
    -device e1000,netdev=n0 \
    -netdev user,id=n0 \
    >/dev/null 2>&1 &
  local qpid=$!
  local deadline=$((SECONDS + TIMEOUT_SEC))
  local ok=0
  while (( SECONDS < deadline )); do
    if [[ -f "$log" ]] && grep -q "$want" "$log" 2>/dev/null; then
      ok=1
      break
    fi
    if ! kill -0 "$qpid" 2>/dev/null; then
      break
    fi
    sleep 0.5
  done
  sleep 2
  kill "$qpid" 2>/dev/null || true
  wait "$qpid" 2>/dev/null || true
  if [[ "$ok" != "1" ]]; then
    echo "FAIL: did not see '$want' in $log"
    tail -60 "$log" || true
    exit 1
  fi
  echo "ok: $want"
}

echo "==> pass 1: save"
run_once "$SERIAL1" "peakdisk: smoke save ok"
echo "==> pass 2: restore"
run_once "$SERIAL2" "peakdisk: smoke restore ok"
echo "OK — PeakFS QEMU roundtrip passed"
