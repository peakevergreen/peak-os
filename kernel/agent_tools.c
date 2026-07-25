#include "agent_internal.h"
#include "console.h"
#include "vfs.h"
#include "util.h"
#include "ubin.h"
#include "peakvec.h"
#include "sysmon.h"

static const char tools_catalog[] =
    "fs.read,fs.write,fs.list,fs.exec,fs.stat,fs.mkdir,fs.rm,fs.search,"
    "sys.info,mem.recall,audit.tail,console.print";

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
    /* UI only — goals/paths must not mirror to COM1 (privacy.md). */
    console_write_ui(msg ? msg : "");
    if (msg && msg[0] && msg[strlen(msg) - 1] != '\n')
        console_write_ui("\n");
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

static int exec_cmd_allowed(const char *cmd) {
    static const char *allow[] = {
        "ls", "cat", "wc", "stat", "du", "df", "which", "basename", "dirname",
        "realpath", "head", "tail", "grep", "sort", "uniq", "find", "sha256sum",
        NULL
    };
    for (int i = 0; allow[i]; i++) {
        if (!strcmp(cmd, allow[i]))
            return 1;
    }
    return 0;
}

static int exec_line_safe(const char *line) {
    if (!line || !line[0])
        return 0;
    for (const char *p = line; *p; p++) {
        char c = *p;
        if (c == '|' || c == ';' || c == '&' || c == '`' || c == '$' ||
            c == '>' || c == '<' || c == '\n')
            return 0;
    }
    return 1;
}

int agent_tool_fs_exec(const char *line) {
    if (!agent_policy_tool_allowed("fs.exec")) {
        agent_audit_event("fs.exec", line, "deny-tool");
        return -1;
    }
    if (!exec_line_safe(line)) {
        agent_audit_event("fs.exec", line, "deny-syntax");
        return -1;
    }
    char linebuf[128];
    size_t n = strlen(line);
    if (n >= sizeof(linebuf))
        n = sizeof(linebuf) - 1;
    memcpy(linebuf, line, n);
    linebuf[n] = '\0';
    char *p = linebuf;
    char cmd[32];
    size_t i = 0;
    while (*p == ' ')
        p++;
    for (; *p && *p != ' ' && i + 1 < sizeof(cmd); p++)
        cmd[i++] = *p;
    cmd[i] = '\0';
    if (!cmd[0] || !exec_cmd_allowed(cmd)) {
        agent_audit_event("fs.exec", line, "deny-cmd");
        return -1;
    }
    char *argv[16];
    int argc = 0;
    argv[argc++] = cmd;
    while (*p == ' ')
        p++;
    while (*p && argc + 1 < 16) {
        argv[argc++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
        while (*p == ' ')
            p++;
    }
    argv[argc] = NULL;
    char path[64];
    snprintf(path, sizeof(path), "/bin/%s", cmd);
    int rc = ubin_run(path, argc, argv);
    agent_audit_event("fs.exec", line, rc == 0 ? "ok" : "fail");
    return rc == 0 ? 0 : -1;
}

int agent_tool_fs_stat(const char *path, char *out, size_t out_len) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.stat")) {
        agent_audit_event("fs.stat", path, "deny-tool");
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.stat", path, "deny-path");
        return -1;
    }
    struct vfs_stat st;
    if (vfs_stat(norm, &st) != 0) {
        agent_audit_event("fs.stat", norm, "fail");
        return -1;
    }
    if (out && out_len) {
        snprintf(out, out_len, "%s %c %llu refs=%u children=%u",
                 norm, st.type == VFS_DIR ? 'd' : 'f', (unsigned long long)st.size,
                 (unsigned)st.refs, (unsigned)st.nchildren);
    }
    agent_audit_event("fs.stat", norm, "ok");
    return 0;
}

int agent_tool_fs_mkdir(const char *path) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.mkdir")) {
        agent_audit_event("fs.mkdir", path, "deny-tool");
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.mkdir", path, "deny-path");
        return -1;
    }
    if (!vfs_mkdir(norm)) {
        agent_audit_event("fs.mkdir", norm, "fail");
        return -1;
    }
    agent_audit_event("fs.mkdir", norm, "ok");
    return 0;
}

int agent_tool_fs_rm(const char *path) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.rm")) {
        agent_audit_event("fs.rm", path, "deny-tool");
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.rm", path, "deny-path");
        return -1;
    }
    if (!strcmp(norm, AGENT_AUDIT_PATH) || !strcmp(norm, AGENT_MEM_PATH)) {
        agent_audit_event("fs.rm", norm, "deny-protected");
        return -1;
    }
    int rc = vfs_is_dir(norm) ? vfs_remove_tree(norm) : vfs_unlink(norm);
    agent_audit_event("fs.rm", norm, rc == 0 ? "ok" : "fail");
    return rc == 0 ? 0 : -1;
}

struct search_ctx {
    const char *needle;
    char *out;
    size_t out_len;
    size_t out_off;
    int matches;
    int max_matches;
};

static int search_walk_cb(const char *path, struct vfs_node *node, void *ctx) {
    struct search_ctx *sc = ctx;
    if (!sc || sc->matches >= sc->max_matches || !node)
        return 0;
    if (node->type != VFS_FILE)
        return 0;
    int hit = 0;
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            base = p + 1;
    if (sc->needle && sc->needle[0]) {
        for (const char *p = base; *p; p++) {
            const char *a = p, *b = sc->needle;
            while (*a && *b && *a == *b) {
                a++;
                b++;
            }
            if (!*b) {
                hit = 1;
                break;
            }
        }
    }
    if (!hit && node->size && sc->needle && sc->needle[0]) {
        char buf[512];
        size_t n = 0;
        size_t cap = node->size < sizeof(buf) - 1 ? node->size : sizeof(buf) - 1;
        if (vfs_read_file(path, buf, cap, &n) == 0 && n) {
            buf[n] = '\0';
            size_t nl = strlen(sc->needle);
            for (size_t i = 0; i + nl <= n; i++) {
                if (!memcmp(buf + i, sc->needle, nl)) {
                    hit = 1;
                    break;
                }
            }
        }
    }
    if (hit && sc->out_off + 2 < sc->out_len) {
        if (sc->matches)
            sc->out[sc->out_off++] = '\n';
        for (const char *p = path; *p && sc->out_off + 1 < sc->out_len; p++)
            sc->out[sc->out_off++] = *p;
        sc->out[sc->out_off] = '\0';
        sc->matches++;
    }
    return 0;
}

int agent_tool_fs_search(const char *needle, char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("fs.search")) {
        agent_audit_event("fs.search", needle ? needle : "-", "deny-tool");
        return -1;
    }
    if (!needle || !needle[0] || !out || out_len < 4) {
        agent_audit_event("fs.search", "-", "fail");
        return -1;
    }
    out[0] = '\0';
    struct search_ctx sc;
    sc.needle = needle;
    sc.out = out;
    sc.out_len = out_len;
    sc.out_off = 0;
    sc.matches = 0;
    sc.max_matches = 16;
    vfs_walk("/home/dev/workspace", search_walk_cb, &sc);
    agent_audit_event("fs.search", needle, sc.matches ? "ok" : "empty");
    return sc.matches ? 0 : -1;
}

int agent_tool_sys_info(char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("sys.info")) {
        agent_audit_event("sys.info", "-", "deny-tool");
        return -1;
    }
    if (!out || out_len < 32)
        return -1;
    sysmon_poll();
    const struct sysmon_sample *s = sysmon_latest();
    if (!s) {
        agent_audit_event("sys.info", "-", "fail");
        return -1;
    }
    char rx[16], tx[16];
    sysmon_format_rate(s->rx_bps, rx, sizeof(rx));
    sysmon_format_rate(s->tx_bps, tx, sizeof(tx));
    snprintf(out, out_len,
             "uptime=%lus mem=%u%% heap=%u%% load=%u%% tasks=%u net_rx=%s net_tx=%s vfs_nodes=%lu",
             (unsigned long)s->uptime_secs, (unsigned)s->mem_pct, (unsigned)s->heap_pct,
             (unsigned)s->load_pct, (unsigned)s->tasks, rx, tx,
             (unsigned long)s->vfs_nodes);
    agent_audit_event("sys.info", "-", "ok");
    return 0;
}

int agent_tool_mem_recall(const char *goal, char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("mem.recall")) {
        agent_audit_event("mem.recall", goal ? goal : "-", "deny-tool");
        return -1;
    }
    if (!out || out_len < 8)
        return -1;
    out[0] = '\0';
    size_t o = 0;

    {
        int16_t q[PEAKVEC_DIM];
        peakvec_embed_text(goal ? goal : "", q);
        struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
        int n = peakvec_query("agent", q, 3, hits);
        if (n > 0) {
            const char *hdr = "[recall/vec]\n";
            for (const char *p = hdr; *p && o + 1 < out_len; p++)
                out[o++] = *p;
            for (int i = 0; i < n; i++) {
                if (!hits[i].key[0])
                    continue;
                for (const char *p = hits[i].meta; *p && o + 1 < out_len; p++)
                    out[o++] = *p;
                if (o + 1 < out_len)
                    out[o++] = '\n';
            }
        }
    }

    char mem[AGENT_MEMORY_TAIL_MAX];
    size_t n = 0;
    if (vfs_read_file(AGENT_MEM_PATH, mem, sizeof(mem) - 1, &n) == 0 && n > 0) {
        mem[n] = '\0';
        const char *hdr = "[recall/memory]\n";
        for (const char *p = hdr; *p && o + 1 < out_len; p++)
            out[o++] = *p;
        size_t start = 0;
        if (n > 400)
            start = n - 400;
        for (size_t i = start; i < n && o + 1 < out_len; i++)
            out[o++] = mem[i];
    }
    out[o] = '\0';
    agent_audit_event("mem.recall", goal ? goal : "-", o ? "ok" : "empty");
    return o ? 0 : -1;
}

int agent_tool_audit_tail(char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("audit.tail")) {
        agent_audit_event("audit.tail", "-", "deny-tool");
        return -1;
    }
    if (!out || out_len < 8)
        return -1;
    out[0] = '\0';
    size_t file_sz = 0;
    struct vfs_stat st;
    if (vfs_stat(AGENT_AUDIT_PATH, &st) == 0 && st.type == VFS_FILE)
        file_sz = st.size;
    size_t want = out_len > 400 ? 400 : out_len - 1;
    if (file_sz == 0) {
        agent_audit_event("audit.tail", "-", "empty");
        return -1;
    }
    size_t off = file_sz > want ? file_sz - want : 0;
    size_t n = 0;
    if (vfs_read_at(AGENT_AUDIT_PATH, off, out, want, &n) != 0 || !n) {
        agent_audit_event("audit.tail", "-", "fail");
        return -1;
    }
    out[n < out_len ? n : out_len - 1] = '\0';
    agent_audit_event("audit.tail", "-", "ok");
    return 0;
}
