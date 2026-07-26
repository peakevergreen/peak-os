#include "desktop_internal.h"
#include "fb.h"
#include "theme.h"
#include "net.h"
#include "privacy.h"
#include "random.h"
#include "timer.h"
#include "notify.h"
#include "clipboard.h"
#include "util.h"
#include "tls_session.h"
#include "tls.h"

static int nc_kill_arm;
static int nc_row; /* context target row */
static char nc_status[48];

static const char *persist_label(int p) {
    switch (p) {
    case 0: return "private (RAM only)";
    case 1: return "workspace (/home)";
    case 2: return "full (home + system)";
    default: return "?";
    }
}

void desktop_netctl_init(void) {
    nc_kill_arm = 0;
    nc_row = -1;
    nc_status[0] = '\0';
}

static void nc_mark_dirty(void) {
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static void nc_dhcp_renew(void) {
    snprintf(nc_status, sizeof(nc_status), "Renewing DHCP…");
    nc_mark_dirty();
    notify_push("Renewing DHCP…");
    dirty_bits |= DIRTY_TOAST;
    if (net_dhcp_try(300) == 0) {
        snprintf(nc_status, sizeof(nc_status), "DHCP renew ok");
        notify_push("DHCP renew ok");
    } else {
        snprintf(nc_status, sizeof(nc_status), "DHCP renew failed");
        notify_push("DHCP renew failed");
    }
    dirty_bits |= DIRTY_TOAST;
    nc_mark_dirty();
}

void desktop_netctl_draw(struct win *w) {
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(12);
    uint32_t ty = w->y + th + desktop_u(10);
    uint32_t row = ch + desktop_u(6);
    char line[72];
    struct net_info ni;
    net_get_info(&ni);
    char ip[32], dns[32];
    net_format_ip(ni.ip, ip, sizeof(ip));
    net_format_ip(ni.dns, dns, sizeof(dns));

    fb_draw_string(tx, ty, "Network Control", desktop_color_accent(), desktop_color_bg());
    ty += row;
    snprintf(line, sizeof(line), "Link: %s  IP %s", ni.up ? "up" : "down", ip);
    fb_draw_string(tx, ty, line, desktop_color_fg(), desktop_color_bg());
    ty += row;
    snprintf(line, sizeof(line), "DNS %s  (%s)", dns, ni.addr_mode ? ni.addr_mode : "?");
    fb_draw_string(tx, ty, line, desktop_color_dim(), desktop_color_bg());
    ty += row * 2;

    fb_draw_string(tx, ty, "Allow-list / privacy", desktop_color_accent(), desktop_color_bg());
    ty += row;
    fb_draw_string(tx, ty, "Outbound net-allow", desktop_color_fg(), desktop_color_bg());
    ty += row;
    snprintf(line, sizeof(line), "  %s — right-click to grant",
             privacy_net_client_allowed() ? "granted" : "blocked");
    fb_draw_string(tx, ty, line, privacy_net_client_allowed() ? desktop_color_accent() : desktop_color_dim(),
                   desktop_color_bg());
    ty += row;
    fb_draw_string(tx, ty, "Kill switch", desktop_color_fg(), desktop_color_bg());
    ty += row;
    if (nc_kill_arm && !privacy_net_kill_switch())
        fb_draw_string(tx, ty, "  click again to ENABLE (blocks all net)", theme_get()->danger,
                       desktop_color_bg());
    else {
        snprintf(line, sizeof(line), "  %s — double-click row to toggle",
                 privacy_net_kill_switch() ? "ON (blocked)" : "off");
        fb_draw_string(tx, ty, line, privacy_net_kill_switch() ? theme_get()->danger : desktop_color_dim(),
                       desktop_color_bg());
    }
    ty += row;
    fb_draw_string(tx, ty, "Persistence profile", desktop_color_fg(), desktop_color_bg());
    ty += row;
    snprintf(line, sizeof(line), "  %s — right-click to cycle", persist_label(privacy_persist_profile()));
    fb_draw_string(tx, ty, line, desktop_color_fg(), desktop_color_bg());
    ty += row * 2;

    fb_draw_string(tx, ty, "Actions", desktop_color_accent(), desktop_color_bg());
    ty += row;
    fb_draw_string(tx, ty, "DHCP renew (click)", desktop_color_fg(), desktop_color_bg());
    ty += row;
    if (nc_status[0])
        fb_draw_string(tx, ty, nc_status, desktop_color_accent(), desktop_color_bg());
    else
        fb_draw_string(tx, ty, "Right-click for copy IP / Settings", desktop_color_dim(), desktop_color_bg());
    ty += row;
    uint32_t rf = random_status_flags();
    snprintf(line, sizeof(line), "RNG flags=0x%x%s%s", (unsigned)rf,
             (rf & RANDOM_READY_CRYPTO) ? " CRYPTO" : "",
             (rf & RANDOM_FLAG_WEAK) ? " WEAK" : "");
    fb_draw_string(tx, ty, line, desktop_color_dim(), desktop_color_bg());
    ty += row;
    snprintf(line, sizeof(line), "TLS cache %d/%d  last resume %s",
             tls_session_used_count(), tls_session_max_slots(),
             tls_last_handshake_resumed() ? "yes" : "no");
    fb_draw_string(tx, ty, line, desktop_color_fg(), desktop_color_bg());
}

int desktop_netctl_click(struct win *w, int32_t mx, int32_t my) {
    (void)mx;
    uint32_t ch = fb_cell_h();
    uint32_t ty = w->y + desktop_title_h() + desktop_u(10) + (ch + desktop_u(6)) * 9;
    if ((uint32_t)my >= ty && (uint32_t)my < ty + ch + desktop_u(6)) {
        nc_dhcp_renew();
        return 1;
    }
    return 0;
}

int desktop_netctl_key(int key) {
    (void)key;
    return 0;
}

void desktop_netctl_ctx_prepare(struct win *w, int32_t mx, int32_t my) {
    (void)mx;
    uint32_t ch = fb_cell_h();
    uint32_t row_h = ch + desktop_u(6);
    uint32_t base = w->y + desktop_title_h() + desktop_u(10) + row_h * 4;
    nc_row = (int)((my - (int32_t)base) / (int32_t)row_h);
    if (nc_row < 0)
        nc_row = -1;
}

int desktop_netctl_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 2)
        return 0;
    int n = 0;
#define CADD(l, e, s, a) do { if (n >= max_items) return n; \
    items[n].label=(l); items[n].enabled=(e); items[n].separator=(s); items[n].action_id=(a); n++; } while (0)
    if (nc_row == 0) {
        CADD("Grant outbound (session)", !privacy_net_client_allowed(), 0, CTX_ACT_NC_ALLOW);
        CADD("Revoke outbound", privacy_net_client_allowed(), 0, CTX_ACT_NC_REVOKE);
    } else if (nc_row == 1) {
        CADD(privacy_net_kill_switch() ? "Disable kill switch" : "Enable kill switch", 1, 0,
             CTX_ACT_NC_KILLSW);
    } else if (nc_row == 2) {
        CADD("Cycle persist profile", 1, 0, CTX_ACT_NC_PERSIST);
    } else {
        CADD("Grant outbound (session)", !privacy_net_client_allowed(), 0, CTX_ACT_NC_ALLOW);
        CADD("DHCP renew", 1, 0, CTX_ACT_NC_DHCP);
    }
    CADD("Copy IP", 1, 0, CTX_ACT_NC_COPY_IP);
    CADD(NULL, 0, 1, CTX_ACT_NONE);
    CADD("Open Settings (Network)", 1, 0, CTX_ACT_NC_SETTINGS);
    CADD(NULL, 0, 1, CTX_ACT_NONE);
    CADD("Close window", 1, 0, CTX_ACT_CLOSE);
    return n;
#undef CADD
}

int desktop_netctl_ctx_action(int action_id) {
    switch (action_id) {
    case CTX_ACT_NC_ALLOW:
        privacy_grant_net_client(0);
        notify_push("Outbound granted");
        dirty_bits |= DIRTY_TOAST;
        nc_mark_dirty();
        return 1;
    case CTX_ACT_NC_REVOKE:
        privacy_clear_session();
        notify_push("Session cleared");
        dirty_bits |= DIRTY_TOAST;
        nc_mark_dirty();
        return 1;
    case CTX_ACT_NC_KILLSW:
        if (privacy_net_kill_switch()) {
            privacy_set_net_kill_switch(0);
            nc_kill_arm = 0;
            notify_push("Kill switch off");
        } else if (!nc_kill_arm) {
            nc_kill_arm = 1;
            notify_push("Click again to confirm kill switch");
        } else {
            privacy_set_net_kill_switch(1);
            nc_kill_arm = 0;
            notify_push("Kill switch ON");
        }
        dirty_bits |= DIRTY_TOAST;
        nc_mark_dirty();
        return 1;
    case CTX_ACT_NC_PERSIST: {
        int next = (privacy_persist_profile() + 1) % 3;
        privacy_set_persist_profile(next);
        notify_push("Persist profile changed");
        dirty_bits |= DIRTY_TOAST;
        nc_mark_dirty();
        return 1;
    }
    case CTX_ACT_NC_DHCP:
        nc_dhcp_renew();
        return 1;
    case CTX_ACT_NC_COPY_IP: {
        struct net_info ni;
        net_get_info(&ni);
        char ip[32];
        net_format_ip(ni.ip, ip, sizeof(ip));
        clipboard_set(ip, strlen(ip));
        notify_push("IP copied");
        dirty_bits |= DIRTY_TOAST;
        return 1;
    }
    case CTX_ACT_NC_SETTINGS:
        settings_page = 4;
        desktop_open_app(APP_SETTINGS);
        return 1;
    default:
        return 0;
    }
}
