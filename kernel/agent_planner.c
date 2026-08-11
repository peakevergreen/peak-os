#include "agent_internal.h"
#include "console.h"
#include "serial.h"
#include "vfs.h"
#include "peakvec.h"
#include "util.h"

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
    vfs_read_file(AGENT_MEM_PATH, buf, sizeof(buf) - 1, &n);
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
        vfs_write_file(AGENT_MEM_PATH, buf, n);
    }

    {
        char key[PEAKVEC_KEY_MAX];
        size_t ki = 0;
        const char *kp = "mem:";
        while (*kp && ki + 1 < sizeof(key))
            key[ki++] = *kp++;
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
    if (agent_tool_mem_recall(goal, out, out_len) != 0 && out && out_len)
        out[0] = '\0';
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
    INTENT_UNKNOWN = 0,
    INTENT_CREATE,
    INTENT_EDIT,
    INTENT_SUMMARIZE,
    INTENT_SEARCH,
    INTENT_RECALL,
    INTENT_AUDIT,
    INTENT_READ,
    INTENT_SYSINFO,
    INTENT_PING,
    INTENT_FETCH,
    INTENT_DIFF,
    INTENT_TREE,
    INTENT_PS,
    INTENT_HELP,
    INTENT_PEAKVEC,
    INTENT_STORE,
    INTENT_RUN,
};

struct agent_slots {
    enum agent_intent intent;
    char arg[160];      /* needle, host, url, memory text, cmdline, query */
    char path[VFS_PATH_MAX];
    char path_b[VFS_PATH_MAX];
    int use_grep;       /* INTENT_SEARCH: prefer fs.grep */
};

/* Phrase table — first match wins. Longer / more specific phrases first. */
static const struct {
    const char *phrase;
    enum agent_intent intent;
} intent_table[] = {
    { "what did", INTENT_RECALL },
    { "last ask", INTENT_RECALL },
    { "store memory", INTENT_STORE },
    { "vector query", INTENT_PEAKVEC },
    { "system info", INTENT_SYSINFO },
    { "file tree", INTENT_TREE },
    { "list workspace", INTENT_SUMMARIZE },
    { "what's in", INTENT_SUMMARIZE },
    { "whats in", INTENT_SUMMARIZE },
    { "remember ", INTENT_STORE },
    { "peakvec ", INTENT_PEAKVEC },
    { "peakvec", INTENT_PEAKVEC },
    { "summar", INTENT_SUMMARIZE },
    { "memory", INTENT_RECALL },
    { "recall", INTENT_RECALL },
    { "remember", INTENT_RECALL },
    { "audit", INTENT_AUDIT },
    { "fetch ", INTENT_FETCH },
    { "diff ", INTENT_DIFF },
    { "sysinfo", INTENT_SYSINFO },
    { "uptime", INTENT_SYSINFO },
    { "tree ", INTENT_TREE },
    { "ping ", INTENT_PING },
    { "grep ", INTENT_SEARCH },
    { "search ", INTENT_SEARCH },
    { "find ", INTENT_SEARCH },
    { "read ", INTENT_READ },
    { "show ", INTENT_READ },
    { "cat ", INTENT_READ },
    { "edit ", INTENT_EDIT },
    { "update ", INTENT_EDIT },
    { "modify ", INTENT_EDIT },
    { "create", INTENT_CREATE },
    { "write ", INTENT_CREATE },
    { "make ", INTENT_CREATE },
    { "run ", INTENT_RUN },
    { "help", INTENT_HELP },
    { NULL, INTENT_UNKNOWN },
};

static void copy_token(const char *src, char *dst, size_t dst_len) {
    size_t i = 0;
    while (*src == ' ')
        src++;
    for (; *src && *src != ' ' && i + 1 < dst_len; src++)
        dst[i++] = *src;
    dst[i] = '\0';
}

static void copy_rest(const char *src, char *dst, size_t dst_len) {
    size_t i = 0;
    while (*src == ' ')
        src++;
    for (; *src && i + 1 < dst_len; src++)
        dst[i++] = *src;
    dst[i] = '\0';
}

static void workspace_path_from_name(const char *name, char *path, size_t path_len) {
    if (!name || !name[0]) {
        snprintf(path, path_len, "/home/dev/workspace/agent_out.txt");
        return;
    }
    if (name[0] == '/') {
        size_t i = 0;
        for (; name[i] && i + 1 < path_len; i++)
            path[i] = name[i];
        path[i] = '\0';
        return;
    }
    snprintf(path, path_len, "/home/dev/workspace/%s", name);
}

static void parse_slots(const char *goal, struct agent_slots *s) {
    memset(s, 0, sizeof(*s));
    s->intent = INTENT_UNKNOWN;
    if (!goal || !goal[0])
        return;

    /* Bare "ps" */
    if ((goal[0] == 'p' && goal[1] == 's' && (goal[2] == ' ' || goal[2] == '\0')) ||
        contains_ci(goal, " ps")) {
        s->intent = INTENT_PS;
        return;
    }

    for (int i = 0; intent_table[i].phrase; i++) {
        if (!contains_ci(goal, intent_table[i].phrase))
            continue;
        s->intent = intent_table[i].intent;
        if (s->intent == INTENT_SEARCH && contains_ci(goal, "grep "))
            s->use_grep = 1;

        /* Slot-fill arg after the matched phrase when possible. */
        const char *p = goal;
        const char *ph = intent_table[i].phrase;
        size_t plen = strlen(ph);
        /* find phrase case-insensitively */
        for (; *p; p++) {
            size_t k = 0;
            while (ph[k] && p[k]) {
                char a = p[k], b = ph[k];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b)
                    break;
                k++;
            }
            if (k == plen) {
                p += plen;
                break;
            }
        }

        if (s->intent == INTENT_DIFF) {
            copy_token(p, s->path, sizeof(s->path));
            while (*p && *p != ' ')
                p++;
            while (*p == ' ')
                p++;
            copy_token(p, s->path_b, sizeof(s->path_b));
            if (s->path[0] && s->path[0] != '/') {
                char tmp[VFS_PATH_MAX];
                snprintf(tmp, sizeof(tmp), "/home/dev/workspace/%s", s->path);
                memcpy(s->path, tmp, strlen(tmp) + 1);
            }
            if (!s->path_b[0])
                snprintf(s->path_b, sizeof(s->path_b), "/home/dev/workspace/hello.c");
            else if (s->path_b[0] != '/') {
                char tmp[VFS_PATH_MAX];
                snprintf(tmp, sizeof(tmp), "/home/dev/workspace/%s", s->path_b);
                memcpy(s->path_b, tmp, strlen(tmp) + 1);
            }
            if (!s->path[0])
                snprintf(s->path, sizeof(s->path), "/home/dev/workspace/README.md");
        } else if (s->intent == INTENT_FETCH || s->intent == INTENT_PING ||
                   s->intent == INTENT_SEARCH || s->intent == INTENT_RUN ||
                   s->intent == INTENT_STORE || s->intent == INTENT_PEAKVEC ||
                   s->intent == INTENT_SUMMARIZE) {
            if (s->intent == INTENT_STORE || s->intent == INTENT_PEAKVEC ||
                s->intent == INTENT_RUN)
                copy_rest(p, s->arg, sizeof(s->arg));
            else
                copy_token(p, s->arg, sizeof(s->arg));
        } else if (s->intent == INTENT_READ || s->intent == INTENT_EDIT ||
                   s->intent == INTENT_CREATE || s->intent == INTENT_TREE) {
            char name[64];
            if (extract_filename(goal, name, sizeof(name)))
                workspace_path_from_name(name, s->path, sizeof(s->path));
            else if (s->intent == INTENT_TREE)
                snprintf(s->path, sizeof(s->path), "/home/dev/workspace");
            else if (s->intent == INTENT_READ)
                snprintf(s->path, sizeof(s->path), "/home/dev/workspace/README.md");
            else
                workspace_path_from_name(NULL, s->path, sizeof(s->path));
        }
        return;
    }

    /* Filename-only goal → create */
    {
        char name[64];
        if (extract_filename(goal, name, sizeof(name))) {
            s->intent = INTENT_CREATE;
            workspace_path_from_name(name, s->path, sizeof(s->path));
            return;
        }
    }
    s->intent = INTENT_UNKNOWN;
}

static void set_summary(char *summary, size_t summary_cap, const char *s) {
    size_t i = 0;
    for (; s && s[i] && i + 1 < summary_cap; i++)
        summary[i] = s[i];
    summary[i] = '\0';
}

static int path_is_c(const char *path) {
    size_t n = path ? strlen(path) : 0;
    return n >= 2 && path[n - 2] == '.' && path[n - 1] == 'c';
}

static int path_is_md(const char *path) {
    size_t n = path ? strlen(path) : 0;
    return n >= 3 && path[n - 3] == '.' && path[n - 2] == 'm' && path[n - 1] == 'd';
}

static int path_is_txt(const char *path) {
    size_t n = path ? strlen(path) : 0;
    return n >= 4 && path[n - 4] == '.' && path[n - 3] == 't' &&
           path[n - 2] == 'x' && path[n - 1] == 't';
}

/* Returns 1 if a known template was applied, 0 if no match (caller should refuse). */
static int scaffold_create(const char *goal, const char *path,
                           char *out, size_t out_cap) {
    if (!out || out_cap < 32)
        return 0;
    out[0] = '\0';
    int is_c = path_is_c(path);
    int is_md = path_is_md(path);

    if (is_c && contains_ci(goal, "fib")) {
        snprintf(out, out_cap,
                 "/* peak-agent: fibonacci */\n"
                 "#include <stdio.h>\n"
                 "static int fib(int n) {\n"
                 "    if (n < 2) return n;\n"
                 "    return fib(n - 1) + fib(n - 2);\n"
                 "}\n"
                 "int main(void) {\n"
                 "    for (int i = 0; i < 10; i++)\n"
                 "        printf(\"%%d\\n\", fib(i));\n"
                 "    return 0;\n"
                 "}\n");
        return 1;
    }
    if (is_c && (contains_ci(goal, "hello") || contains_ci(goal, "hello world"))) {
        snprintf(out, out_cap,
                 "/* peak-agent: hello */\n"
                 "#include <stdio.h>\n"
                 "int main(void) {\n"
                 "    printf(\"hello, peak\\n\");\n"
                 "    return 0;\n"
                 "}\n");
        return 1;
    }
    if (is_c && (contains_ci(goal, "http") || contains_ci(goal, "server"))) {
        snprintf(out, out_cap,
                 "/* peak-agent: http stub (guest has no sockets in this scaffold) */\n"
                 "#include <stdio.h>\n"
                 "int main(void) {\n"
                 "    printf(\"http stub — use net.fetch / Peak Browser for live HTTP\\n\");\n"
                 "    return 0;\n"
                 "}\n");
        return 1;
    }
    if (is_c && contains_ci(goal, "makefile")) {
        /* unusual; fall through */
    }
    if (contains_ci(goal, "makefile") || contains_ci(path, "makefile")) {
        snprintf(out, out_cap,
                 "# peak-agent Makefile\n"
                 "CC=clang\n"
                 "CFLAGS=-Wall -Wextra\n"
                 "all: main\n"
                 "main: main.c\n"
                 "\t$(CC) $(CFLAGS) -o $@ $<\n"
                 "clean:\n"
                 "\trm -f main\n");
        return 1;
    }
    if (is_md && (contains_ci(goal, "readme") || contains_ci(path, "README"))) {
        snprintf(out, out_cap,
                 "# Workspace notes\n\n"
                 "Created by peak-agent.\n\n"
                 "## Goal\n\n%s\n",
                 goal ? goal : "");
        return 1;
    }
    if (is_c) {
        /* Generic but non-empty C — still varies via goal comment; body is hello-style. */
        snprintf(out, out_cap,
                 "/* peak-agent create */\n"
                 "/* goal: %s */\n"
                 "#include <stdio.h>\n"
                 "int main(void) {\n"
                 "    printf(\"ok\\n\");\n"
                 "    return 0;\n"
                 "}\n",
                 goal ? goal : "");
        return 1;
    }
    if (is_md || path_is_txt(path)) {
        snprintf(out, out_cap, "# peak-agent\n\n%s\n", goal ? goal : "");
        return 1;
    }
    /* Unknown extension / no template match */
    return 0;
}

/* Apply a real transform to existing content. Returns 1 on success, 0 if no transform matched. */
static int apply_edit_transform(const char *goal, const char *existing, size_t existing_n,
                                char *out, size_t out_cap) {
    if (!out || out_cap < 8 || !existing)
        return 0;
    out[0] = '\0';

    if (contains_ci(goal, "error handling") || contains_ci(goal, "check return") ||
        contains_ci(goal, "null check")) {
        /* Wrap main body with a simple errno-style note + guard stub at top. */
        snprintf(out, out_cap,
                 "%.*s"
                 "\n/* peak-agent edit: added error-handling stub */\n"
                 "static int peak_check(int rc, const char *what) {\n"
                 "    if (rc != 0) {\n"
                 "        /* TODO: log %%s failed */\n"
                 "        (void)what;\n"
                 "        return -1;\n"
                 "    }\n"
                 "    return 0;\n"
                 "}\n",
                 (int)(existing_n < out_cap / 2 ? existing_n : out_cap / 2), existing);
        return 1;
    }
    if (contains_ci(goal, "print") || contains_ci(goal, "printf") ||
        contains_ci(goal, "log ")) {
        snprintf(out, out_cap,
                 "%.*s"
                 "\n/* peak-agent edit: printf helper */\n"
                 "static void peak_log(const char *msg) {\n"
                 "    /* printf(\"%%s\\n\", msg); */\n"
                 "    (void)msg;\n"
                 "}\n",
                 (int)(existing_n < out_cap / 2 ? existing_n : out_cap / 2), existing);
        return 1;
    }
    if (contains_ci(goal, "recursion") || contains_ci(goal, "recursive")) {
        snprintf(out, out_cap,
                 "%.*s"
                 "\n/* peak-agent edit: recursive helper */\n"
                 "static int peak_recur(int n) {\n"
                 "    if (n <= 0) return 0;\n"
                 "    return n + peak_recur(n - 1);\n"
                 "}\n",
                 (int)(existing_n < out_cap / 2 ? existing_n : out_cap / 2), existing);
        return 1;
    }
    if (contains_ci(goal, "todo") || contains_ci(goal, "comment")) {
        snprintf(out, out_cap,
                 "%.*s"
                 "\n/* TODO(peak-agent): %s */\n",
                 (int)(existing_n < out_cap - 80 ? existing_n : out_cap - 80), existing,
                 goal ? goal : "follow-up");
        return 1;
    }
    return 0;
}

void agent_plan_goal(const char *goal, char *summary, size_t summary_cap) {
    console_write_ui("[agent] planner\n");
    console_printf_ui("[agent] goal: %s\n", goal);
    serial_log(SERIAL_LOG_DEBUG, "agent: planner run\n");

    char recall[512];
    memory_recall(goal, recall, sizeof(recall));
    if (recall[0]) {
        agent_tool_console_print("[agent] context from memory/PeakVec:");
        agent_tool_console_print(recall);
    }

    struct agent_slots slots;
    parse_slots(goal, &slots);
    enum agent_intent intent = slots.intent;
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

    if (intent == INTENT_UNKNOWN) {
        agent_tool_console_print(
            "[agent] could not parse goal. try: ask \"create fib.c\" | "
            "\"edit hello.c\" | \"grep needle\" | \"diff a b\" | "
            "\"fetch http://example.com\" | \"summarize workspace\" | help");
        TOOL_NOTE("console.print");
        set_summary(summary, summary_cap, "unparsed goal");
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_PEAKVEC) {
        char qbuf[512];
        const char *q = slots.arg[0] ? slots.arg : goal;
        if (agent_tool_peakvec_query("agent", q, qbuf, sizeof(qbuf)) == 0) {
            TOOL_NOTE("peakvec.query");
            agent_tool_console_print(qbuf);
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "peakvec query");
        } else {
            agent_tool_console_print("[agent] peakvec.query empty or denied");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "peakvec query failed");
        }
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_STORE) {
        const char *text = slots.arg[0] ? slots.arg : goal;
        if (agent_tool_mem_store(text) == 0) {
            TOOL_NOTE("mem.store");
            agent_tool_console_print("[agent] stored in session memory + PeakVec");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "stored memory");
        } else {
            agent_tool_console_print("[agent] mem.store failed or denied");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "store failed");
        }
        memory_append_turn(goal, tools_used, text);
        return;
    }

    if (intent == INTENT_TREE) {
        char buf[2048];
        const char *root = slots.path[0] ? slots.path : "/home/dev/workspace";
        if (agent_tool_fs_tree(root, buf, sizeof(buf)) == 0) {
            agent_tool_console_print(buf);
            TOOL_NOTE("fs.tree");
            set_summary(summary, summary_cap, "workspace tree");
        } else {
            agent_tool_console_print("[agent] fs.tree failed");
            set_summary(summary, summary_cap, "tree failed");
        }
        memory_append_turn(goal, tools_used, path_used[0] ? path_used : NULL);
        return;
    }
    if (intent == INTENT_PS) {
        char buf[1024];
        if (agent_tool_sys_ps(buf, sizeof(buf)) == 0) {
            agent_tool_console_print(buf);
            TOOL_NOTE("sys.ps");
            set_summary(summary, summary_cap, "process list");
        } else {
            agent_tool_console_print("[agent] sys.ps failed");
            set_summary(summary, summary_cap, "ps failed");
        }
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_HELP) {
        agent_tool_console_print(
            "Peak Agent tools: fs.read fs.write fs.list fs.exec fs.stat fs.mkdir fs.rm "
            "fs.search fs.grep fs.diff fs.tree sys.info sys.ps net.ping net.fetch "
            "mem.recall mem.store peakvec.query audit.tail console.print");
        agent_tool_console_print(
            "Try: ask \"summarize workspace\" | \"grep needle\" | \"diff a b\" | "
            "\"fetch http://example.com\" | \"ping example.com\" | audit | memory");
        agent_tool_console_print("CLI builtins: man <cmd> or help");
        TOOL_NOTE("console.print");
        set_summary(summary, summary_cap, "help");
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_RUN) {
        const char *cmdline = slots.arg[0] ? slots.arg : "";
        agent_tool_console_print("[agent] fs.exec:");
        TOOL_NOTE("console.print");
        if (cmdline[0] && agent_tool_fs_exec(cmdline) == 0) {
            TOOL_NOTE("fs.exec");
            set_summary(summary, summary_cap, "ran allowlisted /bin cmd");
        } else {
            agent_tool_console_print("[agent] exec denied or failed (allowlist: ls cat wc stat find …)");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "exec failed");
        }
        memory_append_turn(goal, tools_used, cmdline);
        return;
    }

    if (intent == INTENT_RECALL) {
        char recall_buf[512];
        if (agent_tool_mem_recall(goal, recall_buf, sizeof(recall_buf)) == 0) {
            TOOL_NOTE("mem.recall");
            agent_tool_console_print(recall_buf);
            TOOL_NOTE("console.print");
        } else {
            agent_tool_console_print("[agent] no prior memory — try ask first, or memory / peakvec");
            TOOL_NOTE("console.print");
        }
        set_summary(summary, summary_cap, "recalled session memory");
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_AUDIT) {
        char audit[AGENT_READ_CONTENT_MAX];
        if (agent_tool_audit_tail(audit, sizeof(audit)) == 0) {
            TOOL_NOTE("audit.tail");
            agent_tool_console_print("[agent] audit tail:");
            TOOL_NOTE("console.print");
            agent_tool_console_print(audit);
        } else {
            agent_tool_console_print("[agent] audit empty");
            TOOL_NOTE("console.print");
        }
        set_summary(summary, summary_cap, "showed audit");
        memory_append_turn(goal, tools_used, AGENT_AUDIT_PATH);
        return;
    }

    if (intent == INTENT_SYSINFO) {
        char info[512];
        if (agent_tool_sys_info(info, sizeof(info)) == 0) {
            TOOL_NOTE("sys.info");
            agent_tool_console_print("[agent] system:");
            TOOL_NOTE("console.print");
            agent_tool_console_print(info);
            set_summary(summary, summary_cap, "system info");
        } else {
            agent_tool_console_print("[agent] sys.info unavailable");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "sysinfo failed");
        }
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_PING) {
        if (!slots.arg[0]) {
            agent_tool_console_print("[agent] ping needs a host — try: ask \"ping example.com\"");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "ping failed");
            memory_append_turn(goal, tools_used, NULL);
            return;
        }
        char result[256];
        if (agent_tool_net_ping(slots.arg, result, sizeof(result)) == 0) {
            TOOL_NOTE("net.ping");
            agent_tool_console_print(result);
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "ping ok");
        } else {
            agent_tool_console_print(result[0] ? result : "[agent] ping failed (tool denied or no net grant)");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "ping failed");
        }
        memory_append_turn(goal, tools_used, slots.arg);
        return;
    }

    if (intent == INTENT_FETCH) {
        if (!slots.arg[0]) {
            agent_tool_console_print("[agent] fetch needs a URL — try: ask \"fetch http://example.com\"");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "fetch failed");
            memory_append_turn(goal, tools_used, NULL);
            return;
        }
        char result[AGENT_FETCH_BODY_MAX + 64];
        if (agent_tool_net_fetch(slots.arg, result, sizeof(result)) == 0) {
            TOOL_NOTE("net.fetch");
            agent_tool_console_print(result);
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "fetch ok");
        } else {
            agent_tool_console_print(result[0] ? result : "[agent] fetch failed (tool denied or no net grant)");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "fetch failed");
        }
        memory_append_turn(goal, tools_used, slots.arg);
        return;
    }

    if (intent == INTENT_DIFF) {
        char diff[768];
        if (agent_tool_fs_diff(slots.path, slots.path_b, diff, sizeof(diff)) == 0) {
            TOOL_NOTE("fs.diff");
            agent_tool_console_print("[agent] diff:");
            TOOL_NOTE("console.print");
            agent_tool_console_print(diff);
            set_summary(summary, summary_cap, "diff ok");
        } else {
            agent_tool_console_print("[agent] diff failed");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "diff failed");
        }
        memory_append_turn(goal, tools_used, slots.path);
        return;
    }

    if (intent == INTENT_SEARCH) {
        if (!slots.arg[0]) {
            agent_tool_console_print("[agent] search needs a term — try: ask \"grep needle\"");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "search failed");
            memory_append_turn(goal, tools_used, NULL);
            return;
        }
        char hits[512];
        if (slots.use_grep && agent_tool_fs_grep(slots.arg, hits, sizeof(hits)) == 0) {
            TOOL_NOTE("fs.grep");
            agent_tool_console_print("[agent] grep matches:");
            TOOL_NOTE("console.print");
            agent_tool_console_print(hits);
            set_summary(summary, summary_cap, "grep hits");
        } else if (!slots.use_grep && agent_tool_fs_search(slots.arg, hits, sizeof(hits)) == 0) {
            TOOL_NOTE("fs.search");
            agent_tool_console_print("[agent] search matches:");
            TOOL_NOTE("console.print");
            agent_tool_console_print(hits);
            set_summary(summary, summary_cap, "search results");
        } else {
            agent_tool_console_print(slots.use_grep ? "[agent] no grep matches" : "[agent] no matches");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "search empty");
        }
        memory_append_turn(goal, tools_used, slots.arg);
        return;
    }

    if (intent == INTENT_SUMMARIZE) {
        if (slots.arg[0]) {
            char hits[512];
            if (agent_tool_fs_search(slots.arg, hits, sizeof(hits)) == 0) {
                TOOL_NOTE("fs.search");
                agent_tool_console_print("[agent] summarize search:");
                TOOL_NOTE("console.print");
                agent_tool_console_print(hits);
                const char *first = hits;
                char path[VFS_PATH_MAX];
                size_t pi = 0;
                for (; *first && *first != '\n' && pi + 1 < sizeof(path); first++)
                    path[pi++] = *first;
                path[pi] = '\0';
                if (path[0]) {
                    char body[384];
                    size_t n = 0;
                    if (agent_tool_fs_read(path, body, sizeof(body), &n) == 0 && n) {
                        TOOL_NOTE("fs.read");
                        body[n < sizeof(body) ? n : sizeof(body) - 1] = '\0';
                        if (n > 240)
                            body[240] = '\0';
                        agent_tool_console_print("[agent] snippet:");
                        agent_tool_console_print(body);
                        TOOL_NOTE("console.print");
                    }
                }
                set_summary(summary, summary_cap, "search summarize");
                memory_append_turn(goal, tools_used, slots.arg);
                return;
            }
        }

        char listing[512];
        if (agent_tool_fs_list("/home/dev/workspace", listing, sizeof(listing)) == 0) {
            TOOL_NOTE("fs.list");
            agent_tool_console_print("[agent] workspace:");
            TOOL_NOTE("console.print");
            agent_tool_console_print(listing);
        } else {
            agent_tool_console_print("[agent] could not list workspace");
            TOOL_NOTE("console.print");
        }
        set_summary(summary, summary_cap, "summarized workspace");
        memory_append_turn(goal, tools_used, "/home/dev/workspace");
        return;
    }

    if (intent == INTENT_READ) {
        char body[AGENT_READ_CONTENT_MAX];
        size_t n = 0;
        if (agent_tool_fs_read(slots.path, body, sizeof(body), &n) == 0) {
            TOOL_NOTE("fs.read");
            agent_tool_console_print(slots.path);
            TOOL_NOTE("console.print");
            agent_tool_console_print(body);
            memcpy(path_used, slots.path, strlen(slots.path) + 1);
            set_summary(summary, summary_cap, "read file");
        } else {
            agent_tool_console_print("[agent] read failed");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "read failed");
        }
        memory_append_turn(goal, tools_used, path_used[0] ? path_used : NULL);
        return;
    }

    {
        char listing[512];
        if (agent_tool_fs_list("/home/dev/workspace", listing, sizeof(listing)) == 0) {
            TOOL_NOTE("fs.list");
            console_write_ui("[agent] workspace:\n");
            console_write_ui(listing);
        }

        char path[VFS_PATH_MAX];
        if (slots.path[0])
            memcpy(path, slots.path, strlen(slots.path) + 1);
        else
            workspace_path_from_name(NULL, path, sizeof(path));

        char content[AGENT_PENDING_CONTENT_MAX];
        memset(content, 0, sizeof(content));

        if (intent == INTENT_EDIT) {
            char existing[AGENT_READ_CONTENT_MAX];
            size_t n = 0;
            if (agent_tool_fs_read(path, existing, sizeof(existing), &n) == 0 && n) {
                TOOL_NOTE("fs.read");
                if (!apply_edit_transform(goal, existing, n, content, sizeof(content))) {
                    agent_tool_console_print(
                        "[agent] no edit transform matched — try: "
                        "\"edit … error handling\" | \"edit … print\" | "
                        "\"edit … recursion\" | \"edit … todo\"");
                    TOOL_NOTE("console.print");
                    set_summary(summary, summary_cap, "edit refused");
                    memory_append_turn(goal, tools_used, path);
                    return;
                }
            } else {
                intent = INTENT_CREATE;
            }
        }

        if (intent == INTENT_CREATE || content[0] == '\0') {
            if (!scaffold_create(goal, path, content, sizeof(content))) {
                agent_tool_console_print(
                    "[agent] no create template matched — try: "
                    "\"create fib.c\" | \"create hello.c\" | "
                    "\"create README.md\" | \"create Makefile\"");
                TOOL_NOTE("console.print");
                set_summary(summary, summary_cap, "create refused");
                memory_append_turn(goal, tools_used, path);
                return;
            }
        }

        console_printf_ui("[agent] tool fs.write %s\n", path);
        serial_log(SERIAL_LOG_DEBUG, "agent: fs.write\n");
        int wr = agent_tool_fs_write(path, content, 0);
        TOOL_NOTE("fs.write");
        memcpy(path_used, path, strlen(path) + 1);
        if (wr == 0) {
            agent_tool_console_print("[agent] wrote file");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "wrote file");
        } else if (wr == 1) {
            agent_tool_console_print("[agent] write waiting for GUI approval");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "write pending approval");
        } else {
            agent_tool_console_print("[agent] write denied/failed");
            TOOL_NOTE("console.print");
            set_summary(summary, summary_cap, "write failed");
        }
        memory_append_turn(goal, tools_used, path_used);
        agent_audit_append("goal complete");
    }
    #undef TOOL_NOTE
}
