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

#endif
