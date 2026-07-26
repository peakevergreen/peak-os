#!/usr/bin/env python3
"""B-HTTPS-06 desktop Browser matrix (QEMU screendumps)."""
from __future__ import annotations

import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get("PEAK_HTTPS_GUI", "/tmp/peak-https-gui"))
SER = "/tmp/peak-https-gui.ser"
MON = "/tmp/peak-https-gui.mon"
os.environ["PEAK_QEMU_SERIAL"] = SER
os.environ["PEAK_QEMU_MON"] = MON
os.environ["PEAK_AUDIT_DIR"] = str(OUT)
sys.path.insert(0, str(ROOT / "scripts"))
import peak_qemu_audit_lib as lib  # noqa: E402

lib.MON = MON
lib.SER = SER
lib.AUDIT = OUT
ISO = ROOT / "build" / "peak-os.iso"
DISK = ROOT / "build" / "peak-disk.img"
PROMPT = b"peak:/home/dev/workspace>"


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    for p in (SER, MON):
        try:
            os.unlink(p)
        except FileNotFoundError:
            pass
    lib._mon_sock = None  # type: ignore[attr-defined]

    qemu = [
        "qemu-system-x86_64", "-machine", "pc", "-m", "512",
        "-cdrom", str(ISO),
        "-drive", f"file={DISK},format=raw,if=ide",
        "-boot", "d",
        "-serial", f"unix:{SER},server=on,wait=on",
        "-monitor", f"unix:{MON},server=on,wait=off",
        "-display", "none",
        "-device", "virtio-rng-pci-transitional",
        "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
        "-rtc", "base=2026-07-26T17:00:00",
        "-no-reboot",
    ]
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    rows = []
    try:
        time.sleep(0.3)
        ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(80):
            try:
                ser.connect(SER)
                break
            except OSError:
                time.sleep(0.1)
        buf = bytearray()
        lib.ser_read_until(ser, PROMPT, 120.0, buf)
        lib.mon_connect()
        lib.mouse_reset_estimate()

        # Enter GUI
        lib.sendkeys("g", "u", "i", "ret")
        time.sleep(2.5)
        lib.dump("01-desktop", "after gui")

        # Open Browser via hotkey 3 (typical Peak layout: 1 Term 2 Files 3 Browser)
        # Prefer Peak menu: sendkey for Browser — docs say hotkeys 1-7.
        lib.sendkeys("3")
        time.sleep(1.2)
        lib.dump("02-browser-demo", "peak://demo baseline")

        # Focus URL bar: click approximate address area (scale 3, 1080p)
        # Tab row ~ y=40*3 area; URL bar below tabs.
        lib.mouse_click(400, 90)
        time.sleep(0.2)
        # Select-all / clear: type URL
        for _ in range(40):
            lib.sendkeys("backspace")
        lib.type_text("https://example.com/\n", delay=0.035)
        time.sleep(8.0)
        lib.dump("03-https-example", "HTTPS example.com")
        rows.append(("https://example.com/", "content or lock", "03-https-example.png"))

        # New tab via Ctrl+T if supported, else + area — try 't' with ctrl
        lib.sendkeys("ctrl-t")
        time.sleep(0.8)
        lib.dump("04-new-tab", "new tab")
        rows.append(("new tab", "second tab chrome", "04-new-tab.png"))

        # Navigate peakevergreen
        lib.mouse_click(400, 90)
        for _ in range(50):
            lib.sendkeys("backspace")
        lib.type_text("https://peakevergreen.com/\n", delay=0.035)
        time.sleep(10.0)
        lib.dump("05-https-peak", "peakevergreen.com")
        rows.append(("https://peakevergreen.com/", "content", "05-https-peak.png"))

        # Untrusted page for Accept buttons
        lib.mouse_click(400, 90)
        for _ in range(60):
            lib.sendkeys("backspace")
        lib.type_text("https://untrusted-root.badssl.com/\n", delay=0.035)
        time.sleep(8.0)
        lib.dump("06-tls-untrusted-accept", "Accept/Forget/Retry buttons")
        rows.append(("untrusted-root Accept UX", "Retry Accept Forget", "06-tls-untrusted-accept.png"))

        # HTTP example (plain)
        lib.mouse_click(400, 90)
        for _ in range(60):
            lib.sendkeys("backspace")
        lib.type_text("http://example.com/\n", delay=0.035)
        time.sleep(5.0)
        lib.dump("07-http-example", "plain HTTP")
        rows.append(("http://example.com/", "content", "07-http-example.png"))

        # Expired for honesty
        lib.mouse_click(400, 90)
        for _ in range(60):
            lib.sendkeys("backspace")
        lib.type_text("https://expired.badssl.com/\n", delay=0.035)
        time.sleep(8.0)
        lib.dump("08-tls-expired", "expired title")
        rows.append(("expired.badssl.com", "tls-expired page", "08-tls-expired.png"))

        md = ["# B-HTTPS-06 GUI matrix", ""]
        for url, exp, dump in rows:
            md.append(f"- **{url}**: expected `{exp}` — dump `{dump}`")
        (OUT / "matrix.md").write_text("\n".join(md) + "\n")
        print("GUI matrix dumps in", OUT)
        for p in sorted(OUT.glob("*.png")):
            print(" ", p.name, p.stat().st_size)
        return 0
    finally:
        try:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
