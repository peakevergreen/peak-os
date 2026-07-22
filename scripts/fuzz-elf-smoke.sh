#!/usr/bin/env bash
# Short adversarial smoke for boot ELF loader (host).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin:${PATH:-}"

make test-host >/dev/null
BIN=""
for c in build/x86_64/tests/test_boot build/tests/test_boot; do
  if [[ -x "$c" ]]; then BIN="$c"; break; fi
done
if [[ -z "$BIN" ]]; then
  echo "FAIL: test_boot binary missing"
  exit 1
fi
SEED="${PEAK_FUZZ_SEED:-42}"
ITERS="${PEAK_FUZZ_ITERS:-200}"
"$BIN" --fuzz "$ITERS" --seed "$SEED"
echo "ok: fuzz-elf-smoke ($ITERS mutations)"
