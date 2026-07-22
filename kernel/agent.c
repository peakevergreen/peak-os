#include "agent.h"
#include "agent_internal.h"
#include "vfs.h"
#include "console.h"
#include "util.h"
#include "fb.h"

static char last_summary[256];
static int pending;
static int audit_wiped_once;

static int write_wait;
static char write_path[VFS_PATH_MAX];
static char write_content[AGENT_PENDING_CONTENT_MAX];
static int write_approved;

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
    console_printf("[agent] approval required: fs.write %s (Y/N in Agent)\n", path);
    return 0;
}

void agent_init(void) {
    last_summary[0] = '\0';
    pending = 0;
    write_wait = 0;
    write_approved = 0;
    agent_policy_reload();
    if (!audit_wiped_once) {
        agent_audit_append("session|boot|start|ok");
        audit_wiped_once = 1;
    }
    {
        char tmp[8];
        size_t n = 0;
        if (vfs_read_file(AGENT_MEM_PATH, tmp, sizeof(tmp), &n) != 0 || n == 0)
            vfs_write_file(AGENT_MEM_PATH, "# Peak project memory\n", 22);
    }
}

void agent_ask(const char *goal) {
    agent_policy_reload();
    agent_plan_goal(goal, last_summary, sizeof(last_summary));
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
        if (vfs_write_file(write_path, write_content, strlen(write_content)) == 0)
            agent_audit_event("fs.write", write_path, "approved");
        else
            agent_audit_event("fs.write", write_path, "approve-fail");
    } else {
        agent_audit_event("fs.write", write_path, "denied");
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
