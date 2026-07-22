#!/usr/bin/env python3
"""Create a FAT12 EFI System Partition image with BOOTX64.EFI (+ optional files)."""
from __future__ import annotations

import argparse
import os
import struct
import sys


def _set12(fat: bytearray, idx: int, val: int) -> None:
    off = idx + (idx // 2)
    if idx & 1:
        fat[off] = (fat[off] & 0x0F) | ((val & 0x0F) << 4)
        fat[off + 1] = (val >> 4) & 0xFF
    else:
        fat[off] = val & 0xFF
        fat[off + 1] = (fat[off + 1] & 0xF0) | ((val >> 8) & 0x0F)


def _dirent(name: bytes, attr: int, cluster: int, size: int) -> bytes:
    ent = bytearray(32)
    ent[0:11] = name
    ent[11] = attr
    struct.pack_into("<H", ent, 26, cluster)
    struct.pack_into("<I", ent, 28, size)
    return bytes(ent)


def build_fat12(efi_path: str, out_path: str, size_kib: int, extras: list[tuple[str, bytes]]) -> None:
    with open(efi_path, "rb") as f:
        efi = f.read()
    if not efi:
        raise SystemExit("EFI file empty")

    sector = 512
    sectors = size_kib * 1024 // sector
    reserved = 1
    fats = 2
    root_ents = 512
    sectors_per_fat = 64
    sectors_per_cluster = 1
    root_sectors = (root_ents * 32 + sector - 1) // sector
    data_start = reserved + fats * sectors_per_fat + root_sectors
    data_sectors = sectors - data_start
    if data_sectors < 64:
        raise SystemExit("image too small")

    img = bytearray(sectors * sector)
    bpb = bytearray(sector)
    bpb[0:3] = b"\xeb\x3c\x90"
    bpb[3:11] = b"PEAKBOOT"
    struct.pack_into("<H", bpb, 11, sector)
    bpb[13] = sectors_per_cluster
    struct.pack_into("<H", bpb, 14, reserved)
    bpb[16] = fats
    struct.pack_into("<H", bpb, 17, root_ents)
    if sectors < 65536:
        struct.pack_into("<H", bpb, 19, sectors)
    else:
        struct.pack_into("<I", bpb, 32, sectors)
    bpb[21] = 0xF8
    struct.pack_into("<H", bpb, 22, sectors_per_fat)
    struct.pack_into("<H", bpb, 24, 32)
    struct.pack_into("<H", bpb, 26, 2)
    bpb[38] = 0x29
    struct.pack_into("<I", bpb, 39, 0x5045414B)
    bpb[43:54] = b"PEAKESP    "
    bpb[54:62] = b"FAT12   "
    bpb[510:512] = b"\x55\xaa"
    img[0:sector] = bpb

    # Directory clusters: 2=EFI, 3=BOOT, 4=PEAK
    fat = bytearray(sectors_per_fat * sector)
    fat[0] = 0xF8
    fat[1] = 0xFF
    fat[2] = 0xFF
    _set12(fat, 2, 0xFF8)
    _set12(fat, 3, 0xFF8)
    _set12(fat, 4, 0xFF8)

    next_cl = 5

    def alloc_chain(blob: bytes) -> int:
        nonlocal next_cl
        n = (len(blob) + sector - 1) // sector
        if n == 0:
            n = 1
        first = next_cl
        for i in range(n):
            cl = first + i
            if cl >= 0xFF0:
                raise SystemExit("FAT12 cluster overflow")
            _set12(fat, cl, 0xFF8 if i + 1 == n else cl + 1)
        next_cl = first + n
        off = (data_start + (first - 2)) * sector
        img[off : off + len(blob)] = blob
        if len(blob) % sector:
            # already zero-filled
            pass
        return first

    efi_first = alloc_chain(efi)
    extra_meta = []
    for short83, blob in extras:
        first = alloc_chain(blob)
        extra_meta.append((short83, first, len(blob)))

    for i in range(fats):
        base = (reserved + i * sectors_per_fat) * sector
        img[base : base + len(fat)] = fat

    root = (reserved + fats * sectors_per_fat) * sector
    img[root : root + 32] = _dirent(b"EFI        ", 0x10, 2, 0)

    efi_dir = data_start * sector
    img[efi_dir : efi_dir + 32] = _dirent(b".          ", 0x10, 2, 0)
    img[efi_dir + 32 : efi_dir + 64] = _dirent(b"..         ", 0x10, 0, 0)
    img[efi_dir + 64 : efi_dir + 96] = _dirent(b"BOOT       ", 0x10, 3, 0)
    img[efi_dir + 96 : efi_dir + 128] = _dirent(b"PEAK       ", 0x10, 4, 0)

    boot_dir = (data_start + 1) * sector
    img[boot_dir : boot_dir + 32] = _dirent(b".          ", 0x10, 3, 0)
    img[boot_dir + 32 : boot_dir + 64] = _dirent(b"..         ", 0x10, 2, 0)
    img[boot_dir + 64 : boot_dir + 96] = _dirent(b"BOOTX64 EFI", 0x20, efi_first, len(efi))

    peak_dir = (data_start + 2) * sector
    img[peak_dir : peak_dir + 32] = _dirent(b".          ", 0x10, 4, 0)
    img[peak_dir + 32 : peak_dir + 64] = _dirent(b"..         ", 0x10, 2, 0)
    slot = 64
    for short83, first, size in extra_meta:
        img[peak_dir + slot : peak_dir + slot + 32] = _dirent(short83, 0x20, first, size)
        slot += 32

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(img)
    print(f"Wrote {out_path} ({size_kib} KiB FAT12 ESP, clusters used through {next_cl - 1})")


def _short83(name: str) -> bytes:
    """Convert 'PEAK.CONF' or 'KERNEL.ELF' into a FAT 8.3 directory entry name."""
    base, _, ext = name.upper().partition(".")
    base = (base + "        ")[:8]
    ext = (ext + "   ")[:3]
    return (base + ext).encode("ascii")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("efi")
    ap.add_argument("out")
    ap.add_argument("--size-kib", type=int, default=8192)
    ap.add_argument("--kernel", help="Optional kernel ELF placed at EFI/PEAK/KERNEL.ELF")
    ap.add_argument(
        "--extra",
        action="append",
        default=[],
        help="Host path:FAT83name placed under EFI/PEAK (e.g. boot/peak.conf:PEAK.CONF)",
    )
    args = ap.parse_args()
    extras: list[tuple[bytes, bytes]] = []
    if args.kernel:
        with open(args.kernel, "rb") as f:
            extras.append((b"KERNEL  ELF", f.read()))
    for item in args.extra:
        if ":" not in item:
            raise SystemExit(f"--extra needs path:NAME (got {item!r})")
        path, name = item.split(":", 1)
        with open(path, "rb") as f:
            extras.append((_short83(name), f.read()))
    build_fat12(args.efi, args.out, args.size_kib, extras)


if __name__ == "__main__":
    main()
