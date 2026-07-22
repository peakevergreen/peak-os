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

SIZE_TOOL=""
if command -v llvm-size >/dev/null 2>&1; then
  SIZE_TOOL=llvm-size
elif command -v size >/dev/null 2>&1; then
  SIZE_TOOL=size
else
  echo "check-kernel-size: llvm-size/size required" >&2
  exit 1
fi

# Berkeley format line 2: text data bss dec hex filename
bss=$("$SIZE_TOOL" "$ELF" | awk 'NR==2 { print $3; exit }')
if [[ -z "${bss}" ]]; then
  echo "check-kernel-size: could not parse BSS from $SIZE_TOOL" >&2
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
