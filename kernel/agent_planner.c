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
    if (!out || out_len < 8)
        return;
    out[0] = '\0';
    size_t o = 0;

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
    return INTENT_CREATE;
}

static void set_summary(char *summary, size_t summary_cap, const char *s) {
    size_t i = 0;
    for (; s && s[i] && i + 1 < summary_cap; i++)
        summary[i] = s[i];
    summary[i] = '\0';
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
        agent_tool_console_print(
            "Peak Agent: create/edit files, summarize workspace, recall memory, show audit.");
        TOOL_NOTE("console.print");
        set_summary(summary, summary_cap, "help");
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_RECALL) {
        if (!recall[0])
            agent_tool_console_print("[agent] no prior memory yet");
        TOOL_NOTE("console.print");
        set_summary(summary, summary_cap, "recalled session memory");
        memory_append_turn(goal, tools_used, NULL);
        return;
    }

    if (intent == INTENT_AUDIT) {
        /* True tail: read last ~400 bytes even when the log exceeds the buffer. */
        char audit[AGENT_READ_CONTENT_MAX];
        size_t n = 0;
        struct vfs_stat st;
        size_t file_sz = 0;
        if (vfs_stat(AGENT_AUDIT_PATH, &st) == 0 && st.type == VFS_FILE)
            file_sz = st.size;
        size_t want = 400;
        if (want + 1 > sizeof(audit))
            want = sizeof(audit) - 1;
        if (file_sz == 0) {
            agent_tool_console_print("[agent] audit empty");
        } else {
            size_t off = file_sz > want ? file_sz - want : 0;
            if (vfs_read_at(AGENT_AUDIT_PATH, off, audit, want, &n) == 0 && n) {
                audit[n] = '\0';
                agent_tool_console_print("[agent] audit tail:");
                agent_tool_console_print(audit);
            } else {
                agent_tool_console_print("[agent] audit empty");
            }
        }
        TOOL_NOTE("console.print");
        set_summary(summary, summary_cap, "showed audit");
        memory_append_turn(goal, tools_used, AGENT_AUDIT_PATH);
        return;
    }

    if (intent == INTENT_SUMMARIZE) {
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
        char name[64];
        char path[VFS_PATH_MAX] = "/home/dev/workspace/README.md";
        if (extract_filename(goal, name, sizeof(name))) {
            memcpy(path, "/home/dev/workspace/", 20);
            size_t j = 20;
            for (size_t i = 0; name[i] && j + 1 < sizeof(path); i++)
                path[j++] = name[i];
            path[j] = '\0';
        }
        char body[AGENT_READ_CONTENT_MAX];
        size_t n = 0;
        if (agent_tool_fs_read(path, body, sizeof(body), &n) == 0) {
            TOOL_NOTE("fs.read");
            agent_tool_console_print(path);
            TOOL_NOTE("console.print");
            agent_tool_console_print(body);
            memcpy(path_used, path, strlen(path) + 1);
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

        char name[64];
        char path[VFS_PATH_MAX] = "/home/dev/workspace/agent_out.txt";
        if (extract_filename(goal, name, sizeof(name))) {
            memcpy(path, "/home/dev/workspace/", 20);
            size_t j = 20;
            for (size_t i = 0; name[i] && j + 1 < sizeof(path); i++)
                path[j++] = name[i];
            path[j] = '\0';
        }

        char content[AGENT_PENDING_CONTENT_MAX];
        memset(content, 0, sizeof(content));
        size_t o = 0;
        int is_c = 0;
        for (const char *q = path; *q; q++) {
            if (q[0] == '.' && q[1] == 'c' && q[2] == '\0')
                is_c = 1;
        }

        if (intent == INTENT_EDIT) {
            char existing[AGENT_READ_CONTENT_MAX];
            size_t n = 0;
            if (agent_tool_fs_read(path, existing, sizeof(existing), &n) == 0 && n) {
                TOOL_NOTE("fs.read");
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
