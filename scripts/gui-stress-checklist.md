# GUI stress checklist (Commercial GFX)

Run interactively in QEMU after `make iso && ./scripts/run-qemu.sh`.
Product profile: **1080p @ UI scale 3**.

## Opaque drag / rubber resize

- [ ] Drag Terminal across wallpaper — no swim, no ghost chrome, no full-desktop flash
- [ ] Serial/display path presents damage rects during drag (not full-screen every tick)
- [ ] Edge resize shows rubber-band outline; content redraws on release
- [ ] Snap to left/right half-screen still works

## Soft updates

- [ ] Monitor/clock refresh without wallpaper tax when unobscured
- [ ] Obscured Monitor falls back safely (no trash)
- [ ] Toast expiry clears fully (no ghost toasts)
- [ ] Typing in Terminal does not hitch the cursor / input loop

## Cursor / present

- [ ] Move cursor during Monitor refresh and window drag — cursor must not vanish
- [ ] Cursor leaves no trails / holes on wallpaper
- [ ] Boot serial shows `display: vblank=0/1 pageflip=0/1`
- [ ] QEMU `vblank=1` is IS1 probe only — not tear-free proof (see docs/rpi.md)

## Raise / focus / session

- [ ] Click-to-raise / Alt-Tab do not force continuous full-desktop redraw
- [ ] Min / max / close repeatedly on Terminal and Files
- [ ] Session lock / power confirm: idle does not spin-present every tick; Enter/Y/N still work

## Pi HID (aarch64)

- [ ] Under GUI load, mouse/keyboard stay live (`platform_poll` in desktop loop)
- [ ] Damage-only presents do not stall on full-page flip sync

## Monitor timing

- [ ] Monitor shows compose_us / present_us and surf pressure
- [ ] fps counter updates while desktop is active

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

## Session

- [ ] Start → Lock; Enter unlocks
- [ ] Start → Reboot / Power off shows Y/N confirm
- [ ] Help text says `Ctrl+Alt+Esc` leaves desktop
- [ ] Login remains single-user splash (not a security boundary)

## Scale / resolution

- [ ] 1080p @ scale 3 readable; backbuffer + wallpaper + surfaces allocate
- [ ] If available, 1440p: no crash under memory budget soft-fail

## Agent approval

- [ ] `ask create foo.c` → Agent shows Approve write? Y/N
- [ ] `N` denies; `Y` writes under workspace

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
- [ ] Monitor shows JS tab/object/timer counters when Browser has run scripts
- [ ] External site that breaks JS budgets still shows reader fallback text
