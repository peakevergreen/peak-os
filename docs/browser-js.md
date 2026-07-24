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
- **Web API stubs** (`webapi_stubs.c`): `fetch` is GET-only http(s) with same-origin/CORS gating (not a full `Response`); non-GET / `signal` / `body` / other init options, empty or non-string URLs, and non-http(s) schemes fail closed. `localStorage`/`sessionStorage` are in-memory per-tab maps (not disk); empty keys, oversized entries, and quota exhaustion fail closed (no silent truncate). `AbortController` is not installed (unsupported — no fake shell). DOM bridge stays in `browser_js.c`.
- **Monitor:** overview shows `js tabs / objs / timers / gc`.
- **Next:** ring-3 process isolation with validated DOM/network syscalls once userspace process support is sufficient.

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
