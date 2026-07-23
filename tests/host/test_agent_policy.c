/*
 * Host tests for agent policy + audit (kernel/agent_policy.c, agent_tools.c,
 * agent_planner.c) under PEAK_HOST_TEST stubs.
 */
#include "agent_internal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void agent_host_vfs_reset(void);

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void seed_policy(const char *body) {
    expect(vfs_write_file(AGENT_POLICY_PATH, body, strlen(body)) == 0, "seed policy");
}

static size_t audit_len(void) {
    struct vfs_stat st;
    if (vfs_stat(AGENT_AUDIT_PATH, &st) != 0)
        return 0;
    return st.size;
}

static void read_audit(char *buf, size_t cap, size_t *n) {
    *n = 0;
    if (cap)
        buf[0] = '\0';
    if (vfs_read_file(AGENT_AUDIT_PATH, buf, cap ? cap - 1 : 0, n) != 0) {
        *n = 0;
        return;
    }
    if (cap)
        buf[*n < cap ? *n : cap - 1] = '\0';
}

static void test_path_policy(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();

    expect(agent_policy_path_allowed("/home/dev/workspace/x.c"), "allow workspace file");
    expect(agent_policy_path_allowed("/var/peak/sessions/memory.txt"), "allow sessions");
    expect(!agent_policy_path_allowed("/etc/passwd"), "deny /etc/passwd");
    expect(!agent_policy_path_allowed("/home/dev/workspaceevil"), "reject prefix bypass");
    expect(!agent_policy_path_allowed("/var/peak/audit.log"), "audit not under allow_paths");

    char norm[VFS_PATH_MAX];
    expect(agent_policy_normalize_path("/home/dev/workspace/./a", norm, sizeof(norm)) == 0,
           "normalize dot");
    expect(!strcmp(norm, "/home/dev/workspace/a"), "normalized path");
    expect(agent_policy_normalize_path("/home/dev/../etc/passwd", norm, sizeof(norm)) == 0,
           "normalize resolves ..");
    expect(!strcmp(norm, "/home/etc/passwd"), "resolved sibling under /home");
    expect(!agent_policy_path_allowed("/home/dev/../etc/passwd"), "path policy denies escaped path");
    expect(agent_policy_normalize_path("relative", norm, sizeof(norm)) != 0, "reject relative");
}

static void test_tool_policy_reload(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    expect(agent_policy_tool_allowed("fs.read"), "default allow fs.read");
    expect(agent_policy_tool_allowed("console.print"), "default allow console.print");
    expect(agent_policy_write_requires_approval(), "default require write approval");

    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=fs.read,fs.list\n"
        "deny_tools=fs.write\n"
        "require_approval=0\n");
    agent_policy_reload();
    expect(agent_policy_tool_allowed("fs.read"), "reload allow fs.read");
    expect(!agent_policy_tool_allowed("fs.write"), "reload deny fs.write");
    expect(agent_policy_tool_allowed("fs.list"), "reload allow fs.list");
    expect(!agent_policy_tool_allowed("console.print"), "reload omit console.print");
    expect(!agent_policy_write_requires_approval(), "reload require_approval=0");
    expect(agent_policy_path_allowed("/home/dev/workspace/a"), "reload path allow");
    expect(!agent_policy_path_allowed("/var/peak/sessions/x"), "reload path deny sessions");
}

static void test_audit_append_and_event(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();

    agent_audit_event("fs.read", "/home/dev/workspace/a", "ok");
    char buf[512];
    size_t n = 0;
    read_audit(buf, sizeof(buf), &n);
    expect(n > 0, "audit non-empty after event");
    expect(strstr(buf, "agent|fs.read|/home/dev/workspace/a|ok") != NULL, "event format");

    size_t before = audit_len();
    agent_audit_append("goal complete");
    expect(audit_len() > before, "append grows audit");
    read_audit(buf, sizeof(buf), &n);
    expect(strstr(buf, "goal complete") != NULL, "append line present");
}

static void test_audit_truncate_keeps_tail(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();

    /* Grow past the 2 KiB working set so append must keep the true file tail. */
    char chunk[200];
    memset(chunk, 'A', sizeof(chunk) - 1);
    chunk[sizeof(chunk) - 1] = '\0';
    for (int i = 0; i < 20; i++) {
        char line[220];
        snprintf(line, sizeof(line), "old-%02d-%s", i, chunk);
        agent_audit_append(line);
    }
    expect(audit_len() > 1024, "audit grew large");

    agent_audit_append("TAIL-MARKER-RECENT");
    char *full = (char *)malloc(8192);
    expect(full != NULL, "alloc audit readback");
    if (!full)
        return;
    size_t n = 0;
    expect(vfs_read_file(AGENT_AUDIT_PATH, full, 8191, &n) == 0, "read oversized audit");
    full[n < 8191 ? n : 8191] = '\0';
    expect(strstr(full, "TAIL-MARKER-RECENT") != NULL, "truncate keeps recent marker");
    expect(audit_len() <= 2048, "audit bounded after truncate");
    free(full);
}

static void test_deny_audit_write(void) {
    agent_host_vfs_reset();
    /* Broad allow_paths so deny-audit (not deny-path) is the deciding check. */
    seed_policy(
        "allow_paths=/home/dev/workspace,/var/peak\n"
        "allow_tools=fs.read,fs.write,fs.list,console.print\n"
        "deny_tools=\n"
        "require_approval=0\n");
    agent_policy_reload();
    expect(agent_policy_path_allowed(AGENT_AUDIT_PATH), "policy allows audit path for test");

    int wr = agent_tool_fs_write(AGENT_AUDIT_PATH, "tamper", 1);
    expect(wr != 0, "tool write to audit denied");

    char buf[256];
    size_t n = 0;
    read_audit(buf, sizeof(buf), &n);
    expect(strstr(buf, "deny-audit") != NULL, "deny-audit event logged");
    expect(strstr(buf, "tamper") == NULL, "audit body not overwritten by tool");
}

static void test_planner_audit_and_help(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace,/var/peak/sessions\n"
        "allow_tools=fs.read,fs.write,fs.list,console.print\n"
        "deny_tools=proc.exec\n"
        "require_approval=0\n");
    agent_policy_reload();

    agent_audit_append("agent|fs.list|/home/dev/workspace|ok");

    char summary[128];
    agent_plan_goal("show audit", summary, sizeof(summary));
    expect(!strcmp(summary, "showed audit"), "audit intent summary");

    agent_plan_goal("help", summary, sizeof(summary));
    expect(!strcmp(summary, "help"), "help intent summary");
}

int main(void) {
    test_path_policy();
    test_tool_policy_reload();
    test_audit_append_and_event();
    test_audit_truncate_keeps_tail();
    test_deny_audit_write();
    test_planner_audit_and_help();

    if (fails) {
        fprintf(stderr, "%d agent policy test(s) failed\n", fails);
        return 1;
    }
    printf("test_agent_policy: ok\n");
    return 0;
}
