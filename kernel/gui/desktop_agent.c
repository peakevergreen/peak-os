#include "desktop_internal.h"
#include "fb.h"
#include "agent.h"
#include "clipboard.h"
#include "notify.h"
#include "keyboard.h"
#include "util.h"
#include "theme.h"
#include "vfs.h"

#define AGENT_INPUT_MAX 256
#define AGENT_INPUT_LINES 2
#define AGENT_SCROLL_PAGE 5

static char agent_input[AGENT_INPUT_MAX];
static uint32_t agent_input_len;

static void agent_notify_done(void) {
    if (agent_write_pending()) {
        const char *path = agent_pending_write_path();
        char msg[128];
        snprintf(msg, sizeof(msg), "Agent: approve write to %s (Y/N)",
                 path && path[0] ? path : "file");
        notify_push(msg);
    } else {
        notify_push("Agent finished");
    }
}

static void agent_input_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t lh) {
    uint32_t cols = fb_cell_w() ? w / fb_cell_w() : 32;
    if (cols < 16) cols = 16;
    if (agent_input_len + 2 <= cols) {
        char line[AGENT_INPUT_MAX + 4];
        snprintf(line, sizeof(line), "> %s", agent_input);
        fb_draw_string_fit(x, y, w, line, desktop_color_fg(), desktop_color_surface());
        return;
    }
    char line1[AGENT_INPUT_MAX + 4];
    snprintf(line1, sizeof(line1), "> %.*s", (int)(cols > 2 ? cols - 2 : 1), agent_input);
    fb_draw_string_fit(x, y, w, line1, desktop_color_fg(), desktop_color_surface());
    size_t off = cols > 2 ? cols - 2 : 0;
    if (off < agent_input_len)
        fb_draw_string_fit(x, y + lh, w, agent_input + off, desktop_color_fg(), desktop_color_surface());
}

static void agent_approval_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const struct peak_theme *th = theme_get();
    const char *path = agent_pending_write_path();
    uint32_t lh = fb_char_h() + desktop_u(4);
    fb_fill_rect(x, y, w, h, th->border);
    fb_fill_rect(x + desktop_u(1), y + desktop_u(1), w - desktop_u(2), h - desktop_u(2), th->surface);
    fb_draw_string(x + desktop_u(8), y + desktop_u(4), "Write approval required", th->danger, th->surface);
    fb_draw_string(x + desktop_u(8), y + lh + desktop_u(2), "Y = approve write    N = deny", th->accent, th->surface);
    char pline[VFS_PATH_MAX + 16];
    snprintf(pline, sizeof(pline), "path: %s", path && path[0] ? path : "(unknown)");
    fb_draw_string_fit(x + desktop_u(8), y + 2 * lh, w - desktop_u(16), pline, th->fg, th->surface);
}

void desktop_agent_init(void) { agent_input_len ='\0'; agent_input[0] ='\0'; }
void desktop_app_opened(enum app_kind k) {
    if (k == APP_AGENT) desktop_agent_init();
    if (k == APP_NOTEPAD) desktop_notepad_init();
}

void desktop_agent_draw(struct win *w) {
    const struct peak_theme *th = theme_get();
    uint32_t ax = w->x + desktop_u(8), ay = w->y + desktop_title_h() + desktop_u(4);
    uint32_t aw = w->w - desktop_u(16), ah = w->h - desktop_title_h() - desktop_u(12);
    uint32_t lh = fb_char_h() + desktop_u(4);
    uint32_t input_h = lh * AGENT_INPUT_LINES + desktop_u(12);
    uint32_t appr_h = agent_write_pending() ? lh * 3 + desktop_u(16) : 0;
    uint32_t gap = desktop_u(4);
    uint32_t trans_h = ah > input_h + appr_h + gap * 2 ? ah - input_h - appr_h - gap * 2 : ah / 2;
    agent_gui_draw(ax, ay, aw, trans_h);
    uint32_t cy = ay + trans_h + gap;
    if (agent_write_pending()) { agent_approval_draw(ax, cy, aw, appr_h); cy += appr_h + gap; }
    fb_fill_rect(ax, cy, aw, input_h, desktop_color_surface());
    fb_fill_rect(ax, cy, aw, desktop_u(2), th->accent);
    agent_input_draw(ax + desktop_u(4), cy + desktop_u(4), aw - desktop_u(8), lh);
}

int desktop_agent_key(int key) {
    if ((key == 6 || key == 'f' || key == 'F') && keyboard_ctrl_down()) {
        if (agent_transcript_filter_key(0)) {} /* open filter via typing */
        dirty_bits |= DIRTY_WIN;
        return 1;
    }
    if (agent_transcript_filter_key(key)) { dirty_bits |= DIRTY_WIN; return 1; }
    if (agent_write_pending() && (key == 'y' || key == 'Y' || key == 'n' || key == 'N'))
        agent_approve_write(key == 'y' || key == 'Y');
    else if (key == KEY_UP) { if (agent_transcript_scroll(keyboard_ctrl_down() ? AGENT_SCROLL_PAGE : 1)) dirty_bits |= DIRTY_WIN; }
    else if (key == KEY_DOWN) { if (agent_transcript_scroll(keyboard_ctrl_down() ? -AGENT_SCROLL_PAGE : -1)) dirty_bits |= DIRTY_WIN; }
    else if (key == KEY_HOME) { agent_transcript_reset_scroll(); dirty_bits |= DIRTY_WIN; }
    else if (key == KEY_END) { if (agent_transcript_scroll_end()) dirty_bits |= DIRTY_WIN; }
    else if ((key == 6 || key == 18) && keyboard_ctrl_down()) { if (agent_transcript_filter_key(key == 6 ? 27 : key)) dirty_bits |= DIRTY_WIN; }
    else if (key == '\n' && agent_input_len) {
        agent_ask(agent_input); clipboard_set(agent_input, agent_input_len);
        agent_input_len ='\0'; agent_input[0] ='\0'; agent_notify_done();
    } else if (key == '\b' && agent_input_len) agent_input[--agent_input_len] ='\0';
    else if (key >= 32 && key < 127 && agent_input_len + 1 < sizeof(agent_input)) {
        agent_input[agent_input_len++] = (char)key; agent_input[agent_input_len] ='\0';
    } else return 0;
    dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); return 1;
}

int desktop_agent_click(void) {
    if (agent_input_len) agent_ask(agent_input); else agent_ask("summarize workspace README");
    agent_notify_done(); dirty_bits |= DIRTY_WIN; return 1;
}
