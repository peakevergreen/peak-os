#!/usr/bin/env bash
# Soft fuzz smoke for TLS ClientHello builder + X.509 parser.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="/opt/homebrew/opt/llvm/bin:${PATH:-}"
cd "$ROOT"
make -s build/x86_64/tests/test_tls
./build/x86_64/tests/test_tls --fuzz 500 --seed 42
echo "fuzz-tls-smoke: ok"
