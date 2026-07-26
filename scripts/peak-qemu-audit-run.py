#!/usr/bin/env python3
"""Launch Peak OS under QEMU and run a visual audit with screendumps.

Keeps one long-lived serial connection from boot (avoids missing the prompt).
Monitor socket used for screendump + sendkey.
"""
from __future__ import annotations

import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MON = "/tmp/peak-qemu.mon"
SER = "/tmp/peak-serial.sock"
AUDIT = Path("/tmp/peak-audit")
ISO = ROOT / "build" / "peak-os.iso"
DISK = ROOT / "build" / "peak-disk.img"


def mon_cmd(cmd: str) -> str:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5.0)
    s.connect(MON)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.sendall((cmd.strip() + "\n").encode())
    time.sleep(0.2)
    out = b""
    s.settimeout(0.4)
    try:
        while True:
            b = s.recv(4096)
            if not b:
                break
            out += b
    except socket.timeout:
        pass
    s.close()
    return out.decode("utf-8", "replace")


def dump(name: str) -> Path:
    AUDIT.mkdir(parents=True, exist_ok=True)
    path = AUDIT / name
    mon_cmd(f"screendump {path}")
    time.sleep(0.3)
    if not path.is_file() or path.stat().st_size < 100:
        raise RuntimeError(f"bad screendump {path}")
    print(f"DUMP {path} ({path.stat().st_size} bytes)")
    return path


def sendkeys(*keys: str) -> None:
    for k in keys:
        mon_cmd(f"sendkey {k}")
        time.sleep(0.06)


def char_keys(ch: str) -> list[str]:
    if ch == "\n":
        return ["ret"]
    if ch == " ":
        return ["spc"]
    if ch == "-":
        return ["minus"]
    if ch == "/":
        return ["slash"]
    if ch == ".":
        return ["dot"]
    if "A" <= ch <= "Z":
        return [f"shift-{ch.lower()}"]
    if ("a" <= ch <= "z") or ("0" <= ch <= "9"):
        return [ch]
    raise RuntimeError(f"unsupported {ch!r}")


def type_text(text: str) -> None:
    for ch in text:
        for k in char_keys(ch):
            sendkeys(k)


def ser_read_until(ser: socket.socket, needle: bytes, timeout: float, buf: bytearray) -> bytes:
    deadline = time.time() + timeout
    ser.settimeout(1.0)
    while time.time() < deadline:
        if needle in buf:
            return bytes(buf)
        try:
            chunk = ser.recv(4096)
            if chunk:
                buf.extend(chunk)
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()
        except socket.timeout:
            pass
    raise RuntimeError(f"timeout for {needle!r}; tail={bytes(buf[-300:])!r}")


def ser_write(ser: socket.socket, text: str) -> None:
    ser.sendall(text.encode())


def main() -> int:
    os.environ["PATH"] = "/opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin:" + os.environ.get(
        "PATH", ""
    )
    if not ISO.is_file():
        subprocess.check_call(["make", "iso"], cwd=ROOT)
    if not DISK.is_file():
        subprocess.check_call(
            ["dd", "if=/dev/zero", f"of={DISK}", "bs=1048576", "count=32"], cwd=ROOT
        )

    for p in (MON, SER):
        try:
            os.unlink(p)
        except FileNotFoundError:
            pass

    qemu = [
        "qemu-system-x86_64",
        "-machine",
        "pc",
        "-m",
        "512",
        "-smp",
        "1",
        "-cdrom",
        str(ISO),
        "-drive",
        f"file={DISK},format=raw,if=ide",
        "-boot",
        "d",
        # wait=on: hold reset until serial client connects so we never miss boot log
        "-serial",
        f"unix:{SER},server=on,wait=on",
        "-monitor",
        f"unix:{MON},server=on,wait=off",
        "-display",
        "none",
        "-device",
        "virtio-rng-pci-transitional",
        "-netdev",
        "user,id=net0",
        "-device",
        "e1000,netdev=net0",
        "-no-reboot",
    ]
    print("Starting QEMU…")
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def cleanup(*_a):
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    # Connect serial ASAP (QEMU waited for us)
    time.sleep(0.3)
    ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    ser.settimeout(5.0)
    for _ in range(50):
        try:
            ser.connect(SER)
            break
        except OSError:
            time.sleep(0.1)
    else:
        cleanup()
        raise SystemExit("serial connect failed")

    buf = bytearray()
    # Bare "peak:/" matches mid-banner; wait for the interactive prompt.
    prompt = b"peak:/home/dev/workspace>"
    print(f"Waiting for {prompt.decode()} …")
    ser_read_until(ser, prompt, 120.0, buf)
    print("\n==> at shell; dumping CLI")
    # monitor may need a moment
    time.sleep(0.5)
    dump("00-cli-shell.ppm")

    print("==> gui (sendkey — more reliable than early serial)")
    sendkeys("g", "u", "i", "ret")
    time.sleep(4.0)
    dump("01-login-or-desktop.ppm")

    # Dismiss login if present: Enter
    sendkeys("ret")
    time.sleep(1.5)
    dump("02-desktop.ppm")

    print("==> hotkey 1 Terminal")
    sendkeys("1")
    time.sleep(1.5)
    dump("03-terminal.ppm")

    print("==> hotkey 2 Files")
    sendkeys("2")
    time.sleep(1.5)
    dump("04-files.ppm")

    print("==> hotkey 3 Settings")
    sendkeys("3")
    time.sleep(1.5)
    dump("05-settings.ppm")

    print("==> hotkey 6 Browser")
    sendkeys("6")
    time.sleep(2.0)
    dump("06-browser.ppm")

    print("==> hotkey 7 Monitor")
    sendkeys("7")
    time.sleep(1.5)
    dump("07-monitor.ppm")

    print("==> Alt-Tab")
    sendkeys("alt-tab")
    time.sleep(1.0)
    dump("08-alttab.ppm")

    print("==> Esc dismiss")
    sendkeys("esc")
    time.sleep(0.5)

    # Ctrl+Alt+Esc back to CLI
    print("==> leave desktop")
    sendkeys("ctrl-alt-esc")
    time.sleep(2.0)
    dump("09-back-cli.ppm")
    try:
        ser_read_until(ser, prompt, 30.0, buf)
        print("\n==> back at CLI OK")
    except RuntimeError as e:
        print(f"WARN: {e}")

    notes = AUDIT / "NOTES.txt"
    notes.write_text(
        f"QEMU audit run at {time.ctime()}\n"
        f"dumps in {AUDIT} (PPM from QEMU screendump)\n"
        f"serial tail:\n{bytes(buf[-1500:]).decode('utf-8', 'replace')}\n"
    )
    print(f"Wrote {notes}")
    cleanup()
    print("DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
