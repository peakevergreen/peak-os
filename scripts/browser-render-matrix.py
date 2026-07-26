#!/usr/bin/env python3
"""QEMU CLI matrix: non-empty HTTPS bodies for Browser render maturity (BR-13)."""
from __future__ import annotations

import os
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get("PEAK_RENDER_DIAG", "/tmp/peak-render-matrix"))
SER_PATH = os.environ.get("PEAK_QEMU_SERIAL", "/tmp/peak-render.ser")
MON_PATH = os.environ.get("PEAK_QEMU_MON", "/tmp/peak-render.mon")
os.environ["PEAK_QEMU_SERIAL"] = SER_PATH
os.environ["PEAK_QEMU_MON"] = MON_PATH
sys.path.insert(0, str(ROOT / "scripts"))
import peak_qemu_audit_lib as lib  # noqa: E402

lib.MON = MON_PATH
lib.SER = SER_PATH

ISO = ROOT / "build" / "peak-os.iso"
DISK = ROOT / "build" / "peak-disk.img"

URLS = [
    "https://example.com/",
    "https://peakevergreen.com/",
    "https://www.fark.com/",
    "https://www.google.com/",
    "https://en.wikipedia.org/wiki/Main_Page",
]

PROMPT = b"peak:/home/dev/workspace>"


def clean_socks() -> None:
    for p in (SER_PATH, MON_PATH):
        try:
            os.unlink(p)
        except FileNotFoundError:
            pass


def run_cmd(buf: bytearray, sock: socket.socket, cmd: str, wait: float = 90.0) -> str:
    mark = len(buf)
    lib.type_text(cmd + "\n", delay=0.028)
    t0 = time.time()
    sock.settimeout(0.5)
    cmd_b = cmd.encode()
    prompt_re = re.compile(br"\npeak:/home/dev/workspace(?: \[\d+\])?> ?$")
    while time.time() - t0 < wait:
        try:
            chunk = sock.recv(8192)
            if chunk:
                buf.extend(chunk)
        except socket.timeout:
            pass
        tail = bytes(buf[mark:])
        idx = tail.rfind(cmd_b)
        if idx < 0:
            continue
        after = tail[idx + len(cmd_b) :]
        if not prompt_re.search(after):
            continue
        if cmd.startswith("wget") and b"fetching..." not in after:
            continue
        if cmd.startswith("wget") and not (
            b"failed:" in after
            or b"HTTP " in after
            or b"HTTP/2" in after
            or b"saved " in after
            or b"bytes" in after
        ):
            continue
        break
    sock.settimeout(0.15)
    t_end = time.time() + 0.4
    while time.time() < t_end:
        try:
            chunk = sock.recv(8192)
            if chunk:
                buf.extend(chunk)
                t_end = time.time() + 0.4
        except socket.timeout:
            pass
    return bytes(buf[mark:]).decode("utf-8", "replace")


def body_ok(out: str) -> bool:
    if "failed:" in out and "HTTP/2 200" not in out and "HTTP 200" not in out:
        return False
    # Prefer explicit byte counts from wget/curl Peak output.
    m = re.search(r"(\d+)\s*bytes", out, re.I)
    if m and int(m.group(1)) > 0:
        return True
    if re.search(r"HTTP/2?\s*200", out) and "0 bytes" not in out:
        # If status OK and not explicitly empty, accept when HTML markers appear.
        if "<html" in out.lower() or "<!doctype" in out.lower() or "saved" in out.lower():
            return True
        # Peak often prints "HTTP/2 200" then length.
        m2 = re.search(r"HTTP/2?\s*200[^\n]*?(\d+)", out)
        if m2 and int(m2.group(1)) > 0:
            return True
    return False


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    if not ISO.exists():
        print("missing ISO — run make iso first")
        return 1
    clean_socks()
    lib._mon_sock = None  # type: ignore[attr-defined]

    qemu = [
        "qemu-system-x86_64",
        "-machine", "pc",
        "-m", "512",
        "-smp", "1",
        "-cdrom", str(ISO),
        "-drive", f"file={DISK},format=raw,if=ide",
        "-boot", "d",
        "-serial", f"unix:{SER_PATH},server=on,wait=on",
        "-monitor", f"unix:{MON_PATH},server=on,wait=off",
        "-display", "none",
        "-device", "virtio-rng-pci-transitional",
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-rtc", "base=2026-07-26T17:00:00",
        "-no-reboot",
    ]
    print("starting QEMU…", flush=True)
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fails = 0
    try:
        time.sleep(0.3)
        ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(80):
            try:
                ser.connect(SER_PATH)
                break
            except OSError:
                time.sleep(0.1)
        else:
            print("serial connect failed")
            return 1
        buf = bytearray()
        try:
            lib.ser_read_until(ser, PROMPT, 120.0, buf)
        except RuntimeError as e:
            print(f"prompt timeout: {e}")
            return 1
        lib.mon_connect()

        lines = []
        for url in URLS:
            print(f"=== {url} ===", flush=True)
            out = run_cmd(buf, ser, f"wget -O - {url}", wait=100)
            safe = re.sub(r"[^a-zA-Z0-9]+", "_", url).strip("_")
            (OUT / f"{safe}.txt").write_text(out)
            ok = body_ok(out)
            status = "PASS" if ok else "FAIL"
            if not ok:
                fails += 1
            line = f"{status}\t{url}"
            lines.append(line)
            print(line)
            print(out[-700:])

        (OUT / "summary.tsv").write_text("\n".join(lines) + "\n")
        print(f"artifacts: {OUT}")
        print("RENDER_MATRIX", "PASS" if fails == 0 else f"FAIL ({fails})")
        return 0 if fails == 0 else 1
    finally:
        try:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        clean_socks()


if __name__ == "__main__":
    sys.exit(main())
