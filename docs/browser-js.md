# Peak Browser JavaScript

Peak-authored, interpreter-only JavaScript for in-guest Browser tabs. No QuickJS, no JIT.

## Architecture

| Piece | Location |
|-------|----------|
| Bytecode VM + GC | `kernel/js/` |
| DOM / HTML parser | `kernel/gui/dom_core.c`, `dom_parse.c` |
| CSS tokenize + layout | `kernel/gui/css_parse.c`, `css_layout.c` |
| DOM ↔ JS bridge | `kernel/gui/browser_js.c` |
| Web API stubs (`fetch`, storage; no AbortController) | `kernel/gui/webapi_stubs.c` |
| Tab storage + classic `<script src>` | `kernel/gui/webapi.c` |
| CLI | `/bin/js` (`js -e`, script file) |

Per-tab budgets: instruction count, object heap, timers. Scripts never run from IRQ, network locks, or timer ISR — only from `browser_tick()` / explicit eval on the desktop loop.

## Local demo

Open Browser → `peak://demo` (seeded on reset). Click **Count** to exercise DOM mutation, click listeners, and `setTimeout`.

## Reader fallback

If DOM/JS setup fails or scripts exceed budgets, the tab falls back to the existing reader-mode block extractor.

## Compatibility goal

Progressively pass representative site fixtures. Full Chromium-level standards coverage is **not** a completion criterion.

## Isolation (current → next)

- **Now:** VM instruction/object/timer budgets; `browser_tick()` only from the desktop loop (never IRQ/network locks); destroy runtime on navigate/tab close.
- **DOM handles:** each DOM object carries a generation (`handle_gen`); `browser_js_invalidate_handles` on navigate makes stale node refs fail closed (no use-after-navigate).
- **Web API stubs** (`webapi_stubs.c`): `fetch` supports GET and POST (string body, bounded) with same-origin/CORS gating. `AbortController()` factory exposes `signal.aborted` + `abort()`; pre-aborted signals fail `fetch` closed. `localStorage`/`sessionStorage` are in-memory per-tab maps with `getItem`/`setItem`/`removeItem` (not disk). Other init options and non-http(s) schemes still fail closed.
- **JS language:** `async`/`await` unwraps settled `Promise.resolve` values (and non-thenables as identity). `async function` / `async ()=>` return promises. `for await` remains fail-closed. ES modules: `js_eval_module` + `export var`/`export function` + `import {name} from "id"` (max 8 registered modules).
- **Monitor:** overview shows `js tabs / objs / timers / gc`.
- **Next:** full ring-3 process isolation once aarch64/x86 ELF userspace process support is sufficient; DOM/net syscalls behind validated handles.

## Public sites

Optional manual checks (not CI): Fark, peakevergreen.com, and other representative pages.
Reader mode remains the fallback when scripts fail or exceed budgets.

## Tests

- Host: `make test-host` (includes `test_js`, `test_webapi`)
- Fixtures: `tests/fixtures/js/`
- Interactive: `scripts/gui-stress-checklist.md` (Browser JS section)
- Optional live public-site checks are manual — not CI-blocking

## CLI

```
js -e '1+2*3'
js /home/dev/workspace/script.js
```
