#!/usr/bin/env python3
"""Dense QEMU GUI audit scenario runner (harvest phase).

Writes dumps to $PEAK_AUDIT_DIR (default /tmp/peak-audit-v2) and FINDINGS.jsonl.
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
sys.path.insert(0, str(ROOT / "scripts"))

from peak_qemu_audit_lib import (  # noqa: E402
    AUDIT,
    MON,
    PROMPT,
    SER,
    dump,
    dump_hash,
    find_log,
    mouse_click,
    mouse_drag,
    mouse_goto,
    mouse_reset_estimate,
    peak_btn_xy,
    sendkeys,
    ser_read_until,
    type_text,
    u,
)

ISO = ROOT / "build" / "peak-os.iso"
DISK = ROOT / "build" / "peak-disk.img"
STEP = 0


def step(name: str, note: str = "") -> Path:
    global STEP
    STEP += 1
    return dump(f"{STEP:03d}-{name}", note)


def finding(sev: str, area: str, repro: str, expected: str, actual: str, dump_name: str) -> None:
    find_log(
        {
            "sev": sev,
            "area": area,
            "repro": repro,
            "expected": expected,
            "actual": actual,
            "dump": dump_name,
            "status": "candidate",
        }
    )


def boot_qemu() -> tuple[subprocess.Popen, socket.socket, bytearray]:
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

    AUDIT.mkdir(parents=True, exist_ok=True)
    findings = AUDIT / "FINDINGS.jsonl"
    if findings.exists():
        findings.unlink()

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
    print("Starting QEMU…", flush=True)
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def cleanup(*_a):
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    time.sleep(0.3)
    ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    ser.settimeout(5.0)
    for _ in range(80):
        try:
            ser.connect(SER)
            break
        except OSError:
            time.sleep(0.1)
    else:
        cleanup()
        raise SystemExit("serial connect failed")

    buf = bytearray()
    print("Waiting for prompt…", flush=True)
    ser_read_until(ser, PROMPT, 120.0, buf)
    return proc, ser, buf


def enter_gui(buf: bytearray) -> None:
    # PeakDisk line
    text = bytes(buf).decode("latin1", "replace")
    if "Disk (none)" in text:
        finding(
            "P1",
            "PeakDisk",
            "boot with peak-disk.img on -machine pc",
            "Disk (empty) or restore",
            "Disk (none) in serial banner",
            "serial",
        )
    elif "Disk (empty" not in text and "Disk (" not in text:
        finding(
            "P2",
            "PeakDisk",
            "boot",
            "Disk status line present",
            "no Disk line matched",
            "serial",
        )

    step("00-cli", "shell")
    sendkeys("g", "u", "i", "ret")
    time.sleep(2.8)
    step("01-login-or-desktop")
    sendkeys("ret")
    time.sleep(1.2)
    mouse_reset_estimate(100, 100)
    step("02-desktop-empty")


def session_chrome() -> None:
    print("=== A session/chrome ===", flush=True)
    # Brand / clock / wallpaper
    d = step("a-desktop-brand-clock")
    # Start via Peak click
    px, py = peak_btn_xy()
    mouse_click(px, py)
    time.sleep(0.8)
    h0 = dump_hash("a-desktop-brand-clock")
    d1 = step("a-start-menu")
    h1 = dump_hash("a-start-menu")
    if h0 == h1:
        finding(
            "P1",
            "Start menu",
            f"click Peak button ~({px},{py})",
            "Start menu opens",
            "screendump unchanged after Peak click (mouse miss or no menu)",
            str(d1),
        )
    # Type filter
    type_text("note")
    time.sleep(0.5)
    step("a-start-filter-note")
    sendkeys("ret")
    time.sleep(1.0)
    step("a-start-launch-notepad")
    sendkeys("esc")
    time.sleep(0.3)

    # Help overlay — try '?' 
    sendkeys("shift-slash")  # ?
    time.sleep(0.8)
    step("a-help-maybe")
    # Ctrl+Shift+H notify history
    sendkeys("ctrl-shift-h")
    time.sleep(0.7)
    step("a-notify-hist")
    sendkeys("esc")
    time.sleep(0.3)
    sendkeys("ctrl-shift-c")
    time.sleep(0.7)
    step("a-clipboard-picker")
    sendkeys("esc")

    # Open terminal + snap
    sendkeys("1")
    time.sleep(1.0)
    step("a-term-open")
    sendkeys("ctrl-alt-left")
    time.sleep(0.7)
    step("a-term-snap-left")
    sendkeys("ctrl-alt-right")
    time.sleep(0.7)
    step("a-term-snap-right")
    sendkeys("ctrl-alt-up")
    time.sleep(0.7)
    step("a-term-snap-max")
    # Nudge
    sendkeys("ctrl-alt-shift-left")
    time.sleep(0.4)
    step("a-term-nudge")

    # Min/max/close via keys where possible — Ctrl+W close
    sendkeys("2")
    time.sleep(0.9)
    step("a-files-open")
    sendkeys("alt-tab")
    time.sleep(0.6)
    step("a-alttab")
    sendkeys("esc")
    time.sleep(0.3)
    sendkeys("ctrl-w")
    time.sleep(0.6)
    step("a-ctrl-w")

    # Lock / power via Start if menu works
    mouse_click(*peak_btn_xy())
    time.sleep(0.6)
    type_text("lock")
    time.sleep(0.4)
    step("a-start-filter-lock")
    sendkeys("ret")
    time.sleep(0.9)
    step("a-lock-screen")
    sendkeys("ret")
    time.sleep(0.8)
    step("a-unlock")

    mouse_click(*peak_btn_xy())
    time.sleep(0.5)
    type_text("power")
    time.sleep(0.4)
    step("a-start-filter-power")
    sendkeys("ret")
    time.sleep(0.7)
    step("a-power-confirm")
    sendkeys("n")
    time.sleep(0.5)
    step("a-power-cancel")

    # Toasts: open several apps quickly
    for k in ("1", "2", "3", "4"):
        sendkeys(k)
        time.sleep(0.35)
    time.sleep(0.8)
    step("a-toast-stack")
    # Wait for expiry
    time.sleep(4.0)
    step("a-toast-after-wait")


def apps_core() -> None:
    print("=== B apps ===", flush=True)
    # Close excess: Esc then Ctrl+Alt+Esc is too harsh; use Ctrl+W repeatedly
    for _ in range(8):
        sendkeys("ctrl-w")
        time.sleep(0.25)
    time.sleep(0.4)
    step("b-desktop-cleared")

    # Terminal multi
    sendkeys("1")
    time.sleep(0.8)
    type_text("echo termA\n")
    time.sleep(0.4)
    step("b-term1")
    # Second terminal via hotkey again (may raise same)
    sendkeys("1")
    time.sleep(0.5)
    step("b-term-again")
    # Find
    sendkeys("ctrl-f")
    time.sleep(0.4)
    type_text("term")
    time.sleep(0.3)
    step("b-term-find")
    sendkeys("esc")
    # New tab Ctrl+Shift+T
    sendkeys("ctrl-shift-t")
    time.sleep(0.5)
    step("b-term-newtab")

    # Files
    sendkeys("2")
    time.sleep(1.0)
    step("b-files")
    sendkeys("n")
    time.sleep(0.3)
    type_text("audit-tmp\n")
    time.sleep(0.5)
    step("b-files-new")
    sendkeys("r")
    time.sleep(0.3)
    type_text("audit-renamed\n")
    time.sleep(0.5)
    step("b-files-rename")
    sendkeys("d")
    time.sleep(0.4)
    sendkeys("y")
    time.sleep(0.5)
    step("b-files-del")
    sendkeys("down")
    time.sleep(0.2)
    sendkeys("down")
    time.sleep(0.2)
    sendkeys("ret")  # open?
    time.sleep(0.6)
    step("b-files-enter")

    # Notepad via Start
    mouse_click(*peak_btn_xy())
    time.sleep(0.5)
    type_text("notepad")
    time.sleep(0.3)
    sendkeys("ret")
    time.sleep(1.0)
    step("b-notepad")
    type_text("hello audit world")
    time.sleep(0.3)
    step("b-notepad-typed")
    sendkeys("ctrl-s")
    time.sleep(0.6)
    step("b-notepad-save")
    sendkeys("ctrl-f")
    time.sleep(0.3)
    type_text("audit")
    time.sleep(0.3)
    step("b-notepad-find")
    sendkeys("esc")

    # Images via Start
    mouse_click(*peak_btn_xy())
    time.sleep(0.5)
    type_text("images")
    time.sleep(0.3)
    sendkeys("ret")
    time.sleep(1.0)
    step("b-images")
    sendkeys("f")  # fit?
    time.sleep(0.4)
    step("b-images-fit")
    sendkeys("n")
    time.sleep(0.4)
    step("b-images-next")

    # Browser
    sendkeys("6")
    time.sleep(1.5)
    step("b-browser-demo")
    # Try click Count — approximate center of demo page
    mouse_click(960, 520)
    time.sleep(0.8)
    step("b-browser-count-click")
    # Console?
    sendkeys("ctrl-shift-j")
    time.sleep(0.5)
    step("b-browser-console-maybe")
    sendkeys("esc")

    # Settings
    sendkeys("3")
    time.sleep(1.0)
    step("b-settings")
    sendkeys("down")
    time.sleep(0.2)
    sendkeys("down")
    time.sleep(0.2)
    step("b-settings-nav")
    # Scale key when settings focused may be swallowed — try click Look
    mouse_click(400, 300)
    time.sleep(0.5)
    step("b-settings-click")

    # Monitor
    sendkeys("7")
    time.sleep(1.2)
    step("b-monitor")
    sendkeys("right")
    time.sleep(0.4)
    step("b-monitor-page")
    sendkeys("e")  # export?
    time.sleep(0.6)
    step("b-monitor-export-maybe")

    # Agent
    sendkeys("4")
    time.sleep(1.0)
    step("b-agent")
    sendkeys("/")
    time.sleep(0.3)
    type_text("ask")
    time.sleep(0.3)
    step("b-agent-filter")
    sendkeys("esc")

    # Game
    sendkeys("5")
    time.sleep(1.0)
    step("b-game")
    sendkeys("spc")
    time.sleep(0.4)
    step("b-game-input")

    # Disks / Net via Start
    for app in ("disks", "net explorer", "net control"):
        mouse_click(*peak_btn_xy())
        time.sleep(0.45)
        type_text(app)
        time.sleep(0.25)
        sendkeys("ret")
        time.sleep(1.0)
        step(f"b-app-{app.replace(' ', '-')}")


def stress_and_scales() -> None:
    print("=== C stress/scales ===", flush=True)
    # Drag terminal title
    sendkeys("1")
    time.sleep(0.9)
    step("c-before-drag")
    # title bar approx center of a typical window
    mouse_drag(500, 60, 800, 200, step=6)
    time.sleep(0.5)
    step("c-after-drag")
    h0 = dump_hash("c-before-drag")
    h1 = dump_hash("c-after-drag")
    if h0 == h1:
        finding(
            "P2",
            "window drag",
            "mouse_drag title bar",
            "window moves / dump changes",
            "no visible change (mouse drag ineffective)",
            "c-after-drag",
        )

    # Multi-window load
    for k in ("1", "2", "6", "7", "3"):
        sendkeys(k)
        time.sleep(0.4)
    time.sleep(0.8)
    step("c-multi-window")

    # Cursor trail check: move across
    for x in range(100, 1800, 80):
        mouse_goto(x, 400, step=4)
    step("c-cursor-path")

    # Scale cycle when possible — minimize focus issues: Esc overlays then S
    sendkeys("esc")
    sendkeys("esc")
    # Close focused with ctrl-w until desktop-ish, then S
    for _ in range(6):
        sendkeys("ctrl-w")
        time.sleep(0.2)
    time.sleep(0.4)
    step("c-scale3-baseline")
    sendkeys("s")
    time.sleep(0.8)
    step("c-scale-after-s-1")
    sendkeys("s")
    time.sleep(0.8)
    step("c-scale-after-s-2")
    sendkeys("s")
    time.sleep(0.8)
    step("c-scale-after-s-3")  # back toward 3/4

    # Theme T
    sendkeys("t")
    time.sleep(0.7)
    step("c-theme-next")

    # Opaque snap stress
    sendkeys("1")
    time.sleep(0.7)
    sendkeys("ctrl-alt-left")
    time.sleep(0.4)
    sendkeys("ctrl-alt-right")
    time.sleep(0.4)
    sendkeys("ctrl-alt-up")
    time.sleep(0.4)
    step("c-snap-stress")


def cross_cutting() -> None:
    print("=== D cross-cutting ===", flush=True)
    # Hotkey regression with focused terminal
    for _ in range(5):
        sendkeys("ctrl-w")
        time.sleep(0.15)
    sendkeys("1")
    time.sleep(0.8)
    step("d-term-focus")
    sendkeys("2")
    time.sleep(0.9)
    step("d-hotkey2-files")
    # Title padding
    step("d-title-pad-check")
    # Leave GUI
    sendkeys("ctrl-alt-esc")
    time.sleep(2.0)
    step("d-back-cli")


def main() -> int:
    os.environ.setdefault("PEAK_AUDIT_DIR", "/tmp/peak-audit-v2")
    proc, ser, buf = boot_qemu()

    def cleanup():
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=3)
        except Exception:
            proc.kill()

    try:
        enter_gui(buf)
        session_chrome()
        apps_core()
        stress_and_scales()
        cross_cutting()
        (AUDIT / "NOTES.txt").write_text(
            f"Dense audit finished {time.ctime()}\nsteps≈{STEP}\ndir={AUDIT}\n",
            encoding="utf-8",
        )
        print(f"DONE steps={STEP} dir={AUDIT}", flush=True)
    finally:
        cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
