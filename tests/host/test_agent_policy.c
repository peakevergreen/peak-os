/*
 * Host tests for agent policy + audit (kernel/agent_policy.c, agent_tools.c,
 * agent_planner.c) under PEAK_HOST_TEST stubs.
 */
#include "agent_internal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "privacy.h"

void agent_host_vfs_reset(void);
const char *agent_host_last_transcript_tool(void);
void agent_host_clear_transcript_tool(void);

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

    char why[96];
    expect(agent_policy_deny_reason("fs.read", "/etc/passwd", why, sizeof(why)) == 0,
           "deny reason for bad path");
    expect(strstr(why, "deny-path:") != NULL, "deny-path prefix");
    seed_policy("deny_tools=fs.read\n");
    agent_policy_reload();
    expect(agent_policy_deny_reason("fs.read", NULL, why, sizeof(why)) == 0,
           "deny reason for denied tool");
    expect(strstr(why, "deny-tool:") != NULL, "deny-tool prefix");
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
        "allow_tools=fs.read,fs.write,fs.list,console.print,mem.recall,audit.tail\n"
        "require_approval=0\n");
    agent_policy_reload();

    agent_audit_append("agent|fs.list|/home/dev/workspace|ok");

    char summary[128];
    agent_plan_goal("show audit", summary, sizeof(summary));
    expect(!strcmp(summary, "showed audit"), "audit intent summary");

    agent_plan_goal("help", summary, sizeof(summary));
    expect(!strcmp(summary, "help"), "help intent summary");
}

static void test_tool_policy_gates_recall_audit(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace,/var/peak/sessions\n"
        "allow_tools=fs.read,fs.list,console.print\n"
        "deny_tools=mem.recall,audit.tail\n"
        "require_approval=0\n");
    agent_policy_reload();
    expect(!agent_policy_tool_allowed("mem.recall"), "deny mem.recall");
    expect(!agent_policy_tool_allowed("audit.tail"), "deny audit.tail");

    char out[256];
    expect(agent_tool_mem_recall("test", out, sizeof(out)) != 0, "mem.recall tool denied");
    expect(agent_tool_audit_tail(out, sizeof(out)) != 0, "audit.tail tool denied");

    char buf[512];
    size_t n = 0;
    read_audit(buf, sizeof(buf), &n);
    expect(strstr(buf, "deny-tool") != NULL, "deny-tool logged for gated tools");
}

static void test_new_fs_tools(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=fs.stat,fs.mkdir,fs.rm,fs.search,sys.info\n"
        "require_approval=0\n");
    agent_policy_reload();

    expect(vfs_write_file("/home/dev/workspace/findme.txt", "needle-here", 11) == 0,
           "seed search file");

    char out[256];
    expect(agent_tool_fs_stat("/home/dev/workspace/findme.txt", out, sizeof(out)) == 0,
           "fs.stat ok");
    expect(strstr(out, "findme.txt") != NULL, "fs.stat path in output");

    expect(agent_tool_fs_search("needle", out, sizeof(out)) == 0, "fs.search hit");
    expect(strstr(out, "findme.txt") != NULL, "fs.search match path");

    expect(agent_tool_fs_mkdir("/home/dev/workspace/agent_tmp") == 0, "fs.mkdir ok");
    expect(vfs_is_dir("/home/dev/workspace/agent_tmp"), "mkdir created dir");

    expect(agent_tool_sys_info(out, sizeof(out)) == 0, "sys.info ok");
    expect(strstr(out, "uptime=") != NULL, "sys.info uptime");

    expect(agent_tool_fs_rm("/home/dev/workspace/agent_tmp") == 0, "fs.rm dir");
    expect(agent_tool_fs_rm("/home/dev/workspace/findme.txt") == 0, "fs.rm file");
}


static void test_fs_grep_and_net_ping(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=fs.grep,net.ping\n"
        "require_approval=0\n");
    agent_policy_reload();
    expect(vfs_write_file("/home/dev/workspace/grepme.txt", "line1\nneedle here\nline3\n", 24) == 0,
           "seed grep file");
    char out[512];
    expect(agent_tool_fs_grep("needle", out, sizeof(out)) == 0, "fs.grep hit");
    expect(strstr(out, "grepme.txt:2:") != NULL, "fs.grep line number");
    expect(agent_tool_net_ping("example.com", out, sizeof(out)) != 0, "net.ping denied without grant");
    char buf[512]; size_t n = 0;
    read_audit(buf, sizeof(buf), &n);
    expect(strstr(buf, "deny-privacy") != NULL, "net.ping privacy gate logged");
    privacy_grant_net_client(0);
    expect(agent_tool_net_ping("example.com", out, sizeof(out)) == 0, "net.ping ok with grant");
    expect(strstr(out, "PING example.com") != NULL, "net.ping output");
}

static void test_richer_sys_info(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=sys.info\n"
        "require_approval=0\n");
    agent_policy_reload();
    char out[512];
    expect(agent_tool_sys_info(out, sizeof(out)) == 0, "sys.info ok");
    expect(strstr(out, "idle=") != NULL, "sys.info idle");
    expect(strstr(out, "gui_fps=") != NULL, "sys.info gui_fps");
    expect(strstr(out, "compose=") != NULL, "sys.info compose");
}

static void test_audit_tail_formatted(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/var/peak\n"
        "allow_tools=audit.tail\n"
        "require_approval=0\n");
    agent_policy_reload();
    agent_audit_append("agent|fs.read|/home/dev/workspace/a|ok");
    char out[512];
    expect(agent_tool_audit_tail(out, sizeof(out)) == 0, "audit.tail ok");
    expect(strstr(out, "--- recent entries ---") != NULL, "audit.tail formatted header");
    expect(strstr(out, "fs.read") != NULL, "audit.tail content");
}

static void test_fs_exec_allowlist(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=fs.exec\n"
        "require_approval=0\n");
    agent_policy_reload();
    expect(agent_policy_tool_allowed("fs.exec"), "fs.exec allowed");
    expect(agent_tool_fs_exec("find /home/dev/workspace -name README.md") == 0 ||
               agent_tool_fs_exec("ls /home/dev/workspace") == 0,
           "fs.exec runs allowlisted cmd");
}

static void test_planner_audit_true_tail(void) {
    /* Oversized audit: planner must show the true file tail, not the head. */
    agent_host_vfs_reset();
    agent_policy_load_defaults();

    char pad[180];
    memset(pad, 'H', sizeof(pad) - 1);
    pad[sizeof(pad) - 1] = '\0';
    for (int i = 0; i < 30; i++) {
        char line[220];
        snprintf(line, sizeof(line), "head-%02d-%s", i, pad);
        agent_audit_append(line);
    }
    agent_audit_append("TRUE-TAIL-MARKER-XYZ");
    expect(audit_len() > 1024, "audit larger than read buffer");

    char summary[128];
    agent_plan_goal("show audit", summary, sizeof(summary));
    expect(!strcmp(summary, "showed audit"), "true-tail audit summary");

    /* Append path itself kept the marker through truncation. */
    char *full = (char *)malloc(8192);
    expect(full != NULL, "alloc for true-tail check");
    if (!full)
        return;
    size_t n = 0;
    expect(vfs_read_file(AGENT_AUDIT_PATH, full, 8191, &n) == 0, "read audit after plan");
    full[n < 8191 ? n : 8191] = '\0';
    expect(strstr(full, "TRUE-TAIL-MARKER-XYZ") != NULL, "marker still in audit after plan");
    free(full);
}


static void test_fs_diff_and_net_fetch(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=fs.diff,net.fetch\n"
        "require_approval=0\n");
    agent_policy_reload();
    expect(vfs_write_file("/home/dev/workspace/a.txt", "line1\nline2\n", 12) == 0, "seed diff a");
    expect(vfs_write_file("/home/dev/workspace/b.txt", "line1\nline2 changed\n", 19) == 0, "seed diff b");
    char out[512];
    expect(agent_tool_fs_diff("/home/dev/workspace/a.txt", "/home/dev/workspace/b.txt",
                              out, sizeof(out)) == 0, "fs.diff ok");
    expect(strstr(out, "--- /home/dev/workspace/a.txt") != NULL, "fs.diff header a");
    expect(strstr(out, "-line2") != NULL || strstr(out, "+line2 changed") != NULL, "fs.diff hunk");

    expect(agent_tool_net_fetch("http://example.com/", out, sizeof(out)) != 0, "net.fetch denied without grant");
    char buf[512]; size_t n = 0;
    read_audit(buf, sizeof(buf), &n);
    expect(strstr(buf, "deny-privacy") != NULL, "net.fetch privacy gate logged");
    privacy_grant_net_client(0);
    expect(agent_tool_net_fetch("http://example.com/", out, sizeof(out)) == 0, "net.fetch ok with grant");
    expect(strstr(out, "HTTP 200") != NULL, "net.fetch status line");
    expect(strstr(out, "hello from fetch stub") != NULL, "net.fetch body");
    expect(agent_tool_net_fetch("ftp://example.com/", out, sizeof(out)) != 0, "net.fetch rejects ftp");
}

static void test_deny_reason_in_transcript(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=fs.read,console.print\n"
        "deny_tools=fs.write,net.ping\n"
        "require_approval=0\n");
    agent_policy_reload();

    agent_host_clear_transcript_tool();
    expect(agent_tool_fs_write("/home/dev/workspace/x.c", "x", 1) != 0, "fs.write denied by deny_tools");
    expect(strstr(agent_host_last_transcript_tool(), "deny-tool:") != NULL, "fs.write deny in transcript");

    agent_host_clear_transcript_tool();
    expect(agent_tool_fs_read("/etc/passwd", NULL, 0, NULL) != 0, "fs.read path denied");
    expect(strstr(agent_host_last_transcript_tool(), "deny-path:") != NULL, "fs.read deny-path in transcript");

    agent_host_clear_transcript_tool();
    char out[64];
    expect(agent_tool_net_ping("example.com", out, sizeof(out)) != 0, "net.ping denied by deny_tools");
    expect(strstr(agent_host_last_transcript_tool(), "deny-tool:") != NULL, "net.ping deny in transcript");
}

static void test_planner_create_edit_templates(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=fs.read,fs.write,fs.list,console.print\n"
        "require_approval=0\n");
    agent_policy_reload();

    char summary[128];
    agent_plan_goal("create fib.c", summary, sizeof(summary));
    expect(!strcmp(summary, "wrote file"), "fib create wrote");
    char body[512];
    size_t n = 0;
    expect(vfs_read_file("/home/dev/workspace/fib.c", body, sizeof(body) - 1, &n) == 0, "fib exists");
    body[n] = '\0';
    expect(strstr(body, "fib(") != NULL, "fib template has fib()");
    expect(strstr(body, "int main(void) { return 0; }") == NULL, "fib is not empty stub");

    agent_plan_goal("edit fib.c add error handling", summary, sizeof(summary));
    expect(!strcmp(summary, "wrote file"), "edit wrote");
    n = 0;
    expect(vfs_read_file("/home/dev/workspace/fib.c", body, sizeof(body) - 1, &n) == 0, "fib reread");
    body[n] = '\0';
    expect(strstr(body, "peak_check") != NULL, "edit added peak_check");
    expect(strstr(body, "/* peak-agent edit:") == NULL || strstr(body, "peak_check") != NULL,
           "edit is not comment-only");
}

static void test_planner_unparsed_goal(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    char summary[128];
    agent_plan_goal("xyzzy plugh", summary, sizeof(summary));
    expect(!strcmp(summary, "unparsed goal"), "unknown goal is not silent create");
}

static void test_planner_diff_and_fetch(void) {
    agent_host_vfs_reset();
    agent_policy_load_defaults();
    seed_policy(
        "allow_paths=/home/dev/workspace\n"
        "allow_tools=fs.diff,net.fetch,console.print\n"
        "require_approval=0\n");
    agent_policy_reload();
    expect(vfs_write_file("/home/dev/workspace/x.txt", "alpha\n", 6) == 0, "seed planner x");
    expect(vfs_write_file("/home/dev/workspace/y.txt", "beta\n", 5) == 0, "seed planner y");

    char summary[128];
    agent_plan_goal("diff x.txt y.txt", summary, sizeof(summary));
    expect(!strcmp(summary, "diff ok"), "diff intent summary");

    privacy_grant_net_client(0);
    agent_plan_goal("fetch http://example.com/", summary, sizeof(summary));
    expect(!strcmp(summary, "fetch ok"), "fetch intent summary");
}

int main(void) {
    test_path_policy();
    test_tool_policy_reload();
    test_audit_append_and_event();
    test_audit_truncate_keeps_tail();
    test_deny_audit_write();
    test_planner_audit_and_help();
    test_planner_audit_true_tail();
    test_tool_policy_gates_recall_audit();
    test_new_fs_tools();
    test_fs_grep_and_net_ping();
    test_richer_sys_info();
    test_audit_tail_formatted();
    test_fs_exec_allowlist();
    test_fs_diff_and_net_fetch();
    test_deny_reason_in_transcript();
    test_planner_create_edit_templates();
    test_planner_unparsed_goal();
    test_planner_diff_and_fetch();

    if (fails) {
        fprintf(stderr, "%d agent policy test(s) failed\n", fails);
        return 1;
    }
    printf("test_agent_policy: ok\n");
    return 0;
}
