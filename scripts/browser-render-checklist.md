# Browser render maturity checklist (Interactive bar)

Prove on QEMU user-net (`-machine pc`, virtio-rng, `-rtc base=…`).

## CLI / fetch

| ID | Check | Pass criteria |
|----|-------|---------------|
| BR-H2-01 | `wget -O - https://example.com/` | Non-zero body, HTTP/2 or HTTP/1 |
| BR-H2-02 | `wget -O - https://peakevergreen.com/` | Non-zero HTML |
| BR-H2-03 | `wget -O - https://www.fark.com/` | Non-zero HTML |
| BR-H2-04 | `wget -O - https://www.google.com/` | Non-zero HTML (or redirect body) |
| BR-H2-05 | Truncation honesty | Large page shows trunc meta when over 64 KiB H2 store |

## Browser GUI / Interactive

| ID | Check | Pass criteria |
|----|-------|---------------|
| BR-UI-01 | `peak://demo` | Count button increments; form field typeable |
| BR-UI-02 | Navigate peakevergreen / Fark | Status shows boxes or honest reader reason; not blank |
| BR-UI-03 | Back/forward | ≥3 hops; `<`/`>` restore prior URLs |
| BR-UI-04 | Images | Page with `<img>` paints bitmap or `[img]` placeholder |
| BR-UI-05 | External CSS | Colors/margins from linked stylesheet when fetchable |

Harness: `scripts/browser-render-matrix.py` (CLI body sizes) + this checklist for GUI.
