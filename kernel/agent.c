#include "agent.h"
#include "agent_internal.h"
#include "vfs.h"
#include "console.h"
#include "serial.h"
#include "util.h"
#include "fb.h"
#include "theme.h"
#include "notify.h"

static char last_summary[256];
static int pending;
static int audit_boot_logged;

static int write_wait;
static char write_path[VFS_PATH_MAX];
static char write_content[AGENT_PENDING_CONTENT_MAX];
static int write_approved;

#define AGENT_TLINES 24
#define AGENT_TLINE 96
static char tlines[AGENT_TLINES][AGENT_TLINE];
static int tcount;
static int tscroll;

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

int agent_queue_write_approval(const char *path, const char *content) {
    if (write_wait)
        return -1;
    size_t cl = strlen(content);
    if (cl >= AGENT_PENDING_CONTENT_MAX)
        return -1;
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
    agent_transcript_push("[pending write — press Y to approve, N to deny]");
    return 0;
}

void agent_init(void) {
    last_summary[0] = '\0';
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
        if (vfs_write_file(write_path, write_content, strlen(write_content)) == 0) {
            agent_audit_event("fs.write", write_path, "approved");
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
    uint32_t danger = th->danger;
    uint32_t s = fb_ui_scale();
    uint32_t ch = fb_char_h();
    uint32_t line_h = ch + 4 * s;
    fb_fill_rect(x, y, w, h, bg);
    fb_fill_rect(x, y, w, 2 * s, acc);
    fb_draw_string(x + 8 * s, y + 8 * s, "Peak Agent", fg, bg);
    fb_draw_string(x + 8 * s, y + 8 * s + line_h,
                   "tools: read write list exec search stat sys.info", dim, bg);

    uint32_t ty = y + 8 * s + 2 * line_h;
    uint32_t body_h = h > 120 * s ? h - 100 * s : h / 2;
    if (body_h < line_h * 2)
        body_h = line_h * 2;
    int vis = (int)(body_h / line_h);
    if (vis > AGENT_TLINES)
        vis = AGENT_TLINES;
    if (vis < 3)
        vis = 3;

    int start = tcount - vis - tscroll;
    if (start < 0)
        start = 0;
    for (int row = 0; row < vis; row++) {
        int idx = start + row;
        const char *txt = "(no session — type a goal below)";
        if (idx >= 0 && idx < tcount)
            txt = tlines[idx];
        else if (tcount == 0 && row == vis - 1)
            txt = last_summary[0] ? last_summary : txt;
        fb_draw_string_fit(x + 8 * s, ty + (uint32_t)row * line_h, w - 16 * s, txt, fg, bg);
    }

    if (tcount > vis) {
        char hint[48];
        snprintf(hint, sizeof(hint), "scroll %d/%d (Up/Down)", tscroll, tcount - vis);
        fb_draw_string(x + 8 * s, ty + (uint32_t)vis * line_h, hint, dim, bg);
    }

    uint32_t fy = ty + (uint32_t)(vis + (tcount > vis ? 1 : 0)) * line_h + 4 * s;
    char pend[48];
    snprintf(pend, sizeof(pend), "pending approvals: %d", pending);
    fb_draw_string(x + 8 * s, fy, pend, fg, bg);

    if (write_wait) {
        fb_fill_rect(x + 6 * s, fy + line_h, w - 12 * s, 3 * line_h + 8 * s, th->border);
        fb_draw_string(x + 10 * s, fy + line_h + 4 * s,
                       "WRITE APPROVAL REQUIRED", danger, th->border);
        fb_draw_string(x + 10 * s, fy + 2 * line_h + 4 * s,
                       "Y = approve   N = deny", acc, th->border);
        fb_draw_string_fit(x + 10 * s, fy + 3 * line_h + 4 * s, w - 20 * s,
                           write_path, fg, th->border);
    }
}
