# Peak OS Roadmap — AI-first developer OS (from-scratch)

Direction: stay on the from-scratch kernel. Agents, tools, and project context become **system primitives**.

Inference (early): **host-bridged** — guest speaks Peak Agent Protocol over a second serial port (COM2) to a Mac-side proxy that calls LLM APIs. In-guest HTTPS comes later.

## Phases

| Phase | Goal | Status |
|-------|------|--------|
| 0 | Freeze MVP (`v0.1.0-mvp`), architecture + roadmap docs | this tree |
| 1 | Heap, VMM, scheduler, VFS ramdisk, ELF + syscalls, userspace `sh` | in progress |
| 2 | `/home/dev/workspace`, `ls`/`cat`/`edit`, desktop → userspace shell | planned |
| 3 | `peak-agent`, tools, audit log, `ask` (mock planner) | planned |
| 4 | Host proxy + agent.policy over serial bridge | planned |
| 5 | GUI agent panel, project memory, CI/onboarding | planned |

## North star

```
userspace shell ──► peak-agent ──► tools (fs/exec)
                       │
                       ▼
                 host proxy (Mac) ──► LLM API / Ollama
```

Primitives: **workspace**, **agent** (capability bits), **action log**, **session memory** under `/var/peak/`.

## Non-goals (near term)

Linux ABI, full POSIX, package managers, in-kernel LLM weights, GPU drivers.

See also: [ARCHITECTURE.md](../ARCHITECTURE.md), [docs/agent-protocol.md](agent-protocol.md).
