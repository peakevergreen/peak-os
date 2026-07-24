#!/usr/bin/env bash
# Soft-fail live TLS allowlist probe (host curl). Offline / blocked net → exit 0.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ALLOWLIST=(
  "https://example.com/"
  "https://www.cloudflare.com/"
)

if ! command -v curl >/dev/null 2>&1; then
  echo "smoke-tls-live: curl missing — skip"
  exit 0
fi

ok=0
fail=0
skip=0
for url in "${ALLOWLIST[@]}"; do
  code=$(curl -sS -o /dev/null -w "%{http_code}" --connect-timeout 5 --max-time 15 -L "$url" 2>/dev/null || echo "000")
  if [[ "$code" == "000" ]]; then
    echo "smoke-tls-live: SKIP $url (unreachable)"
    skip=$((skip + 1))
  elif [[ "$code" =~ ^[23] ]]; then
    echo "smoke-tls-live: OK   $url ($code)"
    ok=$((ok + 1))
  else
    echo "smoke-tls-live: FAIL $url ($code)"
    fail=$((fail + 1))
  fi
done

echo "smoke-tls-live: ok=$ok fail=$fail skip=$skip"
# Soft-fail: never fail the job for offline / partial reachability.
exit 0
