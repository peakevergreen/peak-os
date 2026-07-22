#include "desktop_internal.h"
#include "fb.h"
#include "agent.h"
#include "clipboard.h"
#include "notify.h"
#include "util.h"

static char agent_input[96];
static uint32_t agent_input_len;

void desktop_agent_init(void) {
    agent_input_len = 0;
    agent_input[0] = '\0';
}

void desktop_app_opened(enum app_kind k) {
    if (k == APP_AGENT)
        desktop_agent_init();
}

void desktop_agent_draw(struct win *w) {
    uint32_t ax = w->x + desktop_u(8);
    uint32_t ay = w->y + desktop_title_h() + desktop_u(4);
    uint32_t aw = w->w - desktop_u(16);
    uint32_t ah = w->h - desktop_title_h() - desktop_u(12);
    agent_gui_draw(ax, ay, aw, ah > desktop_u(80) ? ah - desktop_u(40) : ah / 2);
    uint32_t iy = ay + (ah > desktop_u(80) ? ah - desktop_u(36) : ah / 2 + desktop_u(4));
    fb_fill_rect(ax, iy, aw, fb_cell_h() + desktop_u(8), desktop_color_surface());
    char prompt[112];
    snprintf(prompt, sizeof(prompt), "> %s", agent_input);
    fb_draw_string_fit(ax + desktop_u(4), iy + desktop_u(4), aw - desktop_u(8), prompt,
                       desktop_color_fg(), desktop_color_surface());
}

int desktop_agent_key(int key) {
    if (agent_write_pending() && (key == 'y' || key == 'Y' || key == 'n' || key == 'N')) {
        agent_approve_write(key == 'y' || key == 'Y');
        notify_push((key == 'y' || key == 'Y') ? "Write approved" : "Write denied");
    } else if (key == '\n') {
        if (agent_input_len) {
            agent_ask(agent_input);
            clipboard_set(agent_input, agent_input_len);
            agent_input_len = 0;
            agent_input[0] = '\0';
            notify_push("Agent running");
        }
    } else if (key == '\b' && agent_input_len) {
        agent_input[--agent_input_len] = '\0';
    } else if (key >= 32 && key < 127 && agent_input_len + 1 < sizeof(agent_input)) {
        agent_input[agent_input_len++] = (char)key;
        agent_input[agent_input_len] = '\0';
    } else
        return 0;
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
    return 1;
}

int desktop_agent_click(void) {
    if (agent_input_len)
        agent_ask(agent_input);
    else
        agent_ask("summarize workspace README");
    notify_push("Agent running");
    dirty_bits |= DIRTY_WIN;
    return 1;
}
