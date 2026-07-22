#include "agent_internal.h"
#include "console.h"
#include "vfs.h"
#include "util.h"

static const char tools_catalog[] = "fs.read,fs.write,fs.list,console.print";

const char *agent_tools_catalog(void) {
    return tools_catalog;
}

size_t agent_tools_catalog_len(void) {
    return strlen(tools_catalog);
}

int agent_tool_console_print(const char *msg) {
    if (!agent_policy_tool_allowed("console.print")) {
        agent_audit_event("console.print", "-", "deny-tool");
        return -1;
    }
    console_write(msg ? msg : "");
    if (msg && msg[0] && msg[strlen(msg) - 1] != '\n')
        console_write("\n");
    agent_audit_event("console.print", "-", "ok");
    return 0;
}

int agent_tool_fs_read(const char *path, char *out, size_t out_len, size_t *out_n) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.read")) {
        agent_audit_event("fs.read", path, "deny-tool");
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.read", path, "deny-path");
        return -1;
    }
    size_t n = 0;
    size_t cap = out_len ? out_len - 1 : 0;
    if (cap > AGENT_READ_CONTENT_MAX)
        cap = AGENT_READ_CONTENT_MAX;
    if (vfs_read_file(norm, out, cap, &n) != 0) {
        agent_audit_event("fs.read", norm, "fail");
        return -1;
    }
    if (out_len)
        out[n < out_len ? n : out_len - 1] = '\0';
    if (out_n)
        *out_n = n;
    agent_audit_event("fs.read", norm, "ok");
    return 0;
}

int agent_tool_fs_write(const char *path, const char *content, int auto_ok) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.write")) {
        agent_audit_event("fs.write", path, "deny-tool");
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.write", path, "deny-path");
        return -1;
    }
    if (!strcmp(norm, AGENT_AUDIT_PATH)) {
        agent_audit_event("fs.write", norm, "deny-audit");
        return -1;
    }
    if (agent_policy_write_requires_approval() && !auto_ok) {
        if (agent_queue_write_approval(norm, content) != 0)
            return -1;
        agent_audit_event("fs.write", norm, "pending");
        return 1;
    }
    if (vfs_write_file(norm, content, strlen(content)) != 0)
        return -1;
    agent_audit_event("fs.write", norm, "ok");
    return 0;
}

int agent_tool_fs_list(const char *path, char *out, size_t out_len) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.list")) {
        agent_audit_event("fs.list", path, "deny-tool");
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0)
        return -1;
    if (!agent_policy_path_allowed(norm) && strcmp(norm, "/home/dev") != 0) {
        agent_audit_event("fs.list", norm, "deny-path");
        return -1;
    }
    int r = vfs_list(norm, out, out_len);
    agent_audit_event("fs.list", norm, r == 0 ? "ok" : "fail");
    return r;
}
