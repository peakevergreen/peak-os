#!/usr/bin/env python3
"""Build a flashable Peak OS Raspberry Pi SD image (no root required)."""
from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

SECTOR = 512
# Layout: MBR + 256MiB FAT32 boot + 256MiB PeakFS partition
BOOT_PART_SECTORS = 256 * 1024 * 1024 // SECTOR
DATA_PART_SECTORS = 256 * 1024 * 1024 // SECTOR
PART1_START = 2048
PART2_START = PART1_START + BOOT_PART_SECTORS
TOTAL_SECTORS = PART2_START + DATA_PART_SECTORS


def write_mbr(img: bytearray) -> None:
    def chs_dummy():
        return (0, 0, 0)

    def part_entry(bootable, typ, start, count):
        return struct.pack(
            "<B3sB3sII",
            0x80 if bootable else 0x00,
            bytes(chs_dummy()),
            typ,
            bytes(chs_dummy()),
            start,
            count,
        )

    img[446:462] = part_entry(True, 0x0C, PART1_START, BOOT_PART_SECTORS)  # FAT32 LBA
    img[462:478] = part_entry(False, 0x83, PART2_START, DATA_PART_SECTORS)  # Linux
    img[510] = 0x55
    img[511] = 0xAA


def fat32_format(size: int, volume_label: bytes = b"PEAKBOOT   ") -> bytes:
    """Minimal FAT32 with empty root — enough for firmware + kernel copy via host tools.
    We embed files using a simple contiguous allocation from cluster 2.
    """
    # Use fatcat-less approach: build BPB + FAT + root and place files contiguously.
    bytes_per_sec = 512
    # A 256 MiB FAT32 volume needs more than 65,525 data clusters. 4 KiB
    # clusters fall just below that threshold after FAT overhead and cause
    # strict readers to classify the BPB as malformed FAT16. Use 2 KiB.
    secs_per_clus = 4
    reserved = 32
    num_fats = 2
    # Estimate FAT size
    data_sectors = size // bytes_per_sec - reserved
    # iterative FAT size
    fat_sectors = 1
    for _ in range(8):
        clusters = (data_sectors - num_fats * fat_sectors) // secs_per_clus
        fat_sectors = max(1, (clusters * 4 + bytes_per_sec - 1) // bytes_per_sec)
    fat_sectors = max(fat_sectors, 64)
    root_clus = 2
    total_secs = size // bytes_per_sec

    bpb = bytearray(bytes_per_sec)
    bpb[0:3] = b"\xeb\x58\x90"
    bpb[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", bpb, 11, bytes_per_sec)
    bpb[13] = secs_per_clus
    struct.pack_into("<H", bpb, 14, reserved)
    bpb[16] = num_fats
    struct.pack_into("<H", bpb, 17, 0)  # root ents (FAT32)
    struct.pack_into("<H", bpb, 19, 0)
    bpb[21] = 0xF8
    struct.pack_into("<H", bpb, 22, 0)
    struct.pack_into("<H", bpb, 24, 32)
    struct.pack_into("<H", bpb, 26, 2)
    struct.pack_into("<I", bpb, 28, PART1_START)
    struct.pack_into("<I", bpb, 32, total_secs)
    struct.pack_into("<I", bpb, 36, fat_sectors)
    struct.pack_into("<H", bpb, 40, 0)
    struct.pack_into("<H", bpb, 42, 0)
    struct.pack_into("<I", bpb, 44, root_clus)
    struct.pack_into("<H", bpb, 48, 1)  # FSInfo
    struct.pack_into("<H", bpb, 50, 6)  # backup boot
    bpb[64] = 0x80
    bpb[66] = 0x29
    struct.pack_into("<I", bpb, 67, 0x5045414B)  # serial 'PEAK'
    bpb[71:82] = volume_label[:11].ljust(11)
    bpb[82:90] = b"FAT32   "
    bpb[510] = 0x55
    bpb[511] = 0xAA

    fsinfo = bytearray(bytes_per_sec)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
    fsinfo[510] = 0x55
    fsinfo[511] = 0xAA

    part = bytearray(size)
    part[0:512] = bpb
    part[512:1024] = fsinfo
    part[6 * 512 : 7 * 512] = bpb  # backup
    part[7 * 512 : 8 * 512] = fsinfo  # backup FSInfo

    fat = bytearray(fat_sectors * bytes_per_sec)
    # media + EOC markers
    struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0x0FFFFFFF, 0x0FFFFFFF)

    fat_off = reserved * bytes_per_sec
    for n in range(num_fats):
        part[fat_off + n * len(fat) : fat_off + (n + 1) * len(fat)] = fat

    return bytes(part), reserved, fat_sectors, num_fats, secs_per_clus, root_clus


def add_files_fat32(part: bytearray, meta, files: dict[str, bytes]) -> None:
    reserved, fat_sectors, num_fats, secs_per_clus, root_clus = meta
    bps = 512
    clus_bytes = secs_per_clus * bps
    fat_off = reserved * bps
    data_start = reserved * bps + num_fats * fat_sectors * bps

    def clus_to_off(c: int) -> int:
        return data_start + (c - 2) * clus_bytes

    def fat_get(c: int) -> int:
        o = fat_off + c * 4
        return struct.unpack_from("<I", part, o)[0] & 0x0FFFFFFF

    def fat_set(c: int, v: int) -> None:
        for n in range(num_fats):
            o = fat_off + n * fat_sectors * bps + c * 4
            struct.pack_into("<I", part, o, (v & 0x0FFFFFFF) | 0x00000000)

    # Find free clusters starting at 3 (2 = root)
    next_clus = 3
    dir_entries = bytearray(clus_bytes)

    def alloc_chain(blob: bytes) -> int:
        nonlocal next_clus
        if not blob:
            blob = b"\0"
        nclus = (len(blob) + clus_bytes - 1) // clus_bytes
        first = next_clus
        for i in range(nclus):
            c = next_clus
            next_clus += 1
            off = clus_to_off(c)
            chunk = blob[i * clus_bytes : (i + 1) * clus_bytes]
            part[off : off + len(chunk)] = chunk
            if i + 1 < nclus:
                fat_set(c, c + 1)
            else:
                fat_set(c, 0x0FFFFFFF)
        return first

    ent_i = 0
    used_short: set[bytes] = set()

    def lfn_checksum(short: bytes) -> int:
        checksum = 0
        for byte in short:
            checksum = (((checksum & 1) << 7) | (checksum >> 1)) + byte
            checksum &= 0xFF
        return checksum

    def short_name(name: str) -> tuple[bytes, bool]:
        """Return a unique 8.3 alias and whether an LFN is required."""
        upper = name.upper()
        base, dot, ext = upper.rpartition(".")
        if not dot:
            base, ext = upper, ""
        allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@'`^")
        exact = (
            1 <= len(base) <= 8
            and len(ext) <= 3
            and all(ch in allowed for ch in base + ext)
        )
        candidate = base.ljust(8)[:8] + ext.ljust(3)[:3]
        encoded = candidate.encode("ascii", "strict") if exact else b""
        if exact and encoded not in used_short:
            used_short.add(encoded)
            return encoded, name != upper

        clean_base = "".join(ch for ch in base if ch in allowed) or "FILE"
        clean_ext = "".join(ch for ch in ext if ch in allowed)[:3]
        for index in range(1, 1000000):
            tail = f"~{index}"
            alias_base = (clean_base[: 8 - len(tail)] + tail).ljust(8)
            encoded = (alias_base + clean_ext.ljust(3)).encode("ascii")
            if encoded not in used_short:
                used_short.add(encoded)
                return encoded, True
        raise RuntimeError(f"could not allocate FAT alias for {name}")

    def lfn_entries(name: str, short: bytes) -> list[bytes]:
        units = list(name.encode("utf-16le"))
        code_units = [
            units[i] | (units[i + 1] << 8) for i in range(0, len(units), 2)
        ]
        code_units.append(0x0000)
        while len(code_units) % 13:
            code_units.append(0xFFFF)
        chunks = [
            code_units[i : i + 13] for i in range(0, len(code_units), 13)
        ]
        checksum = lfn_checksum(short)
        result: list[bytes] = []
        for index in range(len(chunks), 0, -1):
            entry = bytearray(32)
            entry[0] = index | (0x40 if index == len(chunks) else 0)
            entry[11] = 0x0F
            entry[12] = 0
            entry[13] = checksum
            struct.pack_into("<H", entry, 26, 0)
            chunk = chunks[index - 1]
            positions = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
            for pos, unit in zip(positions, chunk):
                struct.pack_into("<H", entry, pos, unit)
            result.append(bytes(entry))
        return result

    def append_entry(entry: bytes) -> None:
        nonlocal ent_i
        if ent_i + 32 > len(dir_entries):
            raise RuntimeError("FAT root directory exceeds one cluster")
        dir_entries[ent_i : ent_i + 32] = entry
        ent_i += 32

    for name, data in files.items():
        short, needs_lfn = short_name(name)
        first = alloc_chain(data)
        if needs_lfn:
            for lfn in lfn_entries(name, short):
                append_entry(lfn)
        ent = bytearray(32)
        ent[0:11] = short
        ent[11] = 0x20
        struct.pack_into("<H", ent, 26, first & 0xFFFF)
        struct.pack_into("<H", ent, 20, (first >> 16) & 0xFFFF)
        struct.pack_into("<I", ent, 28, len(data))
        append_entry(ent)

    # Write root cluster
    off = clus_to_off(root_clus)
    part[off : off + len(dir_entries)] = dir_entries
    fat_set(root_clus, 0x0FFFFFFF)

    total_clusters = (len(part) - data_start) // clus_bytes
    free_clusters = max(0, total_clusters - (next_clus - 2))
    for sector in (1, 7):
        fsinfo_off = sector * bps
        struct.pack_into("<I", part, fsinfo_off + 488, free_clusters)
        struct.pack_into("<I", part, fsinfo_off + 492, next_clus)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("kernel8")
    ap.add_argument("output")
    ap.add_argument("--config", required=True)
    ap.add_argument("--cmdline", required=True)
    ap.add_argument("--firmware-dir", required=True)
    args = ap.parse_args()

    fw = Path(args.firmware_dir)
    kernel = Path(args.kernel8).read_bytes()
    files: dict[str, bytes] = {
        "kernel8.img": kernel,
        "config.txt": Path(args.config).read_bytes(),
        "cmdline.txt": Path(args.cmdline).read_bytes(),
    }
    # Also ship kernel_2712.img copy for Pi 5 preference
    files["KERNEL_2712.IMG"] = kernel

    for name in [
        "bootcode.bin",
        "start.elf",
        "start4.elf",
        "fixup.dat",
        "fixup4.dat",
    ]:
        p = fw / name
        if p.exists():
            files[name] = p.read_bytes()

    for dtb in fw.glob("*.dtb"):
        files[dtb.name] = dtb.read_bytes()

    if fw.joinpath("LICENSE.txt").exists():
        files["LICENSE.TXT"] = (fw / "LICENSE.txt").read_bytes()
    if fw.joinpath("MANIFEST.txt").exists():
        files["FWMANIFEST.TXT"] = (fw / "MANIFEST.txt").read_bytes()

    boot_size = BOOT_PART_SECTORS * SECTOR
    part, reserved, fat_sectors, num_fats, spc, root = fat32_format(boot_size)
    part_b = bytearray(part)
    add_files_fat32(part_b, (reserved, fat_sectors, num_fats, spc, root), files)

    total = TOTAL_SECTORS * SECTOR
    img = bytearray(total)
    write_mbr(img)
    img[PART1_START * SECTOR : PART1_START * SECTOR + boot_size] = part_b
    # PeakFS partition: magic header for first-boot init
    data_off = PART2_START * SECTOR
    img[data_off : data_off + 8] = b"PEAKFS01"

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(img)

    # Manifest + checksums beside image
    import subprocess

    h = hashlib.sha256(img).hexdigest()
    sums = out.parent / "SHA256SUMS"
    sums.write_text(f"{h}  {out.name}\n")
    man = out.parent / "peak-rpi-manifest.txt"
    try:
        rev = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], text=True).strip()
    except Exception:
        rev = "unknown"
    man.write_text(
        f"image={out.name}\nsha256={h}\ngit={rev}\n"
        f"boot_start_lba={PART1_START}\nboot_sectors={BOOT_PART_SECTORS}\n"
        f"data_start_lba={PART2_START}\ndata_sectors={DATA_PART_SECTORS}\n"
        f"files={','.join(sorted(files))}\n"
    )
    print(f"Wrote {out} ({total} bytes) sha256={h}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
