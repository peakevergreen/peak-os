#!/usr/bin/env python3
"""Drive Peak OS QEMU audit via monitor (screendump/sendkey) + serial socket.

Usage (QEMU already running with:
  -monitor unix:/tmp/peak-qemu.mon,server,nowait
  -serial unix:/tmp/peak-serial.sock,server,nowait
):

  ./scripts/peak-qemu-audit-drive.py wait-prompt
  # waits for peak:/home/dev/workspace> (not bare peak:/ — that matches boot too early)
  ./scripts/peak-qemu-audit-drive.py type 'gui\\n'
  ./scripts/peak-qemu-audit-drive.py dump /tmp/peak-audit/01-login.png
  ./scripts/peak-qemu-audit-drive.py sendkey ret
"""
from __future__ import annotations

import argparse
import os
import socket
import sys
import time


MON = os.environ.get("PEAK_QEMU_MON", "/tmp/peak-qemu.mon")
SER = os.environ.get("PEAK_QEMU_SERIAL", "/tmp/peak-serial.sock")


def mon_cmd(cmd: str, timeout: float = 5.0) -> str:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(MON)
    # QEMU greets with banner; drain then send
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.sendall((cmd.strip() + "\n").encode())
    time.sleep(0.15)
    chunks = []
    s.settimeout(0.5)
    try:
        while True:
            b = s.recv(4096)
            if not b:
                break
            chunks.append(b)
    except socket.timeout:
        pass
    s.close()
    return b"".join(chunks).decode("utf-8", "replace")


def ser_connect(timeout: float = 30.0) -> socket.socket:
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(2.0)
            s.connect(SER)
            return s
        except OSError as e:
            last = e
            time.sleep(0.2)
    raise SystemExit(f"serial connect failed: {last}")


def ser_read_until(s: socket.socket, needle: bytes, timeout: float = 90.0) -> bytes:
    deadline = time.time() + timeout
    buf = b""
    s.settimeout(1.0)
    while time.time() < deadline:
        try:
            chunk = s.recv(4096)
            if chunk:
                buf += chunk
                if needle in buf:
                    return buf
        except socket.timeout:
            pass
    raise SystemExit(f"timeout waiting for {needle!r}; got {buf[-400:]!r}")


def ser_write(s: socket.socket, data: str) -> None:
    s.sendall(data.encode())


def char_to_sendkey(ch: str) -> list[str]:
    """Map a character to QEMU sendkey sequence(s)."""
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
    if ch == "[":
        return ["bracket_left"]
    if ch == "]":
        return ["bracket_right"]
    if ch == "\\":
        return ["backslash"]
    if ch == "`":
        return ["grave_accent"]
    shift_map = {
        "!": "1",
        "@": "2",
        "#": "3",
        "$": "4",
        "%": "5",
        "^": "6",
        "&": "7",
        "*": "8",
        "(": "9",
        ")": "0",
        "_": "minus",
        "+": "equal",
        "{": "bracket_left",
        "}": "bracket_right",
        "|": "backslash",
        ":": "semicolon",
        '"': "apostrophe",
        "<": "comma",
        ">": "dot",
        "?": "slash",
        "~": "grave_accent",
    }
    if ch in shift_map:
        return [f"shift-{shift_map[ch]}"]
    if "A" <= ch <= "Z":
        return [f"shift-{ch.lower()}"]
    if "a" <= ch <= "z" or "0" <= ch <= "9":
        return [ch]
    raise SystemExit(f"unsupported char for sendkey: {ch!r}")


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("ping-mon")
    p_dump = sub.add_parser("dump")
    p_dump.add_argument("path")
    p_sk = sub.add_parser("sendkey")
    p_sk.add_argument("keys", nargs="+", help="QEMU sendkey names, e.g. ret ctrl-alt-esc")
    p_type = sub.add_parser("type")
    p_type.add_argument("text", help="Literal text; use \\n for Enter")
    p_wait = sub.add_parser("wait-prompt")
    p_wait.add_argument(
        "--needle",
        default="peak:/home/dev/workspace>",
        help="Serial substring that means the interactive shell is ready",
    )
    p_wait.add_argument("--timeout", type=float, default=120.0)
    p_ser = sub.add_parser("serial-type")
    p_ser.add_argument("text", help="Send on serial (supports \\n)")
    p_read = sub.add_parser("serial-read")
    p_read.add_argument("--seconds", type=float, default=1.0)
    p_mouse = sub.add_parser("mouse")
    p_mouse.add_argument("x", type=int)
    p_mouse.add_argument("y", type=int)
    p_mouse.add_argument("--click", action="store_true")
    p_mouse.add_argument("--button", type=int, default=1)
    p_png = sub.add_parser("ppm2png")
    p_png.add_argument("ppm")
    p_png.add_argument("png", nargs="?")
    p_hash = sub.add_parser("hash")
    p_hash.add_argument("path")

    args = ap.parse_args()

    if args.cmd == "ping-mon":
        out = mon_cmd("info status")
        print(out)
        return 0

    if args.cmd == "dump":
        path = os.path.abspath(args.path)
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        out = mon_cmd(f"screendump {path}")
        print(out)
        if not os.path.isfile(path):
            print(f"ERROR: screendump missing: {path}", file=sys.stderr)
            return 1
        print(f"ok {path} ({os.path.getsize(path)} bytes)")
        # Auto-convert if .ppm or sibling png requested
        if path.endswith(".ppm") or path.endswith(".png"):
            try:
                sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
                from peak_qemu_audit_lib import ppm_to_png

                ppm = path if path.endswith(".ppm") else path[:-4] + ".ppm"
                png = path if path.endswith(".png") else path[:-4] + ".png"
                if os.path.isfile(ppm):
                    ppm_to_png(__import__("pathlib").Path(ppm), __import__("pathlib").Path(png))
                    print(f"png {png}")
            except Exception as e:
                print(f"ppm2png warn: {e}", file=sys.stderr)
        return 0

    if args.cmd == "sendkey":
        for k in args.keys:
            mon_cmd(f"sendkey {k}")
            time.sleep(0.05)
        return 0

    if args.cmd == "type":
        text = args.text.encode().decode("unicode_escape")
        for ch in text:
            for k in char_to_sendkey(ch):
                mon_cmd(f"sendkey {k}")
                time.sleep(0.03)
        return 0

    if args.cmd == "wait-prompt":
        s = ser_connect(timeout=args.timeout)
        buf = ser_read_until(s, args.needle.encode(), timeout=args.timeout)
        print(buf.decode("utf-8", "replace")[-500:])
        s.close()
        return 0

    if args.cmd == "serial-type":
        text = args.text.encode().decode("unicode_escape")
        s = ser_connect()
        s.settimeout(0.2)
        try:
            while s.recv(4096):
                pass
        except socket.timeout:
            pass
        ser_write(s, text)
        time.sleep(0.2)
        s.close()
        return 0

    if args.cmd == "serial-read":
        s = ser_connect()
        s.settimeout(args.seconds)
        try:
            data = s.recv(65536)
        except socket.timeout:
            data = b""
        sys.stdout.buffer.write(data)
        s.close()
        return 0

    if args.cmd == "mouse":
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from peak_qemu_audit_lib import mouse_click, mouse_goto

        if args.click:
            mouse_click(args.x, args.y, button=args.button)
        else:
            mouse_goto(args.x, args.y)
        print(f"mouse {'click' if args.click else 'move'} {args.x},{args.y}")
        return 0

    if args.cmd == "ppm2png":
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from pathlib import Path

        from peak_qemu_audit_lib import ppm_to_png

        ppm = Path(args.ppm)
        png = Path(args.png) if args.png else ppm.with_suffix(".png")
        ppm_to_png(ppm, png)
        print(png)
        return 0

    if args.cmd == "hash":
        import hashlib

        data = open(args.path, "rb").read()
        print(hashlib.md5(data).hexdigest())
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
