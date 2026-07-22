#ifndef PEAK_AGENT_INTERNAL_H
#define PEAK_AGENT_INTERNAL_H

#include "types.h"
#include "vfs.h"

#define AGENT_AUDIT_PATH "/var/peak/audit.log"
#define AGENT_MEM_PATH "/var/peak/sessions/memory.txt"
#define AGENT_POLICY_PATH "/etc/peak/agent.policy"

#define AGENT_ALLOW_PATHS_MAX 8
#define AGENT_ALLOW_PATH_LEN 96
#define AGENT_PENDING_CONTENT_MAX 512
#define AGENT_TOOL_NAME_MAX 32
#define AGENT_TOOLS_MAX 8
#define AGENT_READ_CONTENT_MAX 1024
#define AGENT_MEMORY_TAIL_MAX 768

void agent_policy_load_defaults(void);
void agent_policy_reload(void);
int agent_policy_path_allowed(const char *path);
int agent_policy_normalize_path(const char *in, char *out, size_t out_len);
int agent_policy_tool_allowed(const char *tool);
int agent_policy_write_requires_approval(void);

/* Returns 0 ok, 1 pending approval, -1 denied/failed. */
int agent_queue_write_approval(const char *path, const char *content);

void agent_audit_append(const char *line);
void agent_audit_event(const char *op, const char *target, const char *decision);

const char *agent_tools_catalog(void);
size_t agent_tools_catalog_len(void);

int agent_tool_console_print(const char *msg);
int agent_tool_fs_read(const char *path, char *out, size_t out_len, size_t *out_n);
int agent_tool_fs_write(const char *path, const char *content, int auto_ok);
int agent_tool_fs_list(const char *path, char *out, size_t out_len);

void agent_plan_goal(const char *goal, char *summary, size_t summary_cap);

#endif
