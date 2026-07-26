# GUI audit fix list (QEMU visual)

**Date:** 2026-07-26  
**Profile:** BIOS, user-net, 1920×1080 @ UI scale 3, `-display none` + monitor `screendump` / `sendkey`  
**Harness:** [`scripts/peak-qemu-audit-run.py`](peak-qemu-audit-run.py) (+ [`peak-qemu-audit-drive.py`](peak-qemu-audit-drive.py))  
**Note:** Wait for full `peak:/home/dev/workspace>` before typing; `peak:/` alone matches too early. QEMU `screendump` writes PPM even with a `.png` name — convert before inspecting.

| ID | Severity | Area | Repro | Expected | Actual | Dump | Status |
|----|----------|------|-------|----------|--------|------|--------|
| GA-001 | P1 | Desktop hotkeys | Enter `gui`, dismiss login, press `1` then `2` | `1`–`7` always launch apps (Help overlay) | Digits typed into focused Terminal | `/tmp/peak-audit3/app-files.png` (typed `2`) | **Fixed** (#288) |
| GA-002 | P1 | PeakDisk / ATA | Boot with PeakDisk on `q35` + `if=ide` | `Disk (empty)` or restore | Serial: `Disk (none)` | serial banner | **Fixed** (#289) — `run-qemu.sh` / smoke use `-machine pc` |
| GA-003 | P2 | Terminal chrome | Open Terminal at scale 3 | Title has padding inside border | Focus ring overpainted title glyphs | `/tmp/peak-audit-ga003/term-title.png` | **Fixed** (#290) |
| GA-004 | P2 | Audit harness | Send `gui` after seeing `peak:/` | Shell accepts `gui` | Prompt matched mid-banner; `gui` ignored until sendkey | early `/tmp/peak-audit/` | **Fixed** (this PR) |

## Deferred / not bugs

- Brand `"Pe"` under windows: PeakOS corner label occluded by large windows — expected stacking.
- Opaque-drag / cursor-trail stress: needs live cocoa mouse — deferred.
- Bridged LAN / Pi HID: out of scope this pass.

## Close-out

- [x] GA-001 / GA-002 / GA-003 retested with screendumps or serial after merge
- [x] Harness waits for `workspace>` and uses `-machine pc`
- [x] Fixlist rows Fixed or Deferred
