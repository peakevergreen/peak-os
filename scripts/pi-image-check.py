#!/usr/bin/env python3
"""Validate Peak OS Raspberry Pi disk image without mounting."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

SECTOR = 512


def fat32_root_names(boot: bytes) -> set[str]:
    bps = struct.unpack_from("<H", boot, 11)[0]
    spc = boot[13]
    reserved = struct.unpack_from("<H", boot, 14)[0]
    fats = boot[16]
    fat_sectors = struct.unpack_from("<I", boot, 36)[0]
    root_cluster = struct.unpack_from("<I", boot, 44)[0]
    data_start = (reserved + fats * fat_sectors) * bps
    root_off = data_start + (root_cluster - 2) * spc * bps
    root = boot[root_off : root_off + spc * bps]

    names: set[str] = set()
    lfn_parts: list[tuple[int, list[int]]] = []
    positions = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
    for offset in range(0, len(root), 32):
        entry = root[offset : offset + 32]
        if not entry or entry[0] == 0:
            break
        if entry[0] == 0xE5:
            lfn_parts.clear()
            continue
        if entry[11] == 0x0F:
            units = [struct.unpack_from("<H", entry, pos)[0] for pos in positions]
            lfn_parts.append((entry[0] & 0x1F, units))
            continue
        if lfn_parts:
            units = [
                unit
                for _, chunk in sorted(lfn_parts)
                for unit in chunk
                if unit not in (0x0000, 0xFFFF)
            ]
            name = b"".join(struct.pack("<H", unit) for unit in units).decode(
                "utf-16le"
            )
        else:
            base = entry[0:8].decode("ascii").rstrip()
            ext = entry[8:11].decode("ascii").rstrip()
            name = base + (f".{ext}" if ext else "")
        names.add(name.lower())
        lfn_parts.clear()
    return names


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} image")
        return 2
    path = Path(sys.argv[1])
    data = path.read_bytes()
    fail = 0

    def ok(msg: str) -> None:
        print(f"ok: {msg}")

    def bad(msg: str) -> None:
        nonlocal fail
        print(f"FAIL: {msg}")
        fail = 1

    if len(data) < 1024 * 1024:
        bad("image too small")
        return 1
    if data[510] != 0x55 or data[511] != 0xAA:
        bad("missing MBR signature")
    else:
        ok("MBR signature")

    # Partition 1
    p1 = data[446:462]
    bootable, typ = p1[0], p1[4]
    start, count = struct.unpack_from("<II", p1, 8)
    if typ not in (0x0B, 0x0C):
        bad(f"partition 1 type {typ:#x} not FAT32")
    else:
        ok(f"FAT boot partition LBA {start} count {count}")
    if not (bootable & 0x80):
        bad("partition 1 not bootable")
    else:
        ok("partition 1 bootable")

    p2 = data[462:478]
    typ2 = p2[4]
    start2, count2 = struct.unpack_from("<II", p2, 8)
    if typ2 != 0x83:
        bad(f"partition 2 type {typ2:#x} expected 0x83 PeakFS")
    else:
        ok(f"PeakFS partition LBA {start2} count {count2}")
    if start2 < start + count:
        bad("partitions overlap")
    else:
        ok("partitions non-overlapping")

    # Validate FAT32 geometry and exact root filenames.
    boot = data[start * SECTOR : (start + count) * SECTOR]
    bps = struct.unpack_from("<H", boot, 11)[0]
    spc = boot[13]
    reserved = struct.unpack_from("<H", boot, 14)[0]
    fats = boot[16]
    fat_sectors = struct.unpack_from("<I", boot, 36)[0]
    clusters = (count - reserved - fats * fat_sectors) // spc
    if clusters < 65525:
        bad(f"FAT32 has only {clusters} clusters (would be classified FAT16)")
    else:
        ok(f"valid FAT32 geometry ({clusters} clusters)")

    names = fat32_root_names(boot)
    required = {
        "kernel8.img",
        "config.txt",
        "cmdline.txt",
        "bootcode.bin",
        "start.elf",
        "fixup.dat",
        "bcm2710-rpi-3-b.dtb",
    }
    for name in sorted(required):
        if name not in names:
            bad(f"{name} missing with exact FAT filename")
        else:
            ok(f"{name} present")

    magic = data[start2 * SECTOR : start2 * SECTOR + 8]
    if magic != b"PEAKFS01":
        bad(f"PeakFS magic {magic!r}")
    else:
        ok("PeakFS partition magic")

    sums = path.parent / "SHA256SUMS"
    if sums.exists():
        ok("SHA256SUMS present")
    else:
        bad("SHA256SUMS missing")

    if fail:
        print("pi-image-check FAILED")
        return 1
    print("pi-image-check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
