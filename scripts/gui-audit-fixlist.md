# GUI audit fix list (deep harvest v2)

**Date:** 2026-07-26  
**Profile:** BIOS, user-net, `-machine pc`, 1920×1080, UI scale 3  
**Harness:** [`scripts/peak_qemu_audit_lib.py`](peak_qemu_audit_lib.py), [`scripts/peak-qemu-audit-run.py`](peak-qemu-audit-run.py)  
**Dumps:** `/tmp/peak-audit-v2/` (60 PNG steps)  
**Gate:** ≥20 Open proven defects before fix PRs — **25 Open** below.

Historical (prior campaign): GA-001…004 Fixed (#288–#291).

| ID | Sev | Area | Repro | Expected | Actual | Dump | Status |
|----|-----|------|-------|----------|--------|------|--------|
| GA-101 | P1 | snprintf / Disks | Open Disks | Capacity/tree/RAM as numbers | Literal `%llu` in UI | `059-b-app-net-control.png` (Disks foreground) | **Fixed** |
| GA-102 | P1 | snprintf / Notepad | Open Notepad | Line numbers in gutter | Literal `%*d` / garbage | `007-a-start-launch-notepad.png`, `037-b-notepad.png` | **Fixed** |
| GA-103 | P1 | snprintf / agent | `agent` tools status strings | Numeric ctx/irq counters | `%llu` unsupported (same snprintf) | code+Disks proof | **Fixed** |
| GA-104 | P2 | Files toolbar | Open Files @ scale 3 | Full `Shift+range` hint | Clipped to `Shift+ra` | `016-a-files-open.png` | **Fixed** |
| GA-105 | P2 | Settings copy | Settings → Display | Full “live preview (click chip to apply)” | Leading clip `ick chip…` | `047-b-settings.png` | **Fixed** |
| GA-106 | P2 | Monitor export | Open Monitor | “Export snapshot” fully visible | Clipped `Export snapsh` | `050-b-monitor.png` | **Fixed** |
| GA-107 | P2 | Monitor footer | Open Monitor | Clear page/legend hints | Garbled `ï/2/3`, `R^reset^` | `050-b-monitor.png` | **Fixed** |
| GA-108 | P2 | Brand chip | Empty desktop | “PeakOS” fully inside scrim | Ascenders clipped at scrim top | `003-02-desktop-empty.png` | **Fixed** |
| GA-109 | P2 | Toast vs chrome | Launch app (toast) | Toast distinct from window chrome | Toast reads as second title+`x` | `007`, `016`, `041`, `044`, `050`, `055` | **Fixed** |
| GA-110 | P2 | Snap compose | Terminal → Ctrl+Alt+Left | Single clean snapped frame | Dual Terminal chrome / two chrome button sets | `012-a-term-snap-left.png` | **Deferred** |
| GA-111 | P2 | Files border | Open Files | Clean corner join | Stray/double border bottom-right | `016-a-files-open.png` | **Deferred** |
| GA-112 | P2 | Browser tabs | Open Browser | Tab label clear of close | `Peak Evergreenx` runs into close | `044-b-browser-demo.png` | **Fixed** |
| GA-113 | P2 | Settings chip | Settings Display | Clean scale chips | Stray `,` before highlighted `3x` | `047-b-settings.png` | **Fixed** |
| GA-114 | P2 | Toast stack | Open 4 apps quickly | Stacked toasts + clean desktop | Detached Settings/Agent chrome + vertical border ghosts | `025-a-toast-stack.png` | **Fixed** |
| GA-115 | P2 | Export path toast | Monitor export / sysmon | Toast above taskbar, readable | Path bleeds over taskbar | `053-b-agent.png`, `055-b-game.png` | **Fixed** |
| GA-116 | P2 | Browser demo | peak://demo Count | Clear button + working click | Gap/missing button chrome; count stayed 0 after click | `044`, `045-b-browser-count-click.png` | **Deferred** |
| GA-117 | P2 | Images chrome | Open Images | Single title bar | Ghost Images+`x` in body | `041-b-images.png` | **Fixed** |
| GA-118 | P2 | Notepad chrome | Open Notepad | Single title bar | Ghost Notepad+`x` top-right | `007-a-start-launch-notepad.png` | **Fixed** |
| GA-119 | P2 | Files chrome | Open Files | Single title bar | Ghost Files+`x` top-right | `016-a-files-open.png` | **Fixed** |
| GA-120 | P2 | Browser chrome | Open Browser | Single title bar | Ghost Browser title fragment | `044-b-browser-demo.png` | **Fixed** |
| GA-121 | P2 | Monitor chrome | Open Monitor | Single title bar | Ghost Monitor chrome on right | `050-b-monitor.png` | **Fixed** |
| GA-122 | P2 | Game chrome | Open Peak Runner | Single title bar | Detached Peak Runner title top-right | `055-b-game.png` | **Fixed** |
| GA-123 | P2 | Window nudge | Maximized Terminal + Ctrl+Alt+Shift+Left | Visible nudge or no-op honesty | Bit-identical dumps (no feedback) | `014`==`015` | **Deferred** |
| GA-124 | P2 | Images nav | Images `f`/`n` | Visible fit/next change | Bit-identical `042`==`043` | `042-b-images-fit.png` | **Deferred** |
| GA-125 | P2 | Compose ghosts | Multi-window + toasts | Damage clears old chrome | Stray vertical green border strips | `025-a-toast-stack.png` | **Deferred** |

## Deferred / needs-human

- Opaque drag swim/cursor trails: mouse crawl unreliable under `-display none` (accel); needs cocoa or absolute tablet driver.
- Start-menu launch of Disks/Net sometimes landed on wrong app in first pass — mouse estimate drift; retest after GA-101.
- Brand `"Pe"` under maximized windows: expected occlusion (not a bug).

## Close-out checklist

- [x] All Open → Fixed or Deferred after per-ID PRs
- [x] Retest dumps for P1s GA-101/102
