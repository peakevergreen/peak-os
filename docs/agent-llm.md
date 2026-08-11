# Opt-in remote LLM (design)

Status: **design only** — not implemented. Local peak-agent remains the default.
Remote inference must never be enabled without an explicit privacy grant.

## Goals

- Understand natural-language goals beyond the local verb+slot table.
- Propose real multi-step tool plans and non-template edits.
- Keep **guest-executed tools** as the only hands that touch VFS/net/exec.
- Preserve policy, Y/N write approval, audit, and PeakVec memory.

## Non-goals

- Shipping model weights in the freestanding kernel.
- Host serial/SSH agent bridges.
- Silent full-disk or full-memory exfiltration to a provider.
- Multi-provider plugin marketplace (v1 = one OpenAI-compatible HTTPS endpoint).

## Architecture

```
ask(goal)
  → LocalPlanner (parse_slots)
      → high confidence → ToolLoop (today)
      → low confidence AND privacy_grant_llm → RemoteLLM over TLS
            → ToolPlan JSON
            → ToolLoop (same agent_tools + policy)
            → audit | memory | PeakVec
```

Hybrid router rules (proposed):

1. Always run `parse_slots` first.
2. Call remote LLM only when intent is `UNKNOWN` **or** create/edit has no matching template/transform **and** `privacy_grant_llm` is set.
3. If grant is off, behavior is unchanged from today’s local planner.

## Tool-plan schema

Model output must be **structured tool plans**, never free-form shell.

```json
{
  "version": 1,
  "goal": "refactor fib.c to use recursion",
  "steps": [
    { "tool": "fs.search", "args": { "needle": "fib" } },
    { "tool": "fs.read", "args": { "path": "/home/dev/workspace/fib.c" } },
    { "tool": "fs.write", "args": { "path": "/home/dev/workspace/fib.c", "content": "…" } }
  ],
  "notes": "optional short rationale"
}
```

Constraints:

- `tool` must be in the allowlisted catalog (`agent_tools_catalog`).
- Paths must pass `agent_policy_path_allowed`.
- `fs.write` still requires GUI Y/N when `require_approval` is set.
- Reject unknown tools, shell metacharacters, and plans exceeding step/body caps.
- Audit actor for remote steps: `llm|<endpoint-host>|…` (never claim `agent` alone).

## Privacy grant

Proposed grant (mirrors net client):

- Setting / CLI: `privacy grant llm` / `privacy revoke llm`
- Gate: `privacy_llm_allowed()` checked before any TLS call
- Endpoint config: `/etc/peak/llm.endpoint` (URL + optional API key path under `/var/peak/secrets/`, never logged)
- Default: **denied**

Context pack sent to the model (hard caps):

| Field | Cap |
|-------|-----|
| Last N memory turns | 8 turns / 2 KiB |
| PeakVec hits (`agent` + `ws`) | top 5 / 1 KiB meta |
| Allowlisted file snippets | 2 files × 1 KiB |
| Tool catalog | names only |

No recursive directory dumps. No `/etc`, `/var/peak/audit.log`, or secret paths in the pack.

## TLS transport

Reuse in-guest HTTP(S) client (`net_http_*` + WebPKI). Fail closed on cert errors (same as browser/net.fetch).

Request shape (OpenAI-compatible chat completions):

- `POST {endpoint}/v1/chat/completions`
- System prompt: Peak tool-plan schema + deny rules
- User message: goal + context pack
- Response parsed strictly as ToolPlan JSON (strip markdown fences if present; reject on parse failure)

## Failure modes

| Case | Behavior |
|------|----------|
| No grant | Local planner only |
| TLS / HTTP failure | Print reason; fall back to local try: hint |
| Invalid JSON plan | Reject; do not execute partial steps |
| Policy deny mid-plan | Stop; transcript shows deny reason |
| User denies write | Plan ends; audit `denied` |

## Implementation sketch (future PRs)

1. `privacy_grant_llm` + endpoint config + docs/privacy.md update
2. `agent_tool_plan_validate` + executor over existing tools
3. `agent_llm_complete(goal, context, plan_out)` TLS client
4. Wire hybrid router in `agent_plan_goal`
5. Smoke: grant off = unchanged; grant on + mock endpoint = plan → tools

## Security notes

See [security-model.md](security-model.md) (compromised agent prompt / tool abuse). Remote LLM increases prompt-injection surface; mitigations are schema validation, path/tool policy, write approval, and audit.
