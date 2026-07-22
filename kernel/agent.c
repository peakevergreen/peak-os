#include "agent.h"
#include "vfs.h"
#include "console.h"
#include "heap.h"
#include "util.h"
#include "fb.h"
#include "peakvec.h"

#define AUDIT_PATH "/var/peak/audit.log"
#define MEM_PATH "/var/peak/sessions/memory.txt"
#define POLICY_PATH "/etc/peak/agent.policy"

#define ALLOW_PATHS_MAX 8
#define ALLOW_PATH_LEN 96
#define PENDING_CONTENT_MAX 512
#define TOOL_NAME_MAX 32
#define TOOLS_MAX 8
#define READ_CONTENT_MAX 1024
#define MEMORY_TAIL_MAX 768

static char last_summary[256];
static int pending;

static char allow_paths[ALLOW_PATHS_MAX][ALLOW_PATH_LEN];
static int allow_path_count;
static int require_write_approval = 1;
static char allow_tools[TOOLS_MAX][TOOL_NAME_MAX];
static int allow_tool_count;
static char deny_tools[TOOLS_MAX][TOOL_NAME_MAX];
static int deny_tool_count;
static int audit_wiped_once;

/* GUI approval queue for fs.write */
static int write_wait;
static char write_path[VFS_PATH_MAX];
static char write_content[PENDING_CONTENT_MAX];
static int write_approved; /* -1 denied, 0 waiting, 1 approved */

static void audit_append(const char *line) {
    char existing[2048];
    size_t n = 0;
    vfs_read_file(AUDIT_PATH, existing, sizeof(existing) - 1, &n);
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
        vfs_write_file(AUDIT_PATH, existing, n);
    }
}

static void audit_event(const char *op, const char *target, const char *decision) {
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
    audit_append(line);
}

static int tool_listed(const char names[][TOOL_NAME_MAX], int count, const char *tool) {
    for (int i = 0; i < count; i++) {
        if (!strcmp(names[i], tool))
            return 1;
    }
    return 0;
}

static int tool_allowed(const char *tool) {
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

static int normalize_agent_path(const char *in, char *out, size_t out_len) {
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

static void policy_load_defaults(void) {
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

static void policy_reload(void) {
    policy_load_defaults();
    char buf[512];
    size_t n = 0;
    if (vfs_read_file(POLICY_PATH, buf, sizeof(buf) - 1, &n) != 0)
        return;
    buf[n] = '\0';
    const char *p = buf;
    while (*p) {
        if (!strncmp(p, "allow_paths=", 12)) {
            p += 12;
            allow_path_count = 0;
            while (*p && *p != '\n' && allow_path_count < ALLOW_PATHS_MAX) {
                while (*p == ',')
                    p++;
                size_t i = 0;
                while (*p && *p != ',' && *p != '\n' && i + 1 < ALLOW_PATH_LEN)
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
                char name[TOOL_NAME_MAX];
                size_t i = 0;
                while (*p && *p != ',' && *p != '\n' && i + 1 < sizeof(name))
                    name[i++] = *p++;
                name[i] = '\0';
                if (i) {
                    if (is_deny && deny_tool_count < TOOLS_MAX) {
                        memcpy(deny_tools[deny_tool_count], name, i + 1);
                        deny_tool_count++;
                    } else if (!is_deny && allow_tool_count < TOOLS_MAX) {
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
        policy_load_defaults();
}

static int path_allowed(const char *path) {
    char norm[VFS_PATH_MAX];
    if (normalize_agent_path(path, norm, sizeof(norm)) != 0)
        return 0;
    for (int i = 0; i < allow_path_count; i++) {
        if (path_under_prefix(norm, allow_paths[i]))
            return 1;
    }
    return 0;
}

static int queue_write_approval(const char *path, const char *content) {
    if (write_wait)
        return -1;
    size_t cl = strlen(content);
    if (cl >= PENDING_CONTENT_MAX)
        return -1;
    size_t pl = strlen(path);
    if (pl >= sizeof(write_path))
        return -1;
    memcpy(write_path, path, pl + 1);
    memcpy(write_content, content, cl + 1);
    write_wait = 1;
    write_approved = 0;
    pending++;
    console_printf("[agent] approval required: fs.write %s (Y/N in Agent)\n", path);
    return 0;
}

static int tool_console_print(const char *msg) {
    if (!tool_allowed("console.print")) {
        audit_event("console.print", "-", "deny-tool");
        return -1;
    }
    console_write(msg ? msg : "");
    if (msg && msg[0] && msg[strlen(msg) - 1] != '\n')
        console_write("\n");
    audit_event("console.print", "-", "ok");
    return 0;
}

static int tool_fs_read(const char *path, char *out, size_t out_len, size_t *out_n) {
    char norm[VFS_PATH_MAX];
    if (!tool_allowed("fs.read")) {
        audit_event("fs.read", path, "deny-tool");
        return -1;
    }
    if (normalize_agent_path(path, norm, sizeof(norm)) != 0 || !path_allowed(norm)) {
        audit_event("fs.read", path, "deny-path");
        return -1;
    }
    size_t n = 0;
    size_t cap = out_len ? out_len - 1 : 0;
    if (cap > READ_CONTENT_MAX)
        cap = READ_CONTENT_MAX;
    if (vfs_read_file(norm, out, cap, &n) != 0) {
        audit_event("fs.read", norm, "fail");
        return -1;
    }
    if (out_len)
        out[n < out_len ? n : out_len - 1] = '\0';
    if (out_n)
        *out_n = n;
    audit_event("fs.read", norm, "ok");
    return 0;
}

static int tool_fs_write(const char *path, const char *content, int auto_ok) {
    char norm[VFS_PATH_MAX];
    if (!tool_allowed("fs.write")) {
        audit_event("fs.write", path, "deny-tool");
        return -1;
    }
    if (normalize_agent_path(path, norm, sizeof(norm)) != 0 || !path_allowed(norm)) {
        audit_event("fs.write", path, "deny-path");
        return -1;
    }
    if (!strcmp(norm, AUDIT_PATH)) {
        audit_event("fs.write", norm, "deny-audit");
        return -1;
    }
    if (require_write_approval && !auto_ok) {
        if (queue_write_approval(norm, content) != 0)
            return -1;
        audit_event("fs.write", norm, "pending");
        return 1;
    }
    if (vfs_write_file(norm, content, strlen(content)) != 0)
        return -1;
    audit_event("fs.write", norm, "ok");
    return 0;
}

static int tool_fs_list(const char *path, char *out, size_t out_len) {
    char norm[VFS_PATH_MAX];
    if (!tool_allowed("fs.list")) {
        audit_event("fs.list", path, "deny-tool");
        return -1;
    }
    if (normalize_agent_path(path, norm, sizeof(norm)) != 0)
        return -1;
    if (!path_allowed(norm) && strcmp(norm, "/home/dev") != 0) {
        audit_event("fs.list", norm, "deny-path");
        return -1;
    }
    int r = vfs_list(norm, out, out_len);
    audit_event("fs.list", norm, r == 0 ? "ok" : "fail");
    return r;
}

static void memory_append_turn(const char *goal, const char *tools, const char *path) {
    char note[256];
    size_t o = 0;
    const char *prefix = "turn|goal=";
    for (const char *p = prefix; *p && o + 1 < sizeof(note); p++)
        note[o++] = *p;
    for (const char *p = goal; p && *p && o + 1 < sizeof(note); p++) {
        char c = *p;
        if (c == '\n' || c == '|')
            c = ' ';
        note[o++] = c;
    }
    if (tools && o + 8 < sizeof(note)) {
        note[o++] = '|';
        note[o++] = 't';
        note[o++] = '=';
        for (const char *p = tools; *p && o + 1 < sizeof(note); p++)
            note[o++] = *p;
    }
    if (path && o + 8 < sizeof(note)) {
        note[o++] = '|';
        note[o++] = 'p';
        note[o++] = '=';
        for (const char *p = path; *p && o + 1 < sizeof(note); p++)
            note[o++] = *p;
    }
    note[o] = '\0';

    char buf[2048];
    size_t n = 0;
    vfs_read_file(MEM_PATH, buf, sizeof(buf) - 1, &n);
    buf[n] = '\0';
    size_t add = strlen(note);
    if (n + add + 2 >= sizeof(buf)) {
        size_t keep = sizeof(buf) / 2;
        if (n > keep)
            memmove(buf, buf + (n - keep), keep + 1), n = keep;
    }
    if (n + add + 2 < sizeof(buf)) {
        memcpy(buf + n, note, add);
        n += add;
        buf[n++] = '\n';
        buf[n] = '\0';
        vfs_write_file(MEM_PATH, buf, n);
    }

    /* Index the turn in PeakVec for semantic recall. */
    {
        char key[PEAKVEC_KEY_MAX];
        size_t ki = 0;
        const char *kp = "mem:";
        while (*kp && ki + 1 < sizeof(key))
            key[ki++] = *kp++;
        /* Use a short hash of goal for the key. */
        uint32_t h = 5381u;
        for (const char *p = goal; p && *p; p++)
            h = ((h << 5) + h) + (unsigned char)*p;
        char hex[12];
        itoa_u(h, hex, 16);
        for (size_t i = 0; hex[i] && ki + 1 < sizeof(key); i++)
            key[ki++] = hex[i];
        key[ki] = '\0';
        int16_t vec[PEAKVEC_DIM];
        peakvec_embed_text(goal, vec);
        (void)peakvec_upsert("agent", key, vec, note);
    }
}

static void memory_recall(const char *goal, char *out, size_t out_len) {
    if (!out || out_len < 8)
        return;
    out[0] = '\0';
    size_t o = 0;

    /* Semantic hits from PeakVec. */
    {
        int16_t q[PEAKVEC_DIM];
        peakvec_embed_text(goal, q);
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

    /* Tail of structured memory file. */
    char mem[MEMORY_TAIL_MAX];
    size_t n = 0;
    if (vfs_read_file(MEM_PATH, mem, sizeof(mem) - 1, &n) == 0 && n > 0) {
        mem[n] = '\0';
        const char *hdr = "[recall/memory]\n";
        for (const char *p = hdr; *p && o + 1 < out_len; p++)
            out[o++] = *p;
        /* Prefer last ~400 bytes. */
        size_t start = 0;
        if (n > 400)
            start = n - 400;
        for (size_t i = start; i < n && o + 1 < out_len; i++)
            out[o++] = mem[i];
    }
    out[o] = '\0';
}

static int contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0])
        return 0;
    for (const char *p = hay; *p; p++) {
        const char *a = p, *b = needle;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z')
                ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z')
                cb = (char)(cb - 'A' + 'a');
            if (ca != cb)
                break;
            a++;
            b++;
        }
        if (!*b)
            return 1;
    }
    return 0;
}

static int extract_filename(const char *goal, char *name, size_t name_len) {
    const char *p = goal;
    while (*p) {
        if (!strncmp(p, ".c", 2) || !strncmp(p, ".md", 3) || !strncmp(p, ".txt", 4) ||
            !strncmp(p, ".h", 2) || !strncmp(p, ".js", 3)) {
            const char *s = p;
            while (s > goal && s[-1] != ' ' && s[-1] != '"' && s[-1] != '/')
                s--;
            size_t i = 0;
            while (s <= p + 4 && *s && *s != ' ' && i + 1 < name_len)
                name[i++] = *s++;
            name[i] = '\0';
            return i > 0;
        }
        p++;
    }
    return 0;
}

enum agent_intent {
    INTENT_CREATE = 1,
    INTENT_EDIT,
    INTENT_SUMMARIZE,
    INTENT_RECALL,
    INTENT_AUDIT,
    INTENT_READ,
    INTENT_HELP,
};

static enum agent_intent classify_intent(const char *goal) {
    if (contains_ci(goal, "what did") || contains_ci(goal, "last ask") ||
        contains_ci(goal, "remember") || contains_ci(goal, "recall") ||
        contains_ci(goal, "memory"))
        return INTENT_RECALL;
    if (contains_ci(goal, "audit"))
        return INTENT_AUDIT;
    if (contains_ci(goal, "summar") || contains_ci(goal, "list workspace") ||
        contains_ci(goal, "what's in") || contains_ci(goal, "whats in"))
        return INTENT_SUMMARIZE;
    if (contains_ci(goal, "read ") || contains_ci(goal, "show ") ||
        contains_ci(goal, "cat "))
        return INTENT_READ;
    if (contains_ci(goal, "edit ") || contains_ci(goal, "update ") ||
        contains_ci(goal, "modify "))
        return INTENT_EDIT;
    if (contains_ci(goal, "create") || contains_ci(goal, "write ") ||
        contains_ci(goal, "make "))
        return INTENT_CREATE;
    {
        char tmp[64];
        if (extract_filename(goal, tmp, sizeof(tmp)))
            return INTENT_CREATE;
    }
    if (contains_ci(goal, "help"))
        return INTENT_HELP;
    return INTENT_CREATE; /* default: try to produce a workspace artifact */
}

static void set_summary(const char *s) {
    size_t i = 0;
    for (; s && s[i] && i + 1 < sizeof(last_summary); i++)
        last_summary[i] = s[i];
    last_summary[i] = '\0';
}

static void plan_handle_goal(const char *goal) {
    console_write("[agent] planner\n");
    console_printf("[agent] goal: %s\n", goal);

    char recall[512];
    memory_recall(goal, recall, sizeof(recall));
    if (recall[0]) {
        tool_console_print("[agent] context from memory/PeakVec:");
        tool_console_print(recall);
    }

    enum agent_intent intent = classify_intent(goal);
    char tools_used[64] = "";
    char path_used[VFS_PATH_MAX] = "";
    size_t tu = 0;

    #define TOOL_NOTE(name) do { \
        if (tu + 12 < sizeof(tools_used)) { \
            if (tu) tools_used[tu++] = ','; \
            for (const char *_t = name; *_t && tu + 1 < sizeof(tools_used); _t++) \
                tools_used[tu++] = *_t; \
            tools_used[tu] = '\0'; \
        } \
    } while (0)

    if (intent == INTENT_HELP) {
        tool_console_print(
            "Peak Agent: create/edit files, summarize workspace, recall memory, show audit.");
        TOOL_NOTE("console.print");
        set_summary("help");
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_RECALL) {
        if (!recall[0])
            tool_console_print("[agent] no prior memory yet");
        TOOL_NOTE("console.print");
        set_summary("recalled session memory");
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_AUDIT) {
        char audit[READ_CONTENT_MAX];
        size_t n = 0;
        /* Audit is outside allow_paths — read via VFS for explain-only. */
        if (vfs_read_file(AUDIT_PATH, audit, sizeof(audit) - 1, &n) == 0 && n) {
            audit[n] = '\0';
            tool_console_print("[agent] audit tail:");
            /* print last 400 chars */
            size_t start = n > 400 ? n - 400 : 0;
            tool_console_print(audit + start);
        } else {
            tool_console_print("[agent] audit empty");
        }
        TOOL_NOTE("console.print");
        set_summary("showed audit");
        memory_append_turn(goal, tools_used, AUDIT_PATH);
        return;
    }

    if (intent == INTENT_SUMMARIZE) {
        char listing[512];
        if (tool_fs_list("/home/dev/workspace", listing, sizeof(listing)) == 0) {
            TOOL_NOTE("fs.list");
            tool_console_print("[agent] workspace:");
            TOOL_NOTE("console.print");
            tool_console_print(listing);
        } else {
            tool_console_print("[agent] could not list workspace");
            TOOL_NOTE("console.print");
        }
        set_summary("summarized workspace");
        memory_append_turn(goal, tools_used, "/home/dev/workspace");
        return;
    }

    if (intent == INTENT_READ) {
        char name[64];
        char path[VFS_PATH_MAX] = "/home/dev/workspace/README.md";
        if (extract_filename(goal, name, sizeof(name))) {
            memcpy(path, "/home/dev/workspace/", 20);
            size_t j = 20;
            for (size_t i = 0; name[i] && j + 1 < sizeof(path); i++)
                path[j++] = name[i];
            path[j] = '\0';
        }
        char body[READ_CONTENT_MAX];
        size_t n = 0;
        if (tool_fs_read(path, body, sizeof(body), &n) == 0) {
            TOOL_NOTE("fs.read");
            tool_console_print(path);
            TOOL_NOTE("console.print");
            tool_console_print(body);
            memcpy(path_used, path, strlen(path) + 1);
            set_summary("read file");
        } else {
            tool_console_print("[agent] read failed");
            TOOL_NOTE("console.print");
            set_summary("read failed");
        }
        memory_append_turn(goal, tools_used, path_used[0] ? path_used : NULL);
        return;
    }

    /* CREATE or EDIT — multi-step: list → optional read → write */
    {
        char listing[512];
        if (tool_fs_list("/home/dev/workspace", listing, sizeof(listing)) == 0) {
            TOOL_NOTE("fs.list");
            console_write("[agent] workspace:\n");
            console_write(listing);
        }

        char name[64];
        char path[VFS_PATH_MAX] = "/home/dev/workspace/agent_out.txt";
        if (extract_filename(goal, name, sizeof(name))) {
            memcpy(path, "/home/dev/workspace/", 20);
            size_t j = 20;
            for (size_t i = 0; name[i] && j + 1 < sizeof(path); i++)
                path[j++] = name[i];
            path[j] = '\0';
        }

        char content[PENDING_CONTENT_MAX];
        memset(content, 0, sizeof(content));
        size_t o = 0;
        int is_c = 0;
        for (const char *q = path; *q; q++) {
            if (q[0] == '.' && q[1] == 'c' && q[2] == '\0')
                is_c = 1;
        }

        if (intent == INTENT_EDIT) {
            char existing[READ_CONTENT_MAX];
            size_t n = 0;
            if (tool_fs_read(path, existing, sizeof(existing), &n) == 0 && n) {
                TOOL_NOTE("fs.read");
                /* Append an edit marker + goal note. */
                for (size_t i = 0; i < n && o + 1 < sizeof(content); i++)
                    content[o++] = existing[i];
                const char *mark = "\n/* peak-agent edit: ";
                for (const char *q = mark; *q && o + 1 < sizeof(content); q++)
                    content[o++] = *q;
                for (const char *q = goal; *q && o + 4 < sizeof(content); q++)
                    content[o++] = *q;
                content[o++] = '*';
                content[o++] = '/';
                content[o++] = '\n';
                content[o] = '\0';
            } else {
                intent = INTENT_CREATE;
            }
        }

        if (intent == INTENT_CREATE || content[0] == '\0') {
            o = 0;
            if (is_c) {
                const char *prefix = "/* generated by peak-agent */\n/* goal: ";
                for (const char *q = prefix; *q; q++)
                    content[o++] = *q;
                for (const char *q = goal; *q && o + 8 < sizeof(content); q++)
                    content[o++] = *q;
                const char *suffix = " */\nint main(void) { return 0; }\n";
                for (const char *q = suffix; *q && o + 1 < sizeof(content); q++)
                    content[o++] = *q;
            } else {
                const char *prefix = "# peak-agent\n\ngoal: ";
                for (const char *q = prefix; *q; q++)
                    content[o++] = *q;
                for (const char *q = goal; *q && o + 2 < sizeof(content); q++)
                    content[o++] = *q;
                content[o++] = '\n';
            }
            content[o] = '\0';
        }

        console_printf("[agent] tool fs.write %s\n", path);
        int wr = tool_fs_write(path, content, 0);
        TOOL_NOTE("fs.write");
        memcpy(path_used, path, strlen(path) + 1);
        if (wr == 0) {
            tool_console_print("[agent] wrote file");
            TOOL_NOTE("console.print");
            set_summary("wrote file");
        } else if (wr == 1) {
            tool_console_print("[agent] write waiting for GUI approval");
            TOOL_NOTE("console.print");
            set_summary("write pending approval");
        } else {
            tool_console_print("[agent] write denied/failed");
            TOOL_NOTE("console.print");
            set_summary("write failed");
        }
        memory_append_turn(goal, tools_used, path_used);
        audit_append("goal complete");
    }
    #undef TOOL_NOTE
}

void agent_init(void) {
    last_summary[0] = '\0';
    pending = 0;
    write_wait = 0;
    write_approved = 0;
    policy_reload();
    if (!audit_wiped_once) {
        audit_append("session|boot|start|ok");
        audit_wiped_once = 1;
    }
    {
        char tmp[8];
        size_t n = 0;
        if (vfs_read_file(MEM_PATH, tmp, sizeof(tmp), &n) != 0 || n == 0)
            vfs_write_file(MEM_PATH, "# Peak project memory\n", 22);
    }
}

void agent_ask(const char *goal) {
    policy_reload();
    plan_handle_goal(goal);
}

int64_t agent_syscall(uint64_t op, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a3;
    if (op == 1) {
        agent_ask((const char *)a1);
        return 0;
    }
    if (op == 2) {
        const char *tools = "fs.read,fs.write,fs.list,console.print";
        size_t n = strlen(tools);
        if (n > a2)
            n = a2;
        memcpy((void *)a1, tools, n);
        return (int64_t)n;
    }
    return -1;
}

const char *agent_last_summary(void) {
    return last_summary;
}

int agent_pending_approvals(void) {
    return pending;
}

int agent_write_pending(void) {
    return write_wait;
}

const char *agent_pending_write_path(void) {
    return write_wait ? write_path : "";
}

void agent_approve_write(int yes) {
    if (!write_wait)
        return;
    if (yes) {
        if (vfs_write_file(write_path, write_content, strlen(write_content)) == 0)
            audit_event("fs.write", write_path, "approved");
        else
            audit_event("fs.write", write_path, "approve-fail");
    } else {
        audit_event("fs.write", write_path, "denied");
    }
    write_wait = 0;
    write_approved = yes ? 1 : -1;
    if (pending > 0)
        pending--;
    write_path[0] = '\0';
    memset(write_content, 0, sizeof(write_content));
}

void agent_gui_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t bg = fb_rgb(0x0E, 0x1F, 0x16);
    uint32_t fg = fb_rgb(0xE8, 0xF0, 0xEA);
    uint32_t acc = fb_rgb(0x3D, 0xA3, 0x6A);
    uint32_t s = fb_ui_scale();
    uint32_t ch = fb_char_h();
    fb_fill_rect(x, y, w, h, bg);
    fb_fill_rect(x, y, w, 2 * s, acc);
    fb_draw_string(x + 8 * s, y + 8 * s, "Peak Agent", fg, bg);
    fb_draw_string(x + 8 * s, y + 8 * s + ch + 4 * s, "mode: local+PeakVec", fg, bg);
    fb_draw_string(x + 8 * s, y + 8 * s + 2 * (ch + 4 * s),
                   last_summary[0] ? last_summary : "(no session)", fg, bg);
    char pend[48] = "pending: ";
    char num[8];
    itoa_u((uint64_t)pending, num, 10);
    size_t i = 9;
    for (size_t j = 0; num[j] && i + 1 < sizeof(pend); j++)
        pend[i++] = num[j];
    pend[i] = '\0';
    fb_draw_string(x + 8 * s, y + 8 * s + 3 * (ch + 4 * s), pend, fg, bg);
    if (write_wait) {
        fb_draw_string(x + 8 * s, y + 8 * s + 4 * (ch + 4 * s),
                       "Approve write? Y/N", acc, bg);
        fb_draw_string(x + 8 * s, y + 8 * s + 5 * (ch + 4 * s), write_path, fg, bg);
    }
}
