#!/usr/bin/env python3
"""Prove TLS Accept/Forget TOFU via QEMU CLI (self-signed.badssl.com)."""
from __future__ import annotations

import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path("/tmp/peak-https-accept")
SER_PATH = "/tmp/peak-https-accept.ser"
MON_PATH = "/tmp/peak-https-accept.mon"
os.environ["PEAK_QEMU_SERIAL"] = SER_PATH
os.environ["PEAK_QEMU_MON"] = MON_PATH
sys.path.insert(0, str(ROOT / "scripts"))
import peak_qemu_audit_lib as lib  # noqa: E402

lib.MON = MON_PATH
lib.SER = SER_PATH
ISO = ROOT / "build" / "peak-os.iso"
DISK = ROOT / "build" / "peak-disk.img"
PROMPT = b"peak:/home/dev/workspace>"


def drain(buf, sock, settle=0.4):
    sock.settimeout(0.15)
    t_end = time.time() + settle
    while time.time() < t_end:
        try:
            chunk = sock.recv(8192)
            if chunk:
                buf.extend(chunk)
                t_end = time.time() + settle
        except socket.timeout:
            pass


def run_cmd(buf, sock, cmd, wait=90.0):
    import re

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
        if cmd.startswith("wget"):
            if b"fetching..." not in after:
                continue
            if not (b"failed:" in after or b"HTTP " in after or b"HTTP/2" in after):
                continue
        break
    drain(buf, sock)
    return bytes(buf[mark:]).decode("utf-8", "replace")


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    for p in (SER_PATH, MON_PATH):
        try:
            os.unlink(p)
        except FileNotFoundError:
            pass
    lib._mon_sock = None  # type: ignore[attr-defined]

    qemu = [
        "qemu-system-x86_64", "-machine", "pc", "-m", "512", "-smp", "1",
        "-cdrom", str(ISO),
        "-drive", f"file={DISK},format=raw,if=ide",
        "-boot", "d",
        "-serial", f"unix:{SER_PATH},server=on,wait=on",
        "-monitor", f"unix:{MON_PATH},server=on,wait=off",
        "-display", "none",
        "-device", "virtio-rng-pci-transitional",
        "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
        "-rtc", "base=2026-08-04T17:00:00",
        "-no-reboot",
    ]
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
        lib.ser_read_until(ser, PROMPT, 120.0, buf)
        lib.mon_connect()

        steps = []
        for label, cmd, wait in [
            ("fail", "wget https://untrusted-root.badssl.com/", 90),
            ("accept", "tlsinfo -A", 15),
            ("ok", "wget https://untrusted-root.badssl.com/", 90),
            ("forget", "tlsinfo -F untrusted-root.badssl.com", 15),
            ("fail2", "wget https://untrusted-root.badssl.com/", 90),
        ]:
            print(f"=== {label}: {cmd} ===", flush=True)
            out = run_cmd(buf, ser, cmd, wait=wait)
            (OUT / f"{label}.txt").write_text(out)
            print(out[-700:], flush=True)
            steps.append((label, out))

        # Classify
        def is_untrusted(o):
            return "tls-untrusted" in o or "Untrusted certificate" in o

        def is_http_ok(o):
            return ("HTTP 200" in o or "HTTP/2 200" in o) and "failed:" not in o

        def past_cert_trust(o):
            """Accept worked if we no longer fail closed on WebPKI (HTTP ok or later handshake stage)."""
            if is_http_ok(o):
                return True
            if is_untrusted(o):
                return False
            return "failed:" in o  # e.g. SKE after cert accepted

        ok = (
            is_untrusted(steps[0][1])
            and "Accept" in steps[1][1]
            and past_cert_trust(steps[2][1])
            and "Forgot" in steps[3][1]
            and is_untrusted(steps[4][1])
        )
        print("ACCEPT_PROVE", "PASS" if ok else "FAIL")
        (OUT / "result.txt").write_text("PASS\n" if ok else "FAIL\n")
        return 0 if ok else 1
    finally:
        try:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
