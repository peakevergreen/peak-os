#include "agent_internal.h"
#include "vfs.h"
#include "util.h"

static char allow_paths[AGENT_ALLOW_PATHS_MAX][AGENT_ALLOW_PATH_LEN];
static int allow_path_count;
static int require_write_approval = 1;
static char allow_tools[AGENT_TOOLS_MAX][AGENT_TOOL_NAME_MAX];
static int allow_tool_count;
static char deny_tools[AGENT_TOOLS_MAX][AGENT_TOOL_NAME_MAX];
static int deny_tool_count;

int agent_policy_write_requires_approval(void) {
    return require_write_approval;
}

static int tool_listed(const char names[][AGENT_TOOL_NAME_MAX], int count, const char *tool) {
    for (int i = 0; i < count; i++) {
        if (!strcmp(names[i], tool))
            return 1;
    }
    return 0;
}

int agent_policy_tool_allowed(const char *tool) {
    if (tool_listed(deny_tools, deny_tool_count, tool))
        return 0;
    if (allow_tool_count == 0)
        return 1;
    return tool_listed(allow_tools, allow_tool_count, tool);
}

static int path_under_prefix(const char *path, const char *prefix) {
    size_t pl = strlen(prefix);
    if (strncmp(path, prefix, pl) != 0)
        return 0;
    return path[pl] == '\0' || path[pl] == '/';
}

int agent_policy_normalize_path(const char *in, char *out, size_t out_len) {
    if (!in || !out || out_len < 2 || in[0] != '/')
        return -1;
    char norm[VFS_PATH_MAX];
    if (vfs_normalize(in, norm, sizeof(norm)) != 0)
        return -1;
    if (norm[0] != '/')
        return -1;
    for (size_t i = 0; norm[i]; i++) {
        if (norm[i] == '.' && norm[i + 1] == '.' &&
            (i == 0 || norm[i - 1] == '/') &&
            (norm[i + 2] == '/' || norm[i + 2] == '\0'))
            return -1;
    }
    size_t n = strlen(norm);
    if (n + 1 > out_len)
        return -1;
    memcpy(out, norm, n + 1);
    return 0;
}

void agent_policy_load_defaults(void) {
    allow_path_count = 0;
    memcpy(allow_paths[0], "/home/dev/workspace", 20);
    memcpy(allow_paths[1], "/var/peak/sessions", 19);
    allow_path_count = 2;
    require_write_approval = 1;
    allow_tool_count = 0;
    deny_tool_count = 0;
    memcpy(allow_tools[0], "fs.read", 8);
    memcpy(allow_tools[1], "fs.write", 9);
    memcpy(allow_tools[2], "fs.list", 8);
    memcpy(allow_tools[3], "console.print", 14);
    allow_tool_count = 4;
}

void agent_policy_reload(void) {
    agent_policy_load_defaults();
    char buf[512];
    size_t n = 0;
    if (vfs_read_file(AGENT_POLICY_PATH, buf, sizeof(buf) - 1, &n) != 0)
        return;
    buf[n] = '\0';
    const char *p = buf;
    while (*p) {
        if (!strncmp(p, "allow_paths=", 12)) {
            p += 12;
            allow_path_count = 0;
            while (*p && *p != '\n' && allow_path_count < AGENT_ALLOW_PATHS_MAX) {
                while (*p == ',')
                    p++;
                size_t i = 0;
                while (*p && *p != ',' && *p != '\n' && i + 1 < AGENT_ALLOW_PATH_LEN)
                    allow_paths[allow_path_count][i++] = *p++;
                allow_paths[allow_path_count][i] = '\0';
                if (i)
                    allow_path_count++;
            }
        } else if (!strncmp(p, "require_approval=", 17)) {
            p += 17;
            require_write_approval = (strncmp(p, "fs.write", 8) == 0) || (*p == '1');
        } else if (!strncmp(p, "allow_tools=", 12) || !strncmp(p, "deny_tools=", 11)) {
            int is_deny = p[0] == 'd';
            p += is_deny ? 11 : 12;
            if (is_deny)
                deny_tool_count = 0;
            else
                allow_tool_count = 0;
            while (*p && *p != '\n') {
                while (*p == ',')
                    p++;
                char name[AGENT_TOOL_NAME_MAX];
                size_t i = 0;
                while (*p && *p != ',' && *p != '\n' && i + 1 < sizeof(name))
                    name[i++] = *p++;
                name[i] = '\0';
                if (i) {
                    if (is_deny && deny_tool_count < AGENT_TOOLS_MAX) {
                        memcpy(deny_tools[deny_tool_count], name, i + 1);
                        deny_tool_count++;
                    } else if (!is_deny && allow_tool_count < AGENT_TOOLS_MAX) {
                        memcpy(allow_tools[allow_tool_count], name, i + 1);
                        allow_tool_count++;
                    }
                }
            }
        }
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    if (allow_path_count == 0)
        agent_policy_load_defaults();
}

int agent_policy_path_allowed(const char *path) {
    char norm[VFS_PATH_MAX];
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0)
        return 0;
    for (int i = 0; i < allow_path_count; i++) {
        if (path_under_prefix(norm, allow_paths[i]))
            return 1;
    }
    return 0;
}

void agent_audit_append(const char *line) {
    char existing[2048];
    size_t n = 0;
    vfs_read_file(AGENT_AUDIT_PATH, existing, sizeof(existing) - 1, &n);
    existing[n] = '\0';
    size_t add = strlen(line);
    if (n + add + 2 >= sizeof(existing)) {
        size_t keep = sizeof(existing) / 2;
        if (n > keep)
            memmove(existing, existing + (n - keep), keep + 1), n = keep;
    }
    if (n + add + 2 < sizeof(existing)) {
        memcpy(existing + n, line, add);
        n += add;
        existing[n++] = '\n';
        existing[n] = '\0';
        vfs_write_file(AGENT_AUDIT_PATH, existing, n);
    }
}

void agent_audit_event(const char *op, const char *target, const char *decision) {
    char line[192];
    size_t o = 0;
    const char *parts[4] = { "agent", op, target ? target : "-", decision ? decision : "?" };
    for (int p = 0; p < 4; p++) {
        if (p)
            line[o++] = '|';
        for (const char *s = parts[p]; *s && o + 2 < sizeof(line); s++)
            line[o++] = *s;
    }
    line[o] = '\0';
    agent_audit_append(line);
}
