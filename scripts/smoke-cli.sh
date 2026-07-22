#!/usr/bin/env bash
# Smoke checks that do not require a live QEMU session.
# PEAK_SKIP_ISO=1 skips rebuild when build/peak-os.iso already exists (CI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PATH="/opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin:$PATH"

if [[ "${PEAK_SKIP_ISO:-}" == "1" ]]; then
  echo "==> skip iso rebuild (PEAK_SKIP_ISO=1)"
  test -f build/peak-os.iso
else
  echo "==> build iso"
  make iso
fi

echo "==> scripts present"
test -f docs/CLI.md
test -f boot/peak.conf
test ! -e limine.conf
test ! -e scripts/peak-host-proxy.py
test ! -e scripts/peak-ssh

echo "==> builtin count in ubin registry"
count=$(grep -c '^UBIN_CMD' kernel/user/ubin_cmds.def || true)
echo "    registered commands: $count"
test "$count" -ge 25

echo "==> theme names"
grep -q evergreen kernel/theme.c
grep -q midnight kernel/theme.c
grep -q amber kernel/theme.c

echo "==> cell metrics"
grep -q fb_cell_h kernel/fb.c
grep -q fb_cell_h kernel/console.c

echo "==> CLI front-buffer scroll invariant"
grep -q 'console_scroll_plan' kernel/console.c
grep -q 'fb->addr' kernel/console.c
grep -q 'keep CLI on front' kernel/console.c

echo "==> boot logo + status compaction"
grep -q console_boot_logo kernel/console.c
grep -q 'cols - used - tag_len - 1' kernel/console.c

if [[ "${PEAK_SKIP_HOST_TESTS:-}" != "1" ]]; then
  echo "==> phase7/host unit tests"
  make test-host
else
  echo "==> skip host tests (PEAK_SKIP_HOST_TESTS=1)"
fi

echo "==> security / purity markers"
grep -q 'copy_from_user' kernel/syscall.c
grep -qi 'tls certificate unverified' kernel/net/tls.c
grep -q 'agent_approve_write' kernel/agent.c
grep -q 'hlt_if_enabled' kernel/net/*.c
grep -q 'blockdev_flush' kernel/peakdisk.c
grep -q 'peak_bootinfo' kernel/boot.c
grep -q 'net_attempt_stats_reset' kernel/gui/desktop.c
grep -q 'PeakBrowser/1' kernel/net/*.c
! grep -q 'COM2\|COM3\|0x2F8\|0x3E8' kernel/agent.c kernel/user/ubin.c
! grep -rq 'limine\.h\|LIMINE_' kernel boot --include='*.c' --include='*.h' --include='*.S' --include='*.ld'

echo "==> purity"
chmod +x scripts/purity-check.sh
./scripts/purity-check.sh

echo "OK — smoke passed (run make smoke-bios / smoke-uefi for boot)"
