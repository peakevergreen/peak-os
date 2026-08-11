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

static void agent_filter_bar_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t lh) {
    const struct peak_theme *th = theme_get();
    char fbar[96];
    const char *q = agent_transcript_filter_text();
    snprintf(fbar, sizeof(fbar), "Filter: %s  Esc=close  Ctrl+F=toggle",
             q && q[0] ? q : "");
    fb_fill_rect(x, y, w, lh + desktop_u(4), th->surface);
    fb_draw_string_fit(x + desktop_u(4), y + desktop_u(2), w - desktop_u(8), fbar,
                       th->accent, th->surface);
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

void desktop_agent_init(void) {
    agent_input_len = 0;
    agent_input[0] = '\0';
    agent_transcript_filter_set_active(0);
}

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
    uint32_t filter_h = agent_transcript_filter_active() ? lh + desktop_u(8) : 0;
    uint32_t appr_h = 0;
    if (agent_write_pending())
        appr_h = lh * 3 + desktop_u(16);
    else if (agent_pending_approvals() > 0)
        appr_h = lh * 2 + desktop_u(8);
    uint32_t gap = desktop_u(4);
    uint32_t reserved = input_h + appr_h + filter_h + gap * 3;
    uint32_t trans_h = ah > reserved ? ah - reserved : lh * 3;
    agent_gui_draw(ax, ay, aw, trans_h);
    uint32_t cy = ay + trans_h + gap;
    if (agent_write_pending()) {
        agent_approval_draw(ax, cy, aw, appr_h);
        cy += appr_h + gap;
    } else if (agent_pending_approvals() > 0) {
        agent_approval_queue_draw(ax, cy, aw);
        cy += appr_h + gap;
    }
    if (agent_transcript_filter_active()) {
        agent_filter_bar_draw(ax, cy, aw, lh);
        cy += filter_h;
    }
    fb_fill_rect(ax, cy, aw, input_h, desktop_color_surface());
    fb_fill_rect(ax, cy, aw, desktop_u(2), th->accent);
    agent_input_draw(ax + desktop_u(4), cy + desktop_u(4), aw - desktop_u(8), lh);
}

static void agent_do_export(void) {
    if (agent_export_transcript("/home/dev/agent-export.txt") >= 0) {
        notify_push("Agent transcript exported");
        dirty_bits |= DIRTY_TOAST;
    } else {
        notify_push("Export failed");
        dirty_bits |= DIRTY_TOAST;
    }
    dirty_bits |= DIRTY_WIN;
}

int desktop_agent_key(int key) {
    /* Ctrl+F (ASCII 6) or '/' toggles filter mode. */
    if ((key == 6 && keyboard_ctrl_down()) ||
        (key == '/' && !agent_transcript_filter_active() && !agent_write_pending())) {
        agent_transcript_filter_set_active(!agent_transcript_filter_active());
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
        return 1;
    }

    /* Y/N approval always wins while a write is pending (even over filter). */
    if (agent_write_pending() && (key == 'y' || key == 'Y' || key == 'n' || key == 'N')) {
        agent_approve_write(key == 'y' || key == 'Y');
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
        return 1;
    }

    /* Filter keys only when filter mode is active. */
    if (agent_transcript_filter_active() && agent_transcript_filter_key(key)) {
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
        return 1;
    }

    /* Ctrl+E export: keyboard delivers Ctrl+letter as ASCII 1–26 (e → 5). */
    if (key == 5 || ((key == 'e' || key == 'E') && keyboard_ctrl_down())) {
        agent_do_export();
        desktop_mark_focus_surf_dirty();
        return 1;
    }

    if (key == KEY_UP) {
        if (agent_transcript_scroll(keyboard_ctrl_down() ? AGENT_SCROLL_PAGE : 1))
            dirty_bits |= DIRTY_WIN;
    } else if (key == KEY_DOWN) {
        if (agent_transcript_scroll(keyboard_ctrl_down() ? -AGENT_SCROLL_PAGE : -1))
            dirty_bits |= DIRTY_WIN;
    } else if (key == KEY_HOME) {
        agent_transcript_reset_scroll();
        dirty_bits |= DIRTY_WIN;
    } else if (key == KEY_END) {
        if (agent_transcript_scroll_end())
            dirty_bits |= DIRTY_WIN;
    } else if (key == '\n' && agent_input_len) {
        agent_ask(agent_input);
        clipboard_set(agent_input, agent_input_len);
        agent_input_len = 0;
        agent_input[0] = '\0';
        agent_notify_done();
    } else if (key == '\b' && agent_input_len) {
        agent_input[--agent_input_len] = '\0';
    } else if (key >= 32 && key < 127 && agent_input_len + 1 < sizeof(agent_input)) {
        agent_input[agent_input_len++] = (char)key;
        agent_input[agent_input_len] = '\0';
    } else {
        return 0;
    }
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
    return 1;
}

int desktop_agent_click(void) {
    /* Focus only — do not submit a goal (Enter submits). */
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
    return 1;
}

void desktop_agent_wheel(int wheel) {
    if (!wheel)
        return;
    if (agent_transcript_scroll(wheel > 0 ? 1 : -1)) {
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
    }
}
