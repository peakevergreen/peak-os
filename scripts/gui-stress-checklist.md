# GUI stress checklist (Commercial GFX)

Run interactively in QEMU after `make iso && ./scripts/run-qemu.sh`.
Product profile: **1080p @ UI scale 3**.

- [ ] Start menu open/close damages menu + Peak button rects (not full-desktop)
- [ ] Start menu: type to filter apps; Enter launches selection; Up/Down navigates
- [ ] Context menu open/close uses damage rects
- [ ] Files create/delete/rename/navigate use DIRTY_WIN (Monitor compose_us stays soft)
- [ ] Opaque move end damages window footprints (not unconditional DIRTY_FULL)

## Opaque drag / rubber resize

- [ ] Drag Terminal across wallpaper — no swim, no ghost chrome, no full-desktop flash
- [ ] Serial/display path presents damage rects during drag (not full-screen every tick)
- [ ] Edge resize shows rubber-band outline; content redraws on release
- [ ] Snap to left/right half-screen still works
- [ ] Drag title to top edge maximizes; accent snap preview shows near edges
- [ ] Title-drag then Ctrl+W / Esc aborts cleanly (no stuck pixmap / wrong-window snap)
- [ ] Maximized (or over-cap) title-drag uses outline, not dual full-size pixmap alloc

## Soft updates

- [ ] Monitor/clock refresh without wallpaper tax when unobscured
- [ ] Obscured Monitor falls back safely (no trash)
- [ ] Toast expiry clears fully (no ghost toasts)
- [ ] Typing in Terminal does not hitch the cursor / input loop
- [ ] Hold Shift+letter, release Shift then letter — char stops (no sticky repeat)

## Cursor / present

- [ ] Move cursor during Monitor refresh and window drag — cursor must not vanish
- [ ] Cursor leaves no trails / holes on wallpaper
- [ ] Boot serial shows `display: vblank=0/1 pageflip=0/1`
- [ ] QEMU `vblank=1` is IS1 probe only — not tear-free proof (see docs/rpi.md)

## Raise / focus / session

- [ ] Click-to-raise / Alt-Tab do not force continuous full-desktop redraw
- [ ] Min / max / close repeatedly on Terminal and Files
- [ ] Focused window shows accent titlebar ring; unfocused windows stay dim
- [ ] Session lock / power confirm: idle does not spin-present every tick; Enter/Y/N still work
- [ ] Idle with Browser open (pending setTimeout): CPU settles / hlt — no busy-spin present
- [ ] Idle with Browser never opened / minimized after timer dirty: no forever present storm
- [ ] Held mouse button alone does not prevent idle lock forever
- [ ] Dirty workspace → wait autosave: desktop stays interactive (disksave yields)

## Pi HID (aarch64)

- [ ] Under GUI load, mouse/keyboard stay live (`platform_poll` in desktop loop)
- [ ] Damage-only presents do not stall on full-page flip sync

## Monitor timing

- [ ] Monitor shows compose_us / present_us and surf pressure
- [ ] fps counter updates while desktop is active
- [ ] Busy drag / multi-toast: compose_us stays on damage path (no spike to full-desktop every tick when rect list overflows)

## Multi-Terminal

- [ ] Open 3 Terminal windows from Start menu / key `1`
- [ ] Type different text in each; confirm buffers stay independent
- [ ] Scroll one with wheel / Up-Down; others unchanged
- [ ] Close one; remaining still accept input

## Toasts

- [ ] Trigger several notifications (Agent, Save disk)
- [ ] Confirm toast expiry does **not** force continuous full-screen redraws
- [ ] Toast strip must not rescale/thrash the desktop wallpaper cache
- [ ] UI stays responsive while toasts visible

## Input

- [ ] Wheel on Files clamps selection to directory count
- [ ] Settings → Look: theme / wallpaper / brand clicks hit correct rows
- [ ] Settings → Display: scale click cycles 1–4

## Images / surfaces

- [ ] Images zoom/pan stays in viewport (no hang on large negative pan)
- [ ] Oversized image decode fails closed (toast / no OOM)
- [ ] Surface budget / OOM toast does not paint into a failed surface

## Session

- [ ] Start → Lock; Enter unlocks
- [ ] Start → Reboot / Power off shows Y/N confirm
- [ ] Help text says `Ctrl+Alt+Esc` leaves desktop
- [ ] Help overlay shows key/desc columns; click or Esc dismisses
- [ ] Login remains single-user splash (not a security boundary)

## Scale / resolution

- [ ] 1080p @ scale 3 readable; backbuffer + wallpaper + surfaces allocate
- [ ] If available, 1440p: no crash under memory budget soft-fail

## Agent approval

- [ ] `ask create foo.c` → Agent shows Approve write? Y/N (Enter submits; body click does not)
- [ ] `N` denies; `Y` writes under workspace
- [ ] Second create while pending → "write already pending" (not silent fail)
- [ ] Shell `ask create …` prints open-Agent Y/N hint when pending
- [ ] With write pending: Files status bar shows path + Y/N; target row marked `!`
- [ ] Notepad: pending bar offers A apply / Y approve / N deny; Apply loads patch into buffer without writing

## Network / TLS isolation

- [ ] `gui` → sign in → idle: serial shows no DNS/TCP/HTTP/TLS attempts and no `TLS certificate unverified`
- [ ] Browser default tab is local Peak JS demo (`peak://demo`, no network)
- [ ] Explicit Browser Go without trust pin fails inside Browser only (no desktop startup failure)

## Browser JavaScript

- [ ] Open Browser; first tab shows Peak JS Demo without fetching
- [ ] Click **Count**; on-page counter increments (DOM + events)
- [ ] Status/timer updates after ~400ms (`setTimeout`)
- [ ] Navigate away and back to `peak://demo`; runtime recreates cleanly
- [ ] Close tab with JS active; no hang / Exception 13
- [ ] Two JS tabs: close the first; interact with the second (DOM + click) — no Exception 13
- [ ] `setTimeout(() => location.assign('peak://demo'), 0)` — no Exception 13 mid-timer
- [ ] Monitor shows JS tab/object/timer counters when Browser has run scripts
- [ ] External site that breaks JS budgets still shows reader fallback text

## Stability campaign regressions (re-verify)

Focused pass after kernel/GUI stability merges. Details also appear in sections above.

- [ ] Idle must `hlt`: Browser open with pending `setTimeout` — CPU settles, no busy-spin present
- [ ] JS tab close: close active JS tab; with two tabs, close the first then use the second — no hang / Exception 13
- [ ] Opaque drag abort: title-drag then Ctrl+W / Esc / window close — no stuck pixmap or wrong-window snap
- [ ] Sticky key: Hold Shift+letter, release Shift then letter — software repeat clears
- [ ] Agent approval busy: second `ask create …` while pending → "write already pending" (see also [docs/agent-protocol.md](../docs/agent-protocol.md))
- [ ] disksave yield: dirty workspace → wait autosave — desktop stays interactive

## Stability campaign (regression)

True idle must reach `hlt` (no forever `dirty_bits` / deferred-present busy-spin).

- [ ] Idle desktop with Browser + pending `setTimeout`: fan settles; serial not flooded with presents
- [ ] Sticky key: Shift+letter release order — no autorepeat flood; another key clears
- [ ] Agent: second write while pending shows busy / already-pending (not opaque deny)
- [ ] JS: close non-last tab; `location.assign` from timer — no Exception 13
- [ ] Opaque title-drag then close/Esc: gesture aborts; no stuck underlay
- [ ] Autosave / disksave: GUI stays interactive; VFS write during save returns busy
- [ ] PeakFS load of corrupt/truncated image: fail closed (no half-cleared workspace)
- [ ] Context menu: open menu → Ctrl+W target window → click menu item is a no-op
