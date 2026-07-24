#!/usr/bin/env bash
# Fail if kernel ELF BSS or image size exceeds budgets (measured optimization gate).
set -euo pipefail

ELF="${1:-}"
if [[ -z "$ELF" || ! -f "$ELF" ]]; then
  echo "usage: $0 <kernel.elf>" >&2
  exit 2
fi

# Budgets (bytes). Raise deliberately when growth is intentional.
MAX_BSS=$((20 * 1024 * 1024))  # 20 MiB
MAX_FILE=$((12 * 1024 * 1024)) # 12 MiB linked ELF on disk

bss=""
# Prefer llvm-size / GNU size when they understand the target ELF (Linux CI).
# On macOS, host `size` rejects foreign ELF — fall back to a tiny Python parser.
if command -v llvm-size >/dev/null 2>&1; then
  bss=$(llvm-size "$ELF" 2>/dev/null | awk 'NR==2 { print $3; exit }' || true)
elif command -v size >/dev/null 2>&1; then
  bss=$(size "$ELF" 2>/dev/null | awk 'NR==2 { print $3; exit }' || true)
fi

if [[ -z "${bss}" ]]; then
  bss=$(python3 - "$ELF" <<'PY'
import struct, sys
path = sys.argv[1]
data = open(path, "rb").read()
if data[:4] != b"\x7fELF":
    sys.exit("check-kernel-size: not an ELF")
endian = "<" if data[5] == 1 else ">"
ei_class = data[4]
if ei_class == 1:
    e_shoff = struct.unpack_from(endian + "I", data, 32)[0]
    e_shentsize, e_shnum = struct.unpack_from(endian + "HH", data, 46)
    shfmt = endian + "IIIIIIIIII"
else:
    e_shoff = struct.unpack_from(endian + "Q", data, 40)[0]
    e_shentsize, e_shnum = struct.unpack_from(endian + "HH", data, 58)
    shfmt = endian + "IIQQQQIIQQ"
bss = 0
for i in range(e_shnum):
    f = struct.unpack_from(shfmt, data, e_shoff + i * e_shentsize)
    # sh_type=NOBITS(8), sh_flags has SHF_ALLOC(2)
    if f[1] == 8 and (f[2] & 2):
        bss += f[5]
print(bss)
PY
)
fi

if [[ -z "${bss}" ]]; then
  echo "check-kernel-size: could not measure BSS (need llvm-size/size or python3)" >&2
  exit 1
fi

file_sz=$(wc -c < "$ELF" | tr -d ' ')

echo "check-kernel-size: $ELF bss=${bss} file=${file_sz} (limits bss=${MAX_BSS} file=${MAX_FILE})"

if [[ "$bss" -gt "$MAX_BSS" ]]; then
  echo "check-kernel-size: BSS ${bss} exceeds ${MAX_BSS}" >&2
  exit 1
fi
if [[ "$file_sz" -gt "$MAX_FILE" ]]; then
  echo "check-kernel-size: file ${file_sz} exceeds ${MAX_FILE}" >&2
  exit 1
fi
