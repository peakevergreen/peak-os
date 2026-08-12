#include "agent.h"
#include "agent_internal.h"
#include "vfs.h"
#include "console.h"
#include "serial.h"
#include "util.h"
#include "fb.h"
#include "theme.h"
#include "notify.h"
#include "peakvec.h"

static char last_summary[256];
static int pending;
static int audit_boot_logged;

static int write_wait;
static char write_path[VFS_PATH_MAX];
static char write_content[AGENT_PENDING_CONTENT_MAX];
static int write_approved;

#define AGENT_TLINES 48
#define AGENT_TLINE 128
static char tlines[AGENT_TLINES][AGENT_TLINE];
static int tcount;
static int tscroll;
static char agent_transcript_filter[24];
static int agent_filter_active;
static char last_tool_result[AGENT_TLINE];

/* Match desktop_agent: fb_cell_h() + desktop_u(4) without desktop_internal.h. */
static uint32_t agent_line_h(void) {
    return fb_cell_h() + 4u * fb_ui_scale();
}

static void transcript_push_one(const char *line) {
    if (!line)
        return;
    int slot = tcount < AGENT_TLINES ? tcount : AGENT_TLINES - 1;
    if (tcount >= AGENT_TLINES) {
        memmove(tlines[0], tlines[1], (size_t)(AGENT_TLINES - 1) * AGENT_TLINE);
        slot = AGENT_TLINES - 1;
    } else {
        tcount++;
    }
    size_t i = 0;
    for (; line[i] && i + 1 < AGENT_TLINE; i++)
        tlines[slot][i] = line[i];
    tlines[slot][i] = '\0';
    if (tscroll > 0)
        tscroll++;
}

void agent_transcript_clear(void) {
    memset(tlines, 0, sizeof(tlines));
    tcount = 0;
    tscroll = 0;
}

void agent_transcript_push(const char *line) {
    if (!line || !line[0])
        return;
    char chunk[AGENT_TLINE];
    size_t o = 0;
    for (size_t i = 0; line[i]; i++) {
        char c = line[i];
        if (c == '\r')
            continue;
        if (c == '\n') {
            chunk[o] = '\0';
            if (o)
                transcript_push_one(chunk);
            o = 0;
            continue;
        }
        if (o + 1 < sizeof(chunk))
            chunk[o++] = c;
    }
    if (o) {
        chunk[o] = '\0';
        transcript_push_one(chunk);
    }
}

int agent_transcript_scroll(int delta) {
    if (tcount <= 0)
        return 0;
    int max_scroll = tcount > 1 ? tcount - 1 : 0;
    int before = tscroll;
    tscroll += delta;
    if (tscroll < 0)
        tscroll = 0;
    if (tscroll > max_scroll)
        tscroll = max_scroll;
    return tscroll != before;
}

void agent_transcript_reset_scroll(void) {
    tscroll = 0;
}

int agent_transcript_scroll_end(void) {
    if (tcount <= 0) return 0;
    int max_scroll = tcount > 1 ? tcount - 1 : 0;
    if (tscroll == max_scroll) return 0;
    tscroll = max_scroll; return 1;
}
void agent_transcript_note_audit(const char *op, const char *target, const char *decision) {
    char line[AGENT_TLINE];
    snprintf(line, sizeof(line), "[audit] %s %s -> %s", op?op:"?", target?target:"-", decision?decision:"?");
    agent_transcript_push(line);
}
void agent_transcript_note_recall(const char *msg) {
    if (!msg || !msg[0]) return;
    char line[AGENT_TLINE];
    snprintf(line, sizeof(line), "[recall] %s", msg);
    agent_transcript_push(line);
}

void agent_transcript_note_tool(const char *msg) {
    if (!msg||!msg[0]) return;
    char line[AGENT_TLINE]; snprintf(line, sizeof(line), "[tool] %s", msg);
    size_t i = 0;
    for (; msg[i] && i + 1 < sizeof(last_tool_result); i++)
        last_tool_result[i] = msg[i];
    last_tool_result[i] = '\0';
    agent_transcript_push(line);
}

const char *agent_last_tool_result(void) {
    return last_tool_result;
}

int agent_export_transcript(const char *path) {
    if (!path || !path[0])
        return -1;
    char out[AGENT_TLINES * AGENT_TLINE / 2];
    size_t o = 0;
    for (int i = 0; i < tcount && o + AGENT_TLINE + 2 < sizeof(out); i++) {
        size_t l = strlen(tlines[i]);
        if (o + l + 2 >= sizeof(out))
            break;
        memcpy(out + o, tlines[i], l);
        o += l;
        out[o++] = '\n';
    }
    if (last_tool_result[0] && o + 32 < sizeof(out)) {
        o += (size_t)snprintf(out + o, sizeof(out) - o,
                              "\n--- last tool ---\n%s\n", last_tool_result);
    }
    if (o == 0)
        return -1;
    if (vfs_write_file(path, out, o) != 0)
        return -1;
    return (int)o;
}

void agent_approval_queue_draw(uint32_t x, uint32_t y, uint32_t w) {
    const struct peak_theme *th = theme_get();
    uint32_t lh = agent_line_h();
    char line[96];
    if (pending <= 0) {
        fb_draw_string(x, y, "Approval queue: empty", th->dim, th->surface);
        return;
    }
    snprintf(line, sizeof(line), "Approval queue: %d pending", pending);
    fb_draw_string(x, y, line, th->danger, th->surface);
    if (write_wait && write_path[0]) {
        snprintf(line, sizeof(line), "  #1 fs.write %s (Y/N)", write_path);
        fb_draw_string_fit(x, y + lh, w, line, th->fg, th->surface);
    }
}
int agent_queue_write_approval(const char *path, const char *content) {
    if (write_wait)
        return -2;
    size_t cl = strlen(content);
    if (cl >= AGENT_PENDING_CONTENT_MAX)
        return -3;
    size_t pl = strlen(path);
    if (pl >= sizeof(write_path))
        return -1;
    memcpy(write_path, path, pl + 1);
    memcpy(write_content, content, cl + 1);
    write_wait = 1;
    write_approved = 0;
    pending++;
    console_printf_ui("[agent] approval required: fs.write %s (Y/N in Agent)\n", path);
    serial_log(SERIAL_LOG_DEBUG, "agent: write approval pending\n");
    notify_push("Agent: write needs Y/N approval");
    agent_transcript_push("[pending write — Y approve, N deny]");
    return 0;
}

void agent_init(void) {
    last_summary[0] = '\0';
    last_tool_result[0] = '\0';
    pending = 0;
    write_wait = 0;
    write_approved = 0;
    agent_transcript_clear();
    agent_policy_reload();
    /* Append-only boot marker — never truncate/wipe audit.log on init. */
    if (!audit_boot_logged) {
        agent_audit_append("session|boot|start|ok");
        audit_boot_logged = 1;
    }
    {
        char tmp[8];
        size_t n = 0;
        if (vfs_read_file(AGENT_MEM_PATH, tmp, sizeof(tmp), &n) != 0 || n == 0)
            vfs_write_file(AGENT_MEM_PATH, "# Peak project memory\n", 22);
    }
}

void agent_ask(const char *goal) {
    char prompt[128];
    snprintf(prompt, sizeof(prompt), "> %s", goal ? goal : "");
    agent_transcript_push(prompt);
    agent_transcript_reset_scroll();
    agent_policy_reload();
    agent_plan_goal(goal, last_summary, sizeof(last_summary));
    if (last_summary[0])
        agent_transcript_push(last_summary);
}

int64_t agent_syscall(uint64_t op, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a3;
    if (op == 1) {
        agent_ask((const char *)a1);
        return 0;
    }
    if (op == 2) {
        const char *tools = agent_tools_catalog();
        size_t n = agent_tools_catalog_len();
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

void agent_policy_reload_cli(void) {
    agent_policy_reload();
}

int agent_policy_tool_allowed_cli(const char *tool) {
    return agent_policy_tool_allowed(tool);
}

int agent_policy_deny_reason_cli(const char *tool, const char *path, char *out, size_t out_len) {
    return agent_policy_deny_reason(tool, path, out, out_len);
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

const char *agent_pending_write_content(void) {
    return write_wait ? write_content : "";
}

void agent_approve_write(int yes) {
    if (!write_wait)
        return;
    if (yes) {
        if (vfs_write_file(write_path, write_content, strlen(write_content)) == 0) {
            agent_audit_event("fs.write", write_path, "approved");
            /* Index approved writes into workspace PeakVec (fs.write tool path skips when pending). */
            {
                int16_t vec[PEAKVEC_DIM];
                peakvec_embed_text(write_path, vec);
                (void)peakvec_upsert("ws", write_path, vec, write_path);
            }
            notify_push("Write approved");
            agent_transcript_push("[write approved]");
        } else {
            agent_audit_event("fs.write", write_path, "approve-fail");
            notify_push("Write approve failed");
            agent_transcript_push("[write approve failed]");
        }
    } else {
        agent_audit_event("fs.write", write_path, "denied");
        notify_push("Write denied");
        agent_transcript_push("[write denied]");
    }
    write_wait = 0;
    write_approved = yes ? 1 : -1;
    if (pending > 0)
        pending--;
    write_path[0] = '\0';
    memset(write_content, 0, sizeof(write_content));
}

void agent_gui_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const struct peak_theme *th = theme_get();
    uint32_t bg = th->surface;
    uint32_t fg = th->fg;
    uint32_t acc = th->accent;
    uint32_t dim = th->dim;
    uint32_t s = fb_ui_scale();
    uint32_t line_h = agent_line_h();
    fb_fill_rect(x, y, w, h, bg);
    fb_fill_rect(x, y, w, 2 * s, acc);
    fb_draw_string_fit(x + 8 * s, y + 8 * s, w - 16 * s, "Peak Agent", fg, bg);
    fb_draw_string_fit(x + 8 * s, y + 8 * s + line_h, w - 16 * s,
                       "tools: read write list exec search grep stat sys.info net.ping",
                       dim, bg);

    uint32_t hdr = 8 * s + 2 * line_h + 4 * s;
    uint32_t foot = line_h; /* scroll hint row */
    if (hdr + foot >= h)
        foot = 0;
    uint32_t body_h = h > hdr + foot ? h - hdr - foot : line_h;
    int vis = (int)(body_h / line_h);
    if (vis > AGENT_TLINES)
        vis = AGENT_TLINES;
    if (vis < 1)
        vis = 1;

    uint32_t ty = y + hdr;
    int start = tcount - vis - tscroll;
    if (start < 0)
        start = 0;

    /* Compact filter: only draw matching lines (no blank gaps). */
    int drawn = 0;
    int idx = start;
    while (drawn < vis && idx < tcount) {
        const char *txt = tlines[idx];
        int show = 1;
        if (agent_transcript_filter[0]) {
            const char *f = agent_transcript_filter;
            const char *p = txt;
            show = 0;
            for (; *p; p++) {
                if (*p == *f) {
                    f++;
                    if (!*f) {
                        show = 1;
                        break;
                    }
                } else {
                    f = agent_transcript_filter;
                }
            }
        }
        idx++;
        if (!show)
            continue;
        uint32_t line_fg = fg;
        if (txt[0] == '[' && !strncmp(txt, "[plan]", 6))
            line_fg = acc;
        else if (txt[0] == '[' &&
            (!strncmp(txt, "[audit]", 7) || !strncmp(txt, "[tool]", 6) ||
             !strncmp(txt, "[pending", 8) || !strncmp(txt, "[write", 6)))
            line_fg = dim;
        fb_draw_string_fit(x + 8 * s, ty + (uint32_t)drawn * line_h, w - 16 * s,
                           txt, line_fg, bg);
        drawn++;
    }
    if (tcount == 0 && drawn == 0) {
        const char *empty = last_summary[0] ? last_summary
                                            : "(no session — type a goal below)";
        fb_draw_string_fit(x + 8 * s, ty, w - 16 * s, empty, fg, bg);
    }

    if (foot && tcount > vis) {
        char hint[64];
        snprintf(hint, sizeof(hint), "scroll %d/%d  Up/Down  Ctrl+Up/Down  Home/End",
                 tscroll, tcount - vis);
        fb_draw_string_fit(x + 8 * s, ty + (uint32_t)vis * line_h, w - 16 * s,
                           hint, dim, bg);
    }
    /* Approval queue is drawn by desktop_agent_draw (single path). */
}

int agent_transcript_filter_active(void) {
    return agent_filter_active;
}

void agent_transcript_filter_set_active(int on) {
    agent_filter_active = on ? 1 : 0;
    if (!agent_filter_active)
        agent_transcript_filter[0] = '\0';
}

int agent_transcript_filter_key(int key) {
    if (!agent_filter_active)
        return 0;
    if (key == 27) {
        agent_transcript_filter[0] = '\0';
        agent_filter_active = 0;
        return 1;
    }
    if (key == '\b') {
        size_t n = strlen(agent_transcript_filter);
        if (n)
            agent_transcript_filter[n - 1] = '\0';
        return 1;
    }
    if (key >= 32 && key < 127) {
        size_t n = strlen(agent_transcript_filter);
        if (n + 1 < sizeof(agent_transcript_filter)) {
            agent_transcript_filter[n] = (char)key;
            agent_transcript_filter[n + 1] = '\0';
        }
        return 1;
    }
    return 0;
}

const char *agent_transcript_filter_text(void) {
    return agent_transcript_filter;
}
