#!/usr/bin/env bash
# Shared helpers for QEMU serial smoke scripts.
# Source from smoke-qemu.sh / smoke-peakfs.sh — do not execute directly.
set -euo pipefail

smoke_qemu_kill() {
  local qpid=$1
  kill "$qpid" 2>/dev/null || true
  wait "$qpid" 2>/dev/null || true
}

# Wait until $log contains $pattern or timeout; returns 0 on match.
smoke_wait_serial() {
  local log=$1
  local pattern=$2
  local qpid=$3
  local timeout_sec=${4:-60}
  local deadline=$((SECONDS + timeout_sec))
  while (( SECONDS < deadline )); do
    if [[ -f "$log" ]] && grep -q "$pattern" "$log" 2>/dev/null; then
      return 0
    fi
    if ! kill -0 "$qpid" 2>/dev/null; then
      return 1
    fi
    sleep 0.5
  done
  return 1
}

# Standard x86 boot markers shared by BIOS/UEFI smoke.
smoke_check_x86_boot() {
  local log=$1
  echo "==> checking serial boot markers"
  grep -q "PeakOS booting" "$log" || grep -q "Peak BIOS loader\|Peak UEFI loader" "$log" || {
    echo "FAIL: no boot banner / loader"; tail -40 "$log"; return 1;
  }
  grep -q "Physical memory" "$log" || { echo "FAIL: no PMM"; tail -40 "$log"; return 1; }
  grep -q "System monitor" "$log" || { echo "FAIL: no System monitor"; return 1; }
  grep -Eq "virtio-net: ready|e1000 \(dhcp |e1000 \(static |e1000 \(fallback |Network \(e1000\)|Network \(virtio|net: ipv4 ready" "$log" || {
    echo "FAIL: network did not come up"; tail -80 "$log"; return 1;
  }
  grep -q "Boot complete" "$log" || { echo "FAIL: boot incomplete"; tail -40 "$log"; return 1; }
  grep -q "peak:/" "$log" || { echo "FAIL: no shell prompt"; tail -40 "$log"; return 1; }
  if grep -qi "TLS certificate unverified\|reject unverified certificate" "$log"; then
    echo "FAIL: unsolicited TLS during boot"
    return 1
  fi
  return 0
}
