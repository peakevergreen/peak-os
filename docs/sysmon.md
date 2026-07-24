# Peak System Monitor

Live performance view for Peak OS — **CLI** and **GUI**, shared sampler.

## CLI

```text
top              # live refresh (~0.5s). q quit, r reset history
top -n           # one-shot snapshot
sysmon           # alias for top
ps               # kernel tasks / cooperative threads
```

Shows load/idle, memory & heap (with peaks), network rates, scheduler stats
(tasks, context switches, IRQs), a process table, and ASCII sparklines.

## GUI

Desktop → Peak menu → **Monitor**, or press **`7`**.

Pages: **1 Overview** · **2 Tasks** · **3 Network**. **R** resets history.
`[` / `]` also switch pages.

## Metrics

| Signal | Source |
|--------|--------|
| Load / idle % | Idle accounting around `hlt` |
| Memory / peak | PMM used/total pages |
| Heap / peak | kmalloc free-list (IRQ-safe lock) |
| Network | e1000 RX/TX bytes, packets, B/s |
| Tasks / ctx | Cooperative scheduler |
| IRQs | PIT tick count |
| GUI FPS | Frames presented per second |
| Compose µs | Last compose duration (approx.) |
| Present µs | Last present / blit duration (approx.) |
| Surf pressure | Window-surface memory budget (0–100%) |
| PeakVec µs | Last PeakVec query duration (approx.) |
| Agent audit µs | Last agent audit append duration (approx.) |

Sampling runs in the background (`sysmon_poll`). Desktop path notes:
`sysmon_note_compose_us` / `sysmon_note_present_us` / `sysmon_note_surf_pressure`
from the compositor. Agent / PeakVec notes:
`sysmon_note_peakvec_us` / `sysmon_note_agent_audit_us` (via `sysmon_now_us`).
Stress checklist: [scripts/gui-stress-checklist.md](../scripts/gui-stress-checklist.md).

## Concurrency notes

Heap and sysmon use IRQ-safe spinlocks. Kernel threads are cooperative
(`sched_spawn_kthread` / `sched_yield`) with stack switching; the timer sets
a reschedule hint checked from the shell and desktop loops.
