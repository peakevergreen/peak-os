#!/usr/bin/env python3
"""CLI HTTPS diagnosis for Peak Browser WebPKI campaign (QEMU user-net)."""
from __future__ import annotations

import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get("PEAK_HTTPS_DIAG", "/tmp/peak-https-diag"))
SER_PATH = os.environ.get("PEAK_QEMU_SERIAL", "/tmp/peak-https.ser")
MON_PATH = os.environ.get("PEAK_QEMU_MON", "/tmp/peak-https.mon")
os.environ["PEAK_QEMU_SERIAL"] = SER_PATH
os.environ["PEAK_QEMU_MON"] = MON_PATH
sys.path.insert(0, str(ROOT / "scripts"))
import peak_qemu_audit_lib as lib  # noqa: E402

# Re-bind in case the module was already imported with other defaults.
lib.MON = MON_PATH
lib.SER = SER_PATH

ISO = ROOT / "build" / "peak-os.iso"
DISK = ROOT / "build" / "peak-disk.img"

URLS = [
    "https://example.com/",
    "https://www.cloudflare.com/",
    "https://peakevergreen.com/",
    "https://www.google.com/",
    "https://letsencrypt.org/",
    "https://github.com/",
    "http://example.com/",
]

PROMPT = b"peak:/home/dev/workspace>"


def clean_socks() -> None:
    for p in (SER_PATH, MON_PATH):
        try:
            os.unlink(p)
        except FileNotFoundError:
            pass


def wait_prompt(buf: bytearray, sock: socket.socket, timeout: float = 120.0) -> bool:
    t0 = time.time()
    sock.settimeout(1.0)
    while time.time() - t0 < timeout:
        if PROMPT in bytes(buf):
            # Prefer idle prompt without job/status suffix when possible.
            text = bytes(buf)
            if b"peak:/home/dev/workspace> " in text or text.rstrip().endswith(PROMPT):
                return True
            # Also accept status-suffixed prompts like "... [1]>"
            if b"peak:/home/dev/workspace [" in text and b"]>" in text:
                return True
        try:
            chunk = sock.recv(8192)
            if chunk:
                buf.extend(chunk)
        except socket.timeout:
            pass
    return False


def drain(buf: bytearray, sock: socket.socket, settle: float = 0.35) -> None:
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


def run_cmd(buf: bytearray, sock: socket.socket, cmd: str, wait: float = 25.0) -> str:
    """Type a shell command and wait until it finishes (new prompt after output)."""
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
            # Do not treat the typing echo as completion.
            if b"fetching..." not in after:
                continue
            if not (b"failed:" in after or b"HTTP " in after or b"HTTP/2" in after or b"saved " in after):
                continue
        break
    drain(buf, sock, 0.5)
    return bytes(buf[mark:]).decode("utf-8", "replace")


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
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
        "-rtc", "base=2026-08-04T17:00:00",
        "-no-reboot",
    ]
    print("starting QEMU…", flush=True)
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.3)
        ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        ser.settimeout(5.0)
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
        print("waiting for shell prompt...")
        try:
            lib.ser_read_until(ser, PROMPT, 120.0, buf)
        except RuntimeError as e:
            (OUT / "boot-fail.txt").write_text(bytes(buf).decode("utf-8", "replace")[-8000:])
            print(f"prompt timeout: {e}")
            return 1
        print("prompt ok")
        lib.mon_connect()

        results = []
        for label, cmd, wait in [
            ("date", "date", 8),
            ("ifconfig", "ifconfig", 10),
            ("tlsinfo", "tlsinfo", 10),
            ("nslookup-example", "nslookup example.com", 20),
        ]:
            print(f"=== {label} ===")
            out = run_cmd(buf, ser, cmd, wait=wait)
            (OUT / f"{label}.txt").write_text(out)
            print(out[-600:])
            results.append((label, out))

        for url in URLS:
            safe = url.replace("://", "_").replace("/", "_").replace(".", "-")
            print(f"=== wget {url} ===")
            out = run_cmd(buf, ser, f"wget {url}", wait=90)
            (OUT / f"wget_{safe}.txt").write_text(out)
            # Follow with tlsinfo for fail reason
            info = run_cmd(buf, ser, "tlsinfo", wait=10)
            (OUT / f"tlsinfo_after_{safe}.txt").write_text(info)
            print(out[-900:])
            print("--- tlsinfo ---")
            print(info[-500:])
            results.append((url, out))

        summary = []
        for name, out in results:
            # Classify from the command body only (before trailing tlsinfo).
            body = out.split("---", 1)[0] if "---" in out else out
            # Prefer the last status-bearing line.
            status = "unknown"
            if name in ("date", "ifconfig", "tlsinfo", "nslookup-example"):
                status = "INFO"
            elif "HTTP 200" in body or "HTTP/2 200" in body:
                status = "OK"
            elif "tls-expired" in body or "expired or not yet valid" in body.lower():
                status = "TLS_EXPIRED"
            elif "tls-mismatch" in body or "hostname mismatch" in body.lower():
                status = "TLS_MISMATCH"
            elif "tls-untrusted" in body or "WebPKI" in body or "signature verify failed" in body:
                status = "TLS_UNTRUSTED"
            elif "DNS failed" in body or "Could not resolve" in body:
                status = "DNS_FAIL"
            elif "failed:" in body:
                status = "FAIL"
            elif "timeout" in body.lower():
                status = "TIMEOUT"
            summary.append(f"{status}\t{name}")
        (OUT / "summary.tsv").write_text("\n".join(summary) + "\n")
        (OUT / "serial-tail.txt").write_text(bytes(buf).decode("utf-8", "replace")[-20000:])
        print("SUMMARY:")
        print("\n".join(summary))
        print(f"artifacts: {OUT}")
        return 0
    finally:
        try:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        clean_socks()


if __name__ == "__main__":
    raise SystemExit(main())
