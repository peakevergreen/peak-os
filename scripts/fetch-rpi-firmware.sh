#!/usr/bin/env bash
# Fetch pinned Raspberry Pi boot firmware (binary exception). Offline cache supported.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/third_party/rpi-firmware"
CACHE="${PEAK_FW_CACHE:-$ROOT/build/fw-cache/rpi-firmware}"
# Pin to a known raspberrypi/firmware commit (boot/ tree)
FW_REPO="https://github.com/raspberrypi/firmware"
FW_REF="${PEAK_FW_REF:-1.20250430}"

mkdir -p "$CACHE" "$OUT"

FILES=(
  bootcode.bin
  start.elf
  start4.elf
  fixup.dat
  fixup4.dat
  bcm2710-rpi-3-b.dtb
  bcm2710-rpi-3-b-plus.dtb
  bcm2710-rpi-cm3.dtb
  bcm2710-rpi-zero-2-w.dtb
  bcm2711-rpi-4-b.dtb
  bcm2711-rpi-400.dtb
  bcm2711-rpi-cm4.dtb
  bcm2712-rpi-5-b.dtb
)

echo "==> Raspberry Pi firmware → $OUT (ref $FW_REF)"

fetch_one() {
  local f="$1"
  local url="$FW_REPO/raw/$FW_REF/boot/$f"
  local dest="$CACHE/$FW_REF/$f"
  mkdir -p "$(dirname "$dest")"
  if [[ -f "$dest" && "${PEAK_FW_FORCE:-}" != "1" ]]; then
    return 0
  fi
  echo "  fetch $f"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$dest"
  else
    wget -q "$url" -O "$dest"
  fi
}

mkdir -p "$CACHE/$FW_REF"
fetch_fail=0
for f in "${FILES[@]}"; do
  if ! fetch_one "$f"; then
    echo "FAIL: could not fetch $f"
    fetch_fail=1
  fi
done
if [[ "$fetch_fail" -ne 0 ]]; then
  echo "firmware-fetch FAILED"
  exit 1
fi

# Stage into third_party
rm -rf "$OUT"
mkdir -p "$OUT"
for f in "${FILES[@]}"; do
  if [[ -f "$CACHE/$FW_REF/$f" ]]; then
    cp "$CACHE/$FW_REF/$f" "$OUT/$f"
  else
    echo "FAIL: missing staged $f"
    exit 1
  fi
done

# Verify against committed lockfile when present
LOCK="$ROOT/scripts/rpi-firmware.sha256"
if [[ -f "$LOCK" ]]; then
  echo "==> Verifying firmware hashes against $LOCK"
  if command -v shasum >/dev/null 2>&1; then
    HASH_CMD=(shasum -a 256)
  else
    HASH_CMD=(sha256sum)
  fi
  while read -r expect name; do
    [[ -z "${expect:-}" || "$expect" == \#* ]] && continue
    [[ -z "${name:-}" ]] && continue
    if [[ ! -f "$OUT/$name" ]]; then
      echo "FAIL: lockfile entry missing file: $name"
      exit 1
    fi
    got=$("${HASH_CMD[@]}" "$OUT/$name" | awk '{print $1}')
    if [[ "$got" != "$expect" ]]; then
      echo "FAIL: hash mismatch for $name"
      echo "  expected $expect"
      echo "  got      $got"
      exit 1
    fi
    echo "ok: $name"
  done <"$LOCK"
fi

cat >"$OUT/LICENSE.txt" <<'EOF'
Raspberry Pi boot firmware binaries are © Raspberry Pi Ltd and redistributed
under the license terms published with raspberrypi/firmware. Peak OS treats
these as a documented binary exception; Peak-authored code remains open source.
EOF

cat >"$OUT/MANIFEST.txt" <<EOF
ref=$FW_REF
repo=$FW_REPO
fetched=$(date -u +%Y-%m-%dT%H:%MZ)
EOF

# Checksums
(
  cd "$OUT"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 * 2>/dev/null | grep -v MANIFEST | grep -v LICENSE >SHA256SUMS || true
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum * 2>/dev/null | grep -v MANIFEST | grep -v LICENSE >SHA256SUMS || true
  fi
)

echo "Firmware staged in $OUT"
ls -la "$OUT" | head -30
