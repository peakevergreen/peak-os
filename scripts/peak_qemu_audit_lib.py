#!/usr/bin/env python3
"""Shared QEMU audit helpers: monitor, serial, mouse, PPM→PNG, findings."""
from __future__ import annotations

import hashlib
import json
import os
import socket
import struct
import time
import zlib
from pathlib import Path
from typing import Any

MON = os.environ.get("PEAK_QEMU_MON", "/tmp/peak-qemu.mon")
SER = os.environ.get("PEAK_QEMU_SERIAL", "/tmp/peak-serial.sock")
AUDIT = Path(os.environ.get("PEAK_AUDIT_DIR", "/tmp/peak-audit-v2"))
PROMPT = b"peak:/home/dev/workspace>"

# Guest cursor estimate (Peak mouse_init starts at 100,100).
_cx = 100
_cy = 100


_mon_sock: socket.socket | None = None


def mon_connect() -> socket.socket:
    global _mon_sock
    if _mon_sock is not None:
        return _mon_sock
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5.0)
    s.connect(MON)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    _mon_sock = s
    return s


def mon_cmd(cmd: str, settle: float = 0.12) -> str:
    s = mon_connect()
    s.sendall((cmd.strip() + "\n").encode())
    if settle:
        time.sleep(settle)
    out = b""
    s.settimeout(0.05 if settle == 0.0 else 0.35)
    try:
        while True:
            b = s.recv(4096)
            if not b:
                break
            out += b
    except socket.timeout:
        pass
    s.settimeout(5.0)
    return out.decode("utf-8", "replace")


def sendkeys(*keys: str, delay: float = 0.055) -> None:
    for k in keys:
        mon_cmd(f"sendkey {k}", settle=0.05)
        time.sleep(delay)


def char_keys(ch: str) -> list[str]:
    if ch == "\n":
        return ["ret"]
    if ch == "\t":
        return ["tab"]
    if ch == " ":
        return ["spc"]
    if ch == "-":
        return ["minus"]
    if ch == "=":
        return ["equal"]
    if ch == "/":
        return ["slash"]
    if ch == ".":
        return ["dot"]
    if ch == ",":
        return ["comma"]
    if ch == ";":
        return ["semicolon"]
    if ch == "'":
        return ["apostrophe"]
    if ch == "?":
        return ["shift-slash"]
    if ch == ":":
        return ["shift-semicolon"]
    if ch == "_":
        return ["shift-minus"]
    if "A" <= ch <= "Z":
        return [f"shift-{ch.lower()}"]
    if ("a" <= ch <= "z") or ("0" <= ch <= "9"):
        return [ch]
    raise RuntimeError(f"unsupported char {ch!r}")


def type_text(text: str, delay: float = 0.04) -> None:
    for ch in text:
        for k in char_keys(ch):
            sendkeys(k, delay=delay)


def mouse_move_rel(dx: int, dy: int) -> None:
    global _cx, _cy
    mon_cmd(f"mouse_move {int(dx)} {int(dy)}", settle=0.0)
    # Mirror Peak mouse_accel_delta for estimate tracking
    def accel(d: int) -> int:
        if d == 0:
            return 0
        sign = -1 if d < 0 else 1
        m = abs(d)
        if m <= 2:
            return d
        m += m // 3
        return sign * (127 if m > 127 else m)

    _cx = max(0, min(1919, _cx + accel(dx)))
    _cy = max(0, min(1079, _cy + accel(dy)))


def mouse_goto(x: int, y: int, step: int = 2) -> None:
    """Relative crawl toward absolute guest coords (estimate)."""
    global _cx, _cy
    x = max(0, min(1919, int(x)))
    y = max(0, min(1079, int(y)))
    # Prefer unaccelerated packets (dx,dy in -2..2) for accuracy, but batch fast.
    while _cx != x or _cy != y:
        dx = max(-step, min(step, x - _cx))
        dy = max(-step, min(step, y - _cy))
        if dx == 0 and dy == 0:
            break
        mouse_move_rel(dx, dy)
    time.sleep(0.03)


def mouse_reset_estimate(x: int = 100, y: int = 100) -> None:
    global _cx, _cy
    _cx, _cy = x, y


def mouse_click(x: int | None = None, y: int | None = None, button: int = 1) -> None:
    """button: 1=left, 2=right, 4=middle (QEMU mask)."""
    if x is not None and y is not None:
        mouse_goto(x, y)
    mon_cmd(f"mouse_button {button}", settle=0.05)
    time.sleep(0.08)
    mon_cmd("mouse_button 0", settle=0.05)
    time.sleep(0.12)


def mouse_drag(x0: int, y0: int, x1: int, y1: int, step: int = 4) -> None:
    mouse_goto(x0, y0, step=2)
    mon_cmd("mouse_button 1", settle=0.05)
    time.sleep(0.05)
    mouse_goto(x1, y1, step=step)
    time.sleep(0.05)
    mon_cmd("mouse_button 0", settle=0.05)
    time.sleep(0.15)


def mouse_wheel(delta: int = -1) -> None:
    # QEMU: mouse_button with wheel not always available; use wheel via move z if supported.
    # Fall back: many builds use 'mouse_button' only for buttons. Try wheel event via sendkey pgdn.
    if delta < 0:
        sendkeys("pgdn")
    else:
        sendkeys("pgup")


def ppm_to_png(ppm: Path, png: Path) -> None:
    data = ppm.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError(f"not P6 PPM: {ppm}")
    i = 3
    while data[i] in b" \t\r\n":
        i += 1
    if data[i] == ord("#"):
        while data[i] not in b"\n":
            i += 1
        i += 1
    dims: list[int] = []
    while len(dims) < 3:
        while data[i] in b" \t\r\n":
            i += 1
        j = i
        while data[i] not in b" \t\r\n":
            i += 1
        dims.append(int(data[j:i]))
    while data[i] in b" \t\r\n":
        i += 1
    w, h, maxv = dims
    if maxv != 255:
        raise RuntimeError("expected maxval 255")
    raw = data[i : i + w * h * 3]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    rows = b"".join(b"\x00" + raw[y * w * 3 : (y + 1) * w * 3] for y in range(h))
    png.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 6))
        + chunk(b"IEND", b"")
    )


def dump(name: str, note: str = "") -> Path:
    AUDIT.mkdir(parents=True, exist_ok=True)
    stem = name if not name.endswith((".ppm", ".png")) else Path(name).stem
    ppm = AUDIT / f"{stem}.ppm"
    png = AUDIT / f"{stem}.png"
    mon_cmd(f"screendump {ppm}")
    time.sleep(0.28)
    if not ppm.is_file() or ppm.stat().st_size < 100:
        raise RuntimeError(f"bad screendump {ppm}")
    ppm_to_png(ppm, png)
    h = hashlib.md5(ppm.read_bytes()[-50000:]).hexdigest()[:10]
    print(f"DUMP {stem} md5tail={h} {note}")
    return png


def dump_hash(name: str) -> str:
    p = AUDIT / f"{Path(name).stem}.ppm"
    if not p.is_file():
        return ""
    return hashlib.md5(p.read_bytes()).hexdigest()


def find_log(finding: dict[str, Any]) -> None:
    AUDIT.mkdir(parents=True, exist_ok=True)
    path = AUDIT / "FINDINGS.jsonl"
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(finding, ensure_ascii=False) + "\n")
    print(f"FINDING {finding.get('id', '?')}: {finding.get('actual', '')[:80]}")


def ser_read_until(ser: socket.socket, needle: bytes, timeout: float, buf: bytearray) -> bytes:
    deadline = time.time() + timeout
    ser.settimeout(1.0)
    while time.time() < deadline:
        if needle in buf:
            return bytes(buf)
        try:
            chunk = ser.recv(8192)
            if chunk:
                buf.extend(chunk)
        except socket.timeout:
            pass
    raise RuntimeError(f"timeout for {needle!r}; tail={bytes(buf[-400:])!r}")


# Scale-3 layout helpers (settings default gui_scale=3)
SCALE = 3


def u(v: int) -> int:
    return v * SCALE


def peak_btn_xy() -> tuple[int, int]:
    """Center of Peak start button at scale 3, 1080p."""
    # fb_char_h=16*scale, cell_h=char_h+scale, taskbar_h=cell_h+12*scale
    cell_h = 16 * SCALE + SCALE
    th = cell_h + u(12)
    return (u(8) + u(60) // 2, 1080 - th // 2)


def win_close_xy(wx: int = 200, wy: int = 40, ww: int = 900) -> tuple[int, int]:
    return (wx + ww - u(15), wy + u(12))
