#include "agent_internal.h"
#include "console.h"
#include "vfs.h"
#include "util.h"
#include "ubin.h"
#include "peakvec.h"
#include "sysmon.h"
#include "privacy.h"
#include "net.h"
#include "timer.h"
#include "sched.h"

static const char tools_catalog[] =
    "fs.read,fs.write,fs.list,fs.exec,fs.stat,fs.mkdir,fs.rm,fs.search,fs.grep,fs.diff,"
    "sys.info,sys.ps,net.ping,net.fetch,mem.recall,mem.store,peakvec.query,audit.tail,"
    "console.print,fs.tree";

static void agent_tool_note_deny(const char *tool, const char *path) {
    char why[96];
    if (agent_policy_deny_reason(tool, path, why, sizeof(why)) == 0)
        agent_transcript_note_tool(why);
}

/* Index workspace file path + short excerpt into PeakVec namespace "ws". */
static void agent_ws_peakvec_upsert(const char *path, const char *content) {
    if (!path || !path[0])
        return;
    int16_t vec[PEAKVEC_DIM];
    char meta[PEAKVEC_META_MAX];
    size_t mi = 0;
    const char *p = path;
    while (*p && mi + 1 < sizeof(meta))
        meta[mi++] = *p++;
    if (content && content[0] && mi + 2 < sizeof(meta)) {
        meta[mi++] = '|';
        for (size_t i = 0; content[i] && mi + 1 < sizeof(meta) && i < 96; i++) {
            char c = content[i];
            if (c == '\n' || c == '|')
                c = ' ';
            meta[mi++] = c;
        }
    }
    meta[mi] = '\0';
    /* Embed path + excerpt so queries like "fib" hit file keys. */
    peakvec_embed_text(meta, vec);
    (void)peakvec_upsert("ws", path, vec, meta);
}

const char *agent_tools_catalog(void) {
    return tools_catalog;
}

size_t agent_tools_catalog_len(void) {
    return strlen(tools_catalog);
}

int agent_tool_console_print(const char *msg) {
    if (!agent_policy_tool_allowed("console.print")) {
        agent_audit_event("console.print", "-", "deny-tool");
        agent_tool_note_deny("console.print", NULL);
        return -1;
    }
    console_write_ui(msg ? msg : "");
    if (msg && msg[0] && msg[strlen(msg) - 1] != '\n')
        console_write_ui("\n");
    /* Plan lines stay distinct in the Agent transcript (not [tool]-prefixed). */
    if (msg && !strncmp(msg, "[plan]", 6))
        agent_transcript_push(msg);
    else
        agent_transcript_note_tool(msg);
    agent_audit_event("console.print", "-", "ok");
    return 0;
}

int agent_tool_fs_read(const char *path, char *out, size_t out_len, size_t *out_n) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.read")) {
        agent_audit_event("fs.read", path, "deny-tool");
        agent_tool_note_deny("fs.read", path);
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.read", path, "deny-path");
        agent_tool_note_deny("fs.read", path);
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
        agent_tool_note_deny("fs.write", path);
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.write", path, "deny-path");
        agent_tool_note_deny("fs.write", path);
        return -1;
    }
    if (!strcmp(norm, AGENT_AUDIT_PATH)) {
        agent_audit_event("fs.write", norm, "deny-audit");
        agent_tool_note_deny("fs.write", norm);
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
    agent_ws_peakvec_upsert(norm, content);
    agent_audit_event("fs.write", norm, "ok");
    return 0;
}

int agent_tool_fs_list(const char *path, char *out, size_t out_len) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.list")) {
        agent_audit_event("fs.list", path, "deny-tool");
        agent_tool_note_deny("fs.list", path);
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0)
        return -1;
    if (!agent_policy_path_allowed(norm) && strcmp(norm, "/home/dev") != 0) {
        agent_audit_event("fs.list", norm, "deny-path");
        agent_tool_note_deny("fs.list", norm);
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
        "diff", "patch", "join", "comm", "xargs", "pgrep", "dmesg", "ping",
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
        agent_tool_note_deny("fs.exec", NULL);
        return -1;
    }
    if (!exec_line_safe(line)) {
        agent_audit_event("fs.exec", line, "deny-syntax");
        agent_transcript_note_tool("deny-syntax: fs.exec rejects shell metacharacters");
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
        agent_transcript_note_tool("deny-cmd: fs.exec command not on allowlist");
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
        agent_tool_note_deny("fs.stat", path);
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.stat", path, "deny-path");
        agent_tool_note_deny("fs.stat", path);
        return -1;
    }
    struct vfs_stat st;
    if (vfs_stat(norm, &st) != 0) {
        agent_audit_event("fs.stat", norm, "fail");
        return -1;
    }
    if (out && out_len) {
        char modebuf[12];
        vfs_mode_string(st.type, st.mode, modebuf, sizeof(modebuf));
        snprintf(out, out_len, "%s %04o %s %llu refs=%u children=%u",
                 norm, (unsigned)st.mode, modebuf,
                 (unsigned long long)st.size,
                 (unsigned)st.refs, (unsigned)st.nchildren);
    }
    agent_audit_event("fs.stat", norm, "ok");
    return 0;
}

int agent_tool_fs_mkdir(const char *path) {
    char norm[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.mkdir")) {
        agent_audit_event("fs.mkdir", path, "deny-tool");
        agent_tool_note_deny("fs.mkdir", path);
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.mkdir", path, "deny-path");
        agent_tool_note_deny("fs.mkdir", path);
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
        agent_tool_note_deny("fs.rm", path);
        return -1;
    }
    if (agent_policy_normalize_path(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.rm", path, "deny-path");
        agent_tool_note_deny("fs.rm", path);
        return -1;
    }
    if (!strcmp(norm, AGENT_AUDIT_PATH) || !strcmp(norm, AGENT_MEM_PATH)) {
        agent_audit_event("fs.rm", norm, "deny-protected");
        agent_transcript_note_tool("deny-protected: audit/memory paths cannot be removed");
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
        agent_tool_note_deny("fs.search", NULL);
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

struct grep_ctx {
    const char *needle;
    char *out;
    size_t out_len;
    size_t out_off;
    int matches;
    int max_matches;
};

static int grep_file_lines(const char *path, struct grep_ctx *gc) {
    char buf[1024];
    size_t n = 0;
    if (vfs_read_file(path, buf, sizeof(buf) - 1, &n) != 0 || !n)
        return 0;
    size_t nl = gc->needle ? strlen(gc->needle) : 0;
    if (!nl)
        return 0;
    int line_no = 1;
    const char *line = buf;
    for (size_t i = 0; i <= n && gc->matches < gc->max_matches; i++) {
        if (i == n || buf[i] == '\n') {
            size_t ll = (size_t)(buf + i - line);
            int hit = 0;
            for (size_t j = 0; j + nl <= ll; j++) {
                if (!memcmp(line + j, gc->needle, nl)) {
                    hit = 1;
                    break;
                }
            }
            if (hit && gc->out_off + 2 < gc->out_len) {
                if (gc->matches)
                    gc->out[gc->out_off++] = '\n';
                char prefix[160];
                int pl = snprintf(prefix, sizeof(prefix), "%s:%d:", path, line_no);
                for (int k = 0; k < pl && gc->out_off + 1 < gc->out_len; k++)
                    gc->out[gc->out_off++] = prefix[k];
                size_t show = ll < 72 ? ll : 72;
                for (size_t j = 0; j < show && gc->out_off + 1 < gc->out_len; j++)
                    gc->out[gc->out_off++] = line[j];
                gc->out[gc->out_off] = '\0';
                gc->matches++;
            }
            line = buf + i + 1;
            line_no++;
        }
    }
    return gc->matches;
}

static int grep_walk_cb(const char *path, struct vfs_node *node, void *ctx) {
    struct grep_ctx *gc = ctx;
    if (!gc || gc->matches >= gc->max_matches || !node || node->type != VFS_FILE)
        return 0;
    grep_file_lines(path, gc);
    return 0;
}

static int diff_next_line(const char *buf, size_t buf_len, size_t *pos,
                          char *line, size_t line_cap) {
    if (!buf || !pos || !line || line_cap < 2)
        return 0;
    if (*pos >= buf_len) {
        line[0] = '\0';
        return 0;
    }
    size_t i = 0;
    while (*pos < buf_len && buf[*pos] != '\n' && i + 1 < line_cap)
        line[i++] = buf[(*pos)++];
    if (*pos < buf_len && buf[*pos] == '\n')
        (*pos)++;
    line[i] = '\0';
    return 1;
}

int agent_tool_fs_diff(const char *path_a, const char *path_b, char *out, size_t out_len) {
    char norm_a[VFS_PATH_MAX];
    char norm_b[VFS_PATH_MAX];
    if (!agent_policy_tool_allowed("fs.diff")) {
        agent_audit_event("fs.diff", path_a ? path_a : "-", "deny-tool");
        agent_tool_note_deny("fs.diff", path_a);
        return -1;
    }
    if (!path_a || !path_b || !out || out_len < 16) {
        agent_audit_event("fs.diff", "-", "fail");
        return -1;
    }
    if (agent_policy_normalize_path(path_a, norm_a, sizeof(norm_a)) != 0 ||
        !agent_policy_path_allowed(norm_a) ||
        agent_policy_normalize_path(path_b, norm_b, sizeof(norm_b)) != 0 ||
        !agent_policy_path_allowed(norm_b)) {
        agent_audit_event("fs.diff", path_a, "deny-path");
        agent_tool_note_deny("fs.diff", path_a);
        return -1;
    }
    char body_a[AGENT_DIFF_BODY_MAX];
    char body_b[AGENT_DIFF_BODY_MAX];
    size_t na = 0, nb = 0;
    if (vfs_read_file(norm_a, body_a, sizeof(body_a) - 1, &na) != 0 ||
        vfs_read_file(norm_b, body_b, sizeof(body_b) - 1, &nb) != 0) {
        agent_audit_event("fs.diff", norm_a, "fail");
        return -1;
    }
    body_a[na < sizeof(body_a) ? na : sizeof(body_a) - 1] = '\0';
    body_b[nb < sizeof(body_b) ? nb : sizeof(body_b) - 1] = '\0';

    size_t o = 0;
    char hdr[160];
    int hl = snprintf(hdr, sizeof(hdr), "--- %s\n+++ %s\n", norm_a, norm_b);
    for (int i = 0; i < hl && o + 1 < out_len; i++)
        out[o++] = hdr[i];

    size_t pa = 0, pb = 0;
    char la[120], lb[120];
    int line_no = 1;
    int hunks = 0;
    const int max_hunks = 24;
    while ((pa < na || pb < nb) && hunks < max_hunks && o + 8 < out_len) {
        int ha = diff_next_line(body_a, na, &pa, la, sizeof(la));
        int hb = diff_next_line(body_b, nb, &pb, lb, sizeof(lb));
        if (!ha && !hb)
            break;
        if (ha && hb && !strcmp(la, lb)) {
            line_no++;
            continue;
        }
        char hunk[32];
        int hul = snprintf(hunk, sizeof(hunk), "@@ %d @@\n", line_no);
        for (int i = 0; i < hul && o + 1 < out_len; i++)
            out[o++] = hunk[i];
        if (ha) {
            if (o + 2 < out_len)
                out[o++] = '-';
            for (const char *p = la; *p && o + 1 < out_len; p++)
                out[o++] = *p;
            if (o + 1 < out_len)
                out[o++] = '\n';
        }
        if (hb) {
            if (o + 2 < out_len)
                out[o++] = '+';
            for (const char *p = lb; *p && o + 1 < out_len; p++)
                out[o++] = *p;
            if (o + 1 < out_len)
                out[o++] = '\n';
        }
        line_no++;
        hunks++;
    }
    out[o] = '\0';
    if (!hunks) {
        const char *same = "(no differences)\n";
        for (const char *p = same; *p && o + 1 < out_len; p++)
            out[o++] = *p;
        out[o] = '\0';
    }
    char target[192];
    snprintf(target, sizeof(target), "%s|%s", norm_a, norm_b);
    agent_audit_event("fs.diff", target, hunks ? "ok" : "same");
    return 0;
}

int agent_tool_fs_grep(const char *needle, char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("fs.grep")) {
        agent_audit_event("fs.grep", needle ? needle : "-", "deny-tool");
        agent_tool_note_deny("fs.grep", NULL);
        return -1;
    }
    if (!needle || !needle[0] || !out || out_len < 4) {
        agent_audit_event("fs.grep", "-", "fail");
        return -1;
    }
    out[0] = '\0';
    struct grep_ctx gc;
    gc.needle = needle;
    gc.out = out;
    gc.out_len = out_len;
    gc.out_off = 0;
    gc.matches = 0;
    gc.max_matches = 12;
    vfs_walk("/home/dev/workspace", grep_walk_cb, &gc);
    agent_audit_event("fs.grep", needle, gc.matches ? "ok" : "empty");
    return gc.matches ? 0 : -1;
}

static void format_session_tail(const char *raw, size_t raw_len, char *out, size_t out_len) {
    if (!out || out_len < 16) {
        if (out && out_len)
            out[0] = '\0';
        return;
    }
    if (!raw || !raw_len) {
        snprintf(out, out_len, "(empty)");
        return;
    }
    int lines = 0;
    for (size_t i = 0; i < raw_len; i++)
        if (raw[i] == '\n')
            lines++;
    if (raw_len && raw[raw_len - 1] != '\n')
        lines++;

    size_t o = 0;
    char hdr[80];
    int hl = snprintf(hdr, sizeof(hdr), "(%zu bytes, %d lines)\n--- recent entries ---\n",
                      raw_len, lines);
    for (int i = 0; i < hl && o + 1 < out_len; i++)
        out[o++] = hdr[i];

    const char *start = raw;
    int tail_lines = 0;
    for (const char *p = raw + raw_len; p > raw; p--) {
        if (p[-1] == '\n') {
            tail_lines++;
            if (tail_lines > 12) {
                start = p;
                break;
            }
        }
    }

    int n = 1;
    for (const char *p = start; *p && o + 1 < out_len; p++) {
        if (p == start || p[-1] == '\n') {
            const char *eol = strchr(p, '\n');
            size_t ll = eol ? (size_t)(eol - p) : strlen(p);
            if (ll > 0) {
                char num[8];
                int nl = snprintf(num, sizeof(num), "%3d  ", n++);
                for (int i = 0; i < nl && o + 1 < out_len; i++)
                    out[o++] = num[i];
                size_t show = ll < 88 ? ll : 88;
                for (size_t j = 0; j < show && o + 1 < out_len; j++)
                    out[o++] = p[j];
                if (o + 1 < out_len)
                    out[o++] = '\n';
            }
        }
    }
    const char *end = "--- end ---\n";
    for (const char *p = end; *p && o + 1 < out_len; p++)
        out[o++] = *p;
    out[o] = '\0';
}

int agent_tool_sys_info(char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("sys.info")) {
        agent_audit_event("sys.info", "-", "deny-tool");
        agent_tool_note_deny("sys.info", NULL);
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
    char rx[16], tx[16], memu[16], heapu[16], comp[16], pres[16];
    sysmon_format_rate(s->rx_bps, rx, sizeof(rx));
    sysmon_format_rate(s->tx_bps, tx, sizeof(tx));
    sysmon_format_bytes(s->mem_used_pages * 4096ull, memu, sizeof(memu));
    sysmon_format_bytes(s->heap_used, heapu, sizeof(heapu));
    sysmon_format_us(s->compose_us, comp, sizeof(comp));
    sysmon_format_us(s->present_us, pres, sizeof(pres));
    snprintf(out, out_len,
             "uptime=%lus mem=%u%%(%s) heap=%u%%(%s) load=%u%% idle=%u%% tasks=%u\n"
             "net_rx=%s net_tx=%s ctx=%llu irq=%llu gui_fps=%u surf=%u%%\n"
             "vfs_nodes=%lu compose=%s present=%s peakvec=%uus audit=%uus",
             (unsigned long)s->uptime_secs, (unsigned)s->mem_pct, memu,
             (unsigned)s->heap_pct, heapu, (unsigned)s->load_pct, (unsigned)s->idle_pct,
             (unsigned)s->tasks, rx, tx, (unsigned long long)s->ctx_switches,
             (unsigned long long)s->irq_count, (unsigned)s->gui_fps,
             (unsigned)s->surf_pressure, (unsigned long)s->vfs_nodes, comp, pres,
             (unsigned)s->peakvec_us, (unsigned)s->agent_audit_us);
    agent_audit_event("sys.info", "-", "ok");
    return 0;
}

static int fetch_url_allowed(const char *url) {
    if (!url || !url[0])
        return 0;
    if (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8))
        return 1;
    return 0;
}

int agent_tool_net_fetch(const char *url, char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("net.fetch")) {
        agent_audit_event("net.fetch", url ? url : "-", "deny-tool");
        agent_tool_note_deny("net.fetch", NULL);
        return -1;
    }
    if (!privacy_net_client_allowed()) {
        agent_audit_event("net.fetch", url ? url : "-", "deny-privacy");
        agent_transcript_note_tool("deny-privacy: net client grant required");
        return -1;
    }
    if (!url || !url[0] || !out || out_len < 32) {
        agent_audit_event("net.fetch", "-", "fail");
        return -1;
    }
    if (!fetch_url_allowed(url)) {
        agent_audit_event("net.fetch", url, "deny-scheme");
        snprintf(out, out_len, "fetch: only http:// and https:// URLs allowed");
        agent_transcript_note_tool("deny-scheme: only http:// and https:// URLs allowed");
        return -1;
    }
    if (!net_ready()) {
        snprintf(out, out_len, "network down");
        agent_audit_event("net.fetch", url, "down");
        return -1;
    }
    char body[AGENT_FETCH_BODY_MAX];
    int status = 0;
    if (net_http_get(url, body, sizeof(body), &status) != 0 || status < 200 || status >= 300) {
        if (net_http_needs_tls()) {
            const char *why = net_http_tls_reject_name();
            snprintf(out, out_len, "fetch failed%s%s (status=%d)",
                     why && why[0] ? ": " : "", why ? why : "", status);
            agent_audit_event("net.fetch", url, "tls-fail");
            return -1;
        }
        const char *why = net_last_error();
        snprintf(out, out_len, "fetch failed%s%s (status=%d)",
                 why && why[0] ? ": " : "", why ? why : "", status);
        agent_audit_event("net.fetch", url, status ? "http-fail" : "fail");
        return -1;
    }
    size_t bl = strlen(body);
    if (bl + 32 >= out_len) {
        body[out_len > 40 ? out_len - 40 : 0] = '\0';
        bl = strlen(body);
        snprintf(out, out_len, "HTTP %d (%zu bytes, truncated)\n%s", status, bl, body);
    } else {
        snprintf(out, out_len, "HTTP %d (%zu bytes)\n%s", status, bl, body);
    }
    agent_audit_event("net.fetch", url, "ok");
    return 0;
}

int agent_tool_net_ping(const char *host, char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("net.ping")) {
        agent_audit_event("net.ping", host ? host : "-", "deny-tool");
        agent_tool_note_deny("net.ping", NULL);
        return -1;
    }
    if (!privacy_net_client_allowed()) {
        agent_audit_event("net.ping", host ? host : "-", "deny-privacy");
        agent_transcript_note_tool("deny-privacy: net client grant required");
        return -1;
    }
    if (!host || !host[0] || !out || out_len < 32) {
        agent_audit_event("net.ping", "-", "fail");
        return -1;
    }
    if (!net_ready()) {
        snprintf(out, out_len, "network down");
        agent_audit_event("net.ping", host, "down");
        return -1;
    }
    uint64_t t0 = timer_ticks();
    uint32_t ip = net_dns_resolve(host, 300);
    if (!ip) {
        const char *why = net_last_error();
        snprintf(out, out_len, "DNS failed%s%s", why && why[0] ? ": " : "", why ? why : "");
        agent_audit_event("net.ping", host, "dns-fail");
        return -1;
    }
    char ipbuf[32];
    net_format_ip(ip, ipbuf, sizeof(ipbuf));
    int cr = net_tcp_connect(ip, 80, 300);
    uint64_t dt = timer_ticks() - t0;
    if (cr == 0) {
        net_tcp_close();
        snprintf(out, out_len, "PING %s (%s) tcp/:80 open time=%lums",
                 host, ipbuf, (unsigned long)(dt * 10));
        agent_audit_event("net.ping", host, "ok");
        return 0;
    }
    const char *why = net_last_error();
    snprintf(out, out_len, "PING %s (%s) tcp/:80 failed%s%s time=%lums",
             host, ipbuf, why && why[0] ? ": " : "", why ? why : "",
             (unsigned long)(dt * 10));
    agent_audit_event("net.ping", host, "fail");
    return -1;
}

int agent_tool_mem_recall(const char *goal, char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("mem.recall")) {
        agent_audit_event("mem.recall", goal ? goal : "-", "deny-tool");
        agent_tool_note_deny("mem.recall", NULL);
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
        int n = peakvec_query("ws", q, 3, hits);
        if (n > 0) {
            char hdr[48];
            snprintf(hdr, sizeof(hdr), "[recall/workspace] %d hit%s\n", n, n == 1 ? "" : "s");
            for (const char *p = hdr; *p && o + 1 < out_len; p++)
                out[o++] = *p;
            for (int i = 0; i < n; i++) {
                if (!hits[i].key[0])
                    continue;
                char line[160];
                int score = hits[i].score_milli;
                snprintf(line, sizeof(line), "  %d.%03d  %s: ",
                         score / 1000, score % 1000, hits[i].key);
                for (const char *p = line; *p && o + 1 < out_len; p++)
                    out[o++] = *p;
                size_t shown = 0;
                for (const char *p = hits[i].meta; *p && o + 1 < out_len; p++, shown++) {
                    if (shown >= 72) {
                        if (o + 4 < out_len) { out[o++]='.'; out[o++]='.'; out[o++]='.'; }
                        break;
                    }
                    out[o++] = *p;
                }
                if (o + 1 < out_len)
                    out[o++] = '\n';
            }
        }
    }

    {
        int16_t q[PEAKVEC_DIM];
        peakvec_embed_text(goal ? goal : "", q);
        struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
        int n = peakvec_query("agent", q, 3, hits);
        if (n > 0) {
            char hdr[48];
            snprintf(hdr, sizeof(hdr), "[recall/PeakVec] %d hit%s\n", n, n == 1 ? "" : "s");
            for (const char *p = hdr; *p && o + 1 < out_len; p++)
                out[o++] = *p;
            for (int i = 0; i < n; i++) {
                if (!hits[i].key[0])
                    continue;
                char line[160];
                int score = hits[i].score_milli;
                snprintf(line, sizeof(line), "  %d.%03d  %s: ",
                         score / 1000, score % 1000, hits[i].key);
                for (const char *p = line; *p && o + 1 < out_len; p++)
                    out[o++] = *p;
                size_t shown = 0;
                for (const char *p = hits[i].meta; *p && o + 1 < out_len; p++, shown++) {
                    if (shown >= 72) {
                        if (o + 4 < out_len) { out[o++]='.'; out[o++]='.'; out[o++]='.'; }
                        break;
                    }
                    out[o++] = *p;
                }
                if (o + 1 < out_len)
                    out[o++] = '\n';
            }
        }
    }

    char mem[AGENT_MEMORY_TAIL_MAX];
    size_t n = 0;
    if (vfs_read_file(AGENT_MEM_PATH, mem, sizeof(mem) - 1, &n) == 0 && n > 0) {
        const char *hdr = "[recall/memory]\n";
        for (const char *p = hdr; *p && o + 1 < out_len; p++)
            out[o++] = *p;
        char formatted[512];
        format_session_tail(mem, n, formatted, sizeof(formatted));
        for (const char *p = formatted; *p && o + 1 < out_len; p++)
            out[o++] = *p;
    }
    out[o] = '\0';
    agent_audit_event("mem.recall", goal ? goal : "-", o ? "ok" : "empty");
    return o ? 0 : -1;
}

int agent_tool_mem_store(const char *text) {
    if (!agent_policy_tool_allowed("mem.store")) {
        agent_audit_event("mem.store", "-", "deny-tool");
        agent_tool_note_deny("mem.store", NULL);
        return -1;
    }
    if (!text || !text[0]) {
        agent_audit_event("mem.store", "-", "empty");
        return -1;
    }
    char existing[AGENT_MEMORY_TAIL_MAX];
    size_t n = 0;
    if (vfs_read_file(AGENT_MEM_PATH, existing, sizeof(existing) - 1, &n) != 0)
        n = 0;
    existing[n] = '\0';
    size_t add = strlen(text);
    if (n + add + 2 >= sizeof(existing)) {
        size_t keep = sizeof(existing) / 2;
        if (n > keep) {
            memmove(existing, existing + (n - keep), keep);
            n = keep;
            existing[n] = '\0';
        }
    }
    if (n + add + 2 < sizeof(existing)) {
        if (n)
            existing[n++] = '\n';
        memcpy(existing + n, text, add);
        n += add;
        existing[n] = '\0';
        if (vfs_write_file(AGENT_MEM_PATH, existing, n) != 0) {
            agent_audit_event("mem.store", "-", "fail");
            return -1;
        }
    }
    int16_t q[PEAKVEC_DIM];
    peakvec_embed_text(text, q);
    (void)peakvec_upsert("agent", "session", q, text);
    agent_audit_event("mem.store", "-", "ok");
    return 0;
}

int agent_tool_peakvec_query(const char *ns, const char *query, char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("peakvec.query")) {
        agent_audit_event("peakvec.query", query ? query : "-", "deny-tool");
        agent_tool_note_deny("peakvec.query", NULL);
        return -1;
    }
    if (!out || out_len < 8)
        return -1;
    out[0] = '\0';
    int16_t q[PEAKVEC_DIM];
    peakvec_embed_text(query ? query : "", q);
    struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
    int n = peakvec_query(ns && ns[0] ? ns : "agent", q, 5, hits);
    if (n <= 0) {
        agent_audit_event("peakvec.query", query ? query : "-", "empty");
        return -1;
    }
    size_t o = 0;
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "[peakvec.query] %d hit%s\n", n, n == 1 ? "" : "s");
    for (const char *p = hdr; *p && o + 1 < out_len; p++)
        out[o++] = *p;
    for (int i = 0; i < n; i++) {
        if (!hits[i].key[0])
            continue;
        char line[160];
        snprintf(line, sizeof(line), "  %d.%03d  %s: ",
                 hits[i].score_milli / 1000, hits[i].score_milli % 1000, hits[i].key);
        for (const char *p = line; *p && o + 1 < out_len; p++)
            out[o++] = *p;
        size_t shown = 0;
        for (const char *p = hits[i].meta; *p && o + 1 < out_len; p++, shown++) {
            if (shown >= 72) {
                if (o + 4 < out_len) {
                    out[o++] = '.';
                    out[o++] = '.';
                    out[o++] = '.';
                }
                break;
            }
            out[o++] = *p;
        }
        if (o + 1 < out_len)
            out[o++] = '\n';
    }
    out[o] = '\0';
    agent_audit_event("peakvec.query", query ? query : "-", "ok");
    return 0;
}


static int tree_depth(const char *root, const char *path) {
    size_t rl = strlen(root);
    if (strncmp(path, root, rl) != 0)
        return 99;
    const char *p = path + rl;
    if (*p == '/')
        p++;
    int d = 0;
    for (; *p; p++)
        if (*p == '/')
            d++;
    return d;
}

struct tree_ctx {
    char *out;
    size_t cap;
    size_t o;
    char root[VFS_PATH_MAX];
    int maxdepth;
    int err;
};

static int tree_walk_cb(const char *path, struct vfs_node *node, void *v) {
    struct tree_ctx *c = v;
    if (!path || !node || c->err)
        return 0;
    int d = tree_depth(c->root, path);
    if (d > c->maxdepth)
        return 0;
    size_t indent = (size_t)d * 2;
    size_t plen = strlen(path);
    if (c->o + indent + plen + 4 >= c->cap) {
        c->err = 1;
        return 1;
    }
    for (size_t i = 0; i < indent; i++)
        c->out[c->o++] = ' ';
    memcpy(c->out + c->o, path, plen);
    c->o += plen;
    if (node->type == VFS_DIR)
        c->out[c->o++] = '/';
    c->out[c->o++] = '\n';
    return 0;
}

int agent_tool_fs_tree(const char *path, char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("fs.tree")) {
        agent_audit_event("fs.tree", path ? path : "-", "deny-tool");
        agent_tool_note_deny("fs.tree", path);
        return -1;
    }
    if (!out || out_len < 16)
        return -1;
    char norm[VFS_PATH_MAX];
    if (!path || !path[0])
        path = "/home/dev/workspace";
    if (vfs_normalize(path, norm, sizeof(norm)) != 0 ||
        !agent_policy_path_allowed(norm)) {
        agent_audit_event("fs.tree", path, "deny-path");
        agent_tool_note_deny("fs.tree", path);
        return -1;
    }
    struct tree_ctx c;
    memset(&c, 0, sizeof(c));
    c.out = out;
    c.cap = out_len;
    snprintf(c.root, sizeof(c.root), "%s", norm);
    c.maxdepth = 4;
    out[0] = '\0';
    vfs_walk(norm, tree_walk_cb, &c);
    if (c.o < out_len)
        out[c.o] = '\0';
    else
        out[out_len - 1] = '\0';
    agent_audit_event("fs.tree", norm, c.err ? "truncated" : "ok");
    return 0;
}

int agent_tool_sys_ps(char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("sys.ps")) {
        agent_audit_event("sys.ps", "-", "deny-tool");
        agent_tool_note_deny("sys.ps", NULL);
        return -1;
    }
    if (!out || out_len < 32)
        return -1;
    struct task list[MAX_TASKS];
    int n = sched_list_tasks(list, MAX_TASKS);
    sched_sort_tasks(list, n);
    size_t o = 0;
    const char *hdr = "PID STATE TICKS STARV NAME\n";
    for (const char *p = hdr; *p && o + 1 < out_len; p++)
        out[o++] = *p;
    for (int i = 0; i < n && o + 40 < out_len; i++) {
        const char *st = list[i].state == TASK_RUNNING ? "run" :
                         list[i].state == TASK_READY ? "ready" :
                         list[i].state == TASK_BLOCKED ? "block" : "zombie";
        char line[96];
        snprintf(line, sizeof(line), "%d %s %lu %u %s\n", list[i].pid, st,
                 (unsigned long)list[i].cpu_ticks,
                 (unsigned)sched_task_starvation(sched_slot_for_pid(list[i].pid)),
                 list[i].name);
        for (const char *p = line; *p && o + 1 < out_len; p++)
            out[o++] = *p;
    }
    out[o] = '\0';
    agent_audit_event("sys.ps", "-", "ok");
    return 0;
}


int agent_tool_audit_tail(char *out, size_t out_len) {
    if (!agent_policy_tool_allowed("audit.tail")) {
        agent_audit_event("audit.tail", "-", "deny-tool");
        agent_tool_note_deny("audit.tail", NULL);
        return -1;
    }
    if (!out || out_len < 8)
        return -1;
    out[0] = '\0';
    size_t file_sz = 0;
    struct vfs_stat st;
    if (vfs_stat(AGENT_AUDIT_PATH, &st) == 0 && st.type == VFS_FILE)
        file_sz = st.size;
    size_t want = file_sz > 400 ? 400 : file_sz;
    if (file_sz == 0) {
        agent_audit_event("audit.tail", "-", "empty");
        return -1;
    }
    size_t off = file_sz > want ? file_sz - want : 0;
    size_t n = 0;
    char raw[512];
    if (vfs_read_at(AGENT_AUDIT_PATH, off, raw, want, &n) != 0 || !n) {
        agent_audit_event("audit.tail", "-", "fail");
        return -1;
    }
    format_session_tail(raw, n, out, out_len);
    agent_audit_event("audit.tail", "-", "ok");
    return 0;
}
