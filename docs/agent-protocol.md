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
| `fs.stat` | implemented | Path metadata (type, size, refs) |
| `fs.mkdir` | implemented | Create directory under allow_paths |
| `fs.rm` | implemented | Remove file or tree (audit/memory protected) |
| `fs.search` | implemented | Capped substring scan of workspace files (paths) |
| `fs.grep` | implemented | Content grep: `path:line:text` (policy-gated) |
| `fs.diff` | implemented | Line diff between two allowlisted paths (bounded hunks) |
| `fs.exec` | implemented | Allowlisted `/bin` builtins only (no shell metachar) |
| `sys.info` | implemented | Uptime, memory/heap, load/idle, net rates, GUI timing |
| `net.ping` | implemented | DNS + TCP/:80 probe; requires `privacy_grant_net_client` |
| `net.fetch` | implemented | Bounded HTTP GET (2 KiB body cap); privacy-gated |
| `mem.recall` | implemented | PeakVec + formatted session memory tail (policy-gated) |
| `audit.tail` | implemented | Formatted tail of audit log (policy-gated) |
| `console.print` | implemented | Prints to console / agent transcript |

`proc.exec` is **not** a tool — guest execution goes through `fs.exec` with an explicit allowlist (`ls`, `cat`, `find`, `sha256sum`, `head`, `tail`, …).

## Planner

Bounded in-guest rule/intent planner (not an LLM):

- create / edit workspace files
- summarize workspace (`fs.list`)
- search workspace (`fs.search`) or grep content (`fs.grep`)
- diff two workspace files (`fs.diff`)
- fetch URL (`net.fetch`, privacy-gated, bounded body)
- ping host (`net.ping`, privacy-gated)
- read a file
- recall prior goals (`mem.recall`)
- show audit tail (`audit.tail`)
- system info (`sys.info`)
- help

Session memory is structured (`turn|goal=…|t=…|p=…`) under `/var/peak/sessions/memory.txt` and **read back** on each `ask`. Turns are also upserted into PeakVec for semantic recall.

## Policy

`/etc/peak/agent.policy` — `allow_paths=`, `allow_tools=`, `deny_tools=`, `require_approval=`.

Defaults allow `/home/dev/workspace` and `/var/peak/sessions`. Seeded policy includes `fs.exec`, `fs.grep`, `fs.diff`, `net.ping`, `net.fetch`, `mem.recall`, and `audit.tail`.

Shell `policy` prints the active policy with labeled sections (paths, tools, approval).

## Audit / memory / vectors

- `/var/peak/audit.log` — structured `actor|op|target|decision`
- `/var/peak/sessions/memory.txt` — append-only session turns
- `/var/peak/vec/` — PeakVec namespace files / blob pointers (see [peakvec.md](peakvec.md))

CLI `audit` and `memory`, plus agent tools `audit.tail` and `mem.recall`, share the same numbered tail format (`--- recent entries ---` / `--- end ---`).

## Related syscalls

- `SYS_agent` — ask / list tools
- `SYS_peakvec` — upsert / query / count / delete (requires `CAP_VEC` or `CAP_AGENT`)
