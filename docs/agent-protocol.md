# Peak Agent Protocol (PAP) v0.1

Transport: guest **COM2** (`0x2F8`) ↔ host TCP/serial attached by QEMU.  
Encoding: newline-delimited JSON (one message per line). UTF-8. Max line 16 KiB.

## Message envelope

```json
{"v":1,"id":"<uuid-or-counter>","type":"<type>","payload":{}}
```

## Guest → host

| type | payload | meaning |
|------|---------|---------|
| `hello` | `{ "os":"Peak OS","ver":"0.2" }` | session start |
| `goal` | `{ "text":"...", "cwd":"/home/dev/workspace", "files":["..."] }` | developer ask |
| `tool_result` | `{ "call_id":"...", "ok":true, "output":"..." }` | tool finished |
| `audit` | `{ "action":"fs.write", "path":"...", "ok":true }` | optional mirror |

## Host → guest

| type | payload | meaning |
|------|---------|---------|
| `hello_ack` | `{ "proxy":"peak-host-proxy" }` | ready |
| `plan` | `{ "steps":["..."], "summary":"..." }` | high-level plan |
| `tool_call` | `{ "call_id":"...", "tool":"fs.write", "args":{...} }` | request tool |
| `done` | `{ "summary":"..." }` | goal complete |
| `error` | `{ "message":"..." }` | failure |
| `mock_plan` | same as `plan` + embedded `tool_call`s | offline/mock mode |

## Tools (guest-executed)

- `fs.read` — `{ "path": "..." }` → text
- `fs.write` — `{ "path":"...", "content":"..." }`
- `fs.list` — `{ "path":"..." }`
- `console.print` — `{ "text":"..." }`
- `proc.exec` — `{ "path":"...", "argv":[] }` (capability-gated)

## Policy

Host and guest both honor `/etc/peak/agent.policy` (guest copy). Deny paths outside allowlist (default: `/home/dev/workspace/**`, `/var/peak/sessions/**`).
