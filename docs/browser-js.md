# Peak Browser JavaScript

Peak-authored, interpreter-only JavaScript for in-guest Browser tabs. No QuickJS, no JIT.

## Architecture

| Piece | Location |
|-------|----------|
| Bytecode VM + GC | `kernel/js/` |
| DOM / HTML parser | `kernel/gui/dom_core.c`, `dom_parse.c` |
| CSS tokenize + layout | `kernel/gui/css_parse.c`, `css_layout.c` |
| DOM ↔ JS bridge | `kernel/gui/browser_js.c` |
| Web API stubs (`fetch`, storage, AbortController) | `kernel/gui/webapi_stubs.c` |
| Tab storage + classic `<script src>` | `kernel/gui/webapi.c` |
| CLI | `/bin/js` (`js -e`, script file) |

Per-tab budgets: instruction count, object heap, timers. Scripts never run from IRQ, network locks, or timer ISR — only from `browser_tick()` / explicit eval on the desktop loop.

## Local demo

Open Browser → `peak://demo` (seeded on reset). Click **Count** to exercise DOM mutation, click listeners, and `setTimeout`. The **Console** panel at the bottom shows `console.log` output; press **c** to toggle it. `console.clear()` clears captured lines; optional substring filter via `browser_console_set_filter` (GUI applies when set).

## Navigation chrome

- **Back / Forward** (`<` `>` buttons, **b** / **f** keys): one-level history per tab; forward stack clears on a new navigation.
- **Tab strip:** each tab shows page title (or URL) with fit-to-width labels; **~** prefix while fetching. Click a tab to switch; click **x** on a tab (when more than one tab is open) or press **w** to close. **+** opens a new tab (`peak://demo`).
- **Reopen closed tab (lite):** **Shift+T** or context menu **Reopen closed tab** restores the last closed tab’s URL/title (one slot).
- **Bookmarks**: saved to `/var/peak/bookmarks` (`title|url` lines). Context menu **Add bookmark**; bookmark bar shows up to four entries; full list in the context menu (max six).
- **TLS / network errors**: error pages show `tls_last_error()` with `[tag]` from `tls_err_name()`, plus `net_last_error()` for DNS/HTTP failures.

## Reader fallback

If layout cannot produce enough boxes, the tab falls back to the reader-mode block extractor with an **honest reason** (empty body, DOM parse fail, sparse layout, etc.). Partial script failure alone does **not** force reader when CSS layout has content.

## Interactive bar (BR-1…14)

- **Fetch:** HTTP/2 stores up to 64 KiB with WINDOW_UPDATE / PING ACK; browser body budget 256 KiB; truncation noted in status.
- **CSS:** Inline `<style>` plus fetched `<link rel=stylesheet>` (bounded); specificity; width/max-width/height/text-align/border; inline flow + form/img boxes.
- **Images:** PNG/JPEG (and PPM/BMP) via `img_decode_*`; painted in layout when decoded.
- **Forms:** `input` / `textarea` / `button` focus + typing; GET/POST submit (`application/x-www-form-urlencoded`).
- **JS bridge:** `click` / `input` / `change` / `submit` / `keydown`; `querySelectorAll`; classList helpers; `location.assign` / `reload`; `Response.text()`.
- **Chrome:** Multi-entry back/forward (16); status shows H1/H2, JS ok/err, box count, bytes.

## Compatibility goal

Interactive bar: readable + lightly interactive real sites. Full Chromium-level standards coverage is **not** a completion criterion.

## Isolation (current → next)

- **Now:** VM instruction/object/timer budgets; `browser_tick()` only from the desktop loop (never IRQ/network locks); destroy runtime on navigate/tab close.
- **DOM handles:** each DOM object carries a generation (`handle_gen`); `browser_js_invalidate_handles` on navigate makes stale node refs fail closed (no use-after-navigate).
- **Web API stubs** (`webapi_stubs.c`): fail-closed matrix documents supported vs rejected surfaces. `fetch` supports GET/POST (string body, bounded) with optional `headers` plain object (string key/value), same-origin/CORS gating, `Response.json()` and `Response.text()` on `bodyText`. `AbortController()` factory exposes `signal.aborted` + `abort()`; pre-aborted signals fail `fetch` closed. `localStorage` persists to VFS under `/var/peak/localStorage/<origin>`; `sessionStorage` is in-memory per-tab. Unsupported init options and non-http(s) schemes fail closed. Isolation enforce mode blocks fetch with explicit ring-3 gated reason.
- **JS language:** `async`/`await` unwraps settled `Promise.resolve` values (and non-thenables as identity). `async function` / `async ()=>` return promises. `Promise.allSettled(arr)` / `Promise.race(arr)` lite over arrays (max 32 entries; settled promises unwrapped). `for await (x of arr)` lite over arrays/settled promises. ES modules: `js_eval_module` + `export var`/`export function` + `import {name} from "id"` (max 8 registered modules).
- **Monitor:** overview shows `js tabs / objs / timers / gc`.
- **Ring-3 scaffold (Pass 86):** `browser_isolation.c` tracks availability; optional enforce mode fail-closes DOM (`browser_js`) and `fetch` when ring-3 is unavailable.
- **Next:** full ring-3 process isolation once aarch64/x86 ELF userspace process support is sufficient; DOM/net syscalls behind validated handles.

## Public sites

Optional manual checks (not CI): Fark, peakevergreen.com, Google, Wikipedia Main Page.
See `scripts/browser-render-checklist.md`.

## Tests

- Host: `make test-host` (includes `test_js`, `test_webapi`, `test_img_decode`, `test_dom`)
- Fixtures: `tests/fixtures/js/`, `tests/fixtures/browser/`
- Interactive: `scripts/gui-stress-checklist.md` (Browser JS section)
- QEMU body matrix: `scripts/browser-render-matrix.py` (manual / optional)
- Optional live public-site checks are manual — not CI-blocking

## CLI

```
js -e '1+2*3'
js /home/dev/workspace/script.js
```
