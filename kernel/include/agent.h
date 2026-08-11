#ifndef PEAK_AGENT_H
#define PEAK_AGENT_H

#include "types.h"

void agent_init(void);
/* Shell entry: ask "goal text" (in-guest intent planner + PeakVec recall) */
void agent_ask(const char *goal);
/* Syscall interface: cmd in rdi-style args via agent_syscall */
int64_t agent_syscall(uint64_t op, uint64_t a1, uint64_t a2, uint64_t a3);
void agent_gui_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
const char *agent_last_summary(void);
int agent_pending_approvals(void);
int agent_write_pending(void);
const char *agent_pending_write_path(void);
void agent_approve_write(int yes);
const char *agent_tools_catalog(void);
void agent_policy_reload_cli(void);
int agent_policy_tool_allowed_cli(const char *tool);
int agent_policy_deny_reason_cli(const char *tool, const char *path, char *out, size_t out_len);

/* Transcript scroll: positive = older lines, 0 = follow latest. */
void agent_transcript_clear(void);
void agent_transcript_push(const char *line);
int  agent_transcript_scroll(int delta);
void agent_transcript_reset_scroll(void);
int  agent_transcript_scroll_end(void);
void agent_transcript_note_audit(const char *op, const char *target, const char *decision);
void agent_transcript_note_recall(const char *msg);
void agent_transcript_note_tool(const char *msg);

/* Export transcript (+ last tool lines) to VFS. Returns bytes written or -1. */
int agent_export_transcript(const char *path);
const char *agent_last_tool_result(void);
void agent_approval_queue_draw(uint32_t x, uint32_t y, uint32_t w);

/* Transcript filter: only active after Ctrl+F / '/'; Esc clears and exits. */
int agent_transcript_filter_active(void);
void agent_transcript_filter_set_active(int on);
int agent_transcript_filter_key(int key);
const char *agent_transcript_filter_text(void);

#endif
