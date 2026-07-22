# Peak Agent Protocol (local)

The agent runs **entirely in-guest**. There is no host serial bridge.

## Entry points

- Shell: `ask <goal text>`
- GUI: Agent window chat + Y/N approval for `fs.write`
- Syscall: `SYS_agent`

## Tools (guest-executed)

Tool catalog and handlers live in `kernel/agent_tools.c`; policy in
`kernel/agent_policy.c`; planner in `kernel/agent_planner.c`.

| Tool | Status | Notes |
|------|--------|-------|
| `fs.read` | implemented | Allowlisted paths; content capped |
| `fs.write` | implemented | Allowlisted paths; may require GUI Y/N |
| `fs.list` | implemented | Directory listing |
| `console.print` | implemented | Prints to console / agent transcript |
| `proc.exec` | denied by default | Seeded in `deny_tools=` — not exposed |

## Planner

Bounded in-guest rule/intent planner (not an LLM):

- create / edit workspace files
- summarize workspace (`fs.list`)
- read a file
- recall prior goals (session memory + PeakVec)
- show audit tail
- help

Session memory is structured (`turn|goal=…|t=…|p=…`) under `/var/peak/sessions/memory.txt` and **read back** on each `ask`. Turns are also upserted into PeakVec for semantic recall.

## Policy

`/etc/peak/agent.policy` — `allow_paths=`, `allow_tools=`, `deny_tools=`, `require_approval=`.

Defaults allow `/home/dev/workspace` and `/var/peak/sessions`.

## Audit / memory / vectors

- `/var/peak/audit.log` — structured `actor|op|target|decision`
- `/var/peak/sessions/memory.txt` — append-only session turns
- `/var/peak/vec/` — PeakVec namespace files / blob pointers (see [peakvec.md](peakvec.md))

## Related syscalls

- `SYS_agent` — ask / list tools
- `SYS_peakvec` — upsert / query / count / delete (requires `CAP_VEC` or `CAP_AGENT`)
