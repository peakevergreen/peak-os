#!/usr/bin/env python3
"""Write build/SHA256SUMS for Peak release artifacts."""
from __future__ import annotations

import hashlib
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"

CANDIDATES = [
    "x86_64/kernel.elf",
    "kernel.elf",
    "x86_64/boot/peak-bios.bin",
    "x86_64/boot/BOOTX64.EFI",
    "boot/peak-bios.bin",
    "boot/BOOTX64.EFI",
    "peak-os.iso",
    "aarch64/kernel8.img",
    "peak-os-rpi-arm64.img",
]


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    lines: list[str] = []
    for rel in CANDIDATES:
        p = BUILD / rel
        if p.is_file():
            lines.append(f"{sha256_file(p)}  {rel}")
    if not lines:
        print("mkmanifest: no artifacts under build/", file=sys.stderr)
        return 1
    out = BUILD / "SHA256SUMS"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(lines)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
