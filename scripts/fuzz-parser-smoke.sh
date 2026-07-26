#!/usr/bin/env bash
# Corpus gates for DNS/ctr/blob parsers (host test).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin:${PATH:-}"

make test-host >/dev/null
BIN=""
for c in build/x86_64/tests/test_parser_corpus build/tests/test_parser_corpus; do
  if [[ -x "$c" ]]; then BIN="$c"; break; fi
done
if [[ -z "$BIN" ]]; then
  echo "FAIL: test_parser_corpus binary missing"
  exit 1
fi
"$BIN"
echo "fuzz-parser-smoke: ok"
