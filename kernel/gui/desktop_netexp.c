#include "desktop_internal.h"
#include "fb.h"
#include "keyboard.h"
#include "net.h"
#include "privacy.h"
#include "timer.h"
#include "clipboard.h"
#include "notify.h"
#include "util.h"

#define NETEXP_LINES 12
#define NETEXP_COLS  56

static char nx_lines[NETEXP_LINES][NETEXP_COLS + 1];
static int nx_count;
static char nx_host[64] = "example.com";
static uint32_t nx_last_ip;

static void nx_clear(void) {
    memset(nx_lines, 0, sizeof(nx_lines));
    nx_count = 0;
}

static void nx_append(const char *s) {
    if (!s || nx_count >= NETEXP_LINES)
        return;
    for (size_t i = 0; s[i] && i < NETEXP_COLS; i++)
        nx_lines[nx_count][i] = s[i];
    nx_count++;
}

static void nx_mark_dirty(void) {
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

void desktop_netexp_init(void) {
    nx_clear();
    nx_last_ip = 0;
    nx_append("Net Explorer — Enter=ping  Tab=nslookup");
    struct net_info ni;
    net_get_info(&ni);
    char ip[32], dns[32];
    net_format_ip(ni.ip, ip, sizeof(ip));
    net_format_ip(ni.dns, dns, sizeof(dns));
    char line[NETEXP_COLS + 1];
    snprintf(line, sizeof(line), "link %s  ip %s", ni.up ? "up" : "down", ip);
    nx_append(line);
    snprintf(line, sizeof(line), "dns %s  mode %s", dns, ni.addr_mode ? ni.addr_mode : "?");
    nx_append(line);
}

static void nx_run_ping(void) {
    if (!net_ready()) {
        nx_append("ping: network down");
        nx_mark_dirty();
        return;
    }
    privacy_grant_net_client(0);
    char line[NETEXP_COLS + 1];
    snprintf(line, sizeof(line), "PING %s", nx_host);
    nx_append(line);
    nx_append("  resolving…");
    nx_mark_dirty();
    uint32_t ip = net_dns_resolve(nx_host, 300);
    if (!ip) {
        nx_append("DNS failed");
        nx_mark_dirty();
        return;
    }
    nx_last_ip = ip;
    net_format_ip(ip, line, sizeof(line));
    nx_append(line);
    nx_append("  connecting tcp/80…");
    nx_mark_dirty();
    uint64_t t0 = timer_ticks();
    int cr = net_tcp_connect(ip, 80, 300);
    uint64_t dt = timer_ticks() - t0;
    if (cr == 0) {
        net_tcp_close();
        snprintf(line, sizeof(line), "tcp/:80 open %lums", (unsigned long)(dt * 10));
        nx_append(line);
    } else {
        nx_append("tcp/:80 no response (DNS ok)");
    }
    nx_mark_dirty();
}

static void nx_run_nslookup(void) {
    if (!net_ready()) {
        nx_append("nslookup: network down");
        nx_mark_dirty();
        return;
    }
    privacy_grant_net_client(0);
    struct net_info ni;
    net_get_info(&ni);
    char dns[32], line[NETEXP_COLS + 1];
    net_format_ip(ni.dns, dns, sizeof(dns));
    snprintf(line, sizeof(line), "Server: %s", dns);
    nx_append(line);
    snprintf(line, sizeof(line), "Name: %s", nx_host);
    nx_append(line);
    nx_append("  querying…");
    nx_mark_dirty();
    uint32_t ip = net_dns_resolve(nx_host, 400);
    if (!ip) {
        nx_append("DNS failed");
        nx_mark_dirty();
        return;
    }
    nx_last_ip = ip;
    char addr[32];
    net_format_ip(ip, addr, sizeof(addr));
    snprintf(line, sizeof(line), "Address: %s", addr);
    nx_append(line);
    nx_mark_dirty();
}

void desktop_netexp_draw(struct win *w) {
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(10);
    uint32_t ty = w->y + th + desktop_u(8);
    uint32_t cw = w->w > desktop_u(20) ? w->w - desktop_u(20) : w->w;
    char prompt[80];
    snprintf(prompt, sizeof(prompt), "host: %s  (Enter=ping Tab=nslookup)", nx_host);
    fb_draw_string_fit(tx, ty, cw, prompt, desktop_color_accent(), desktop_color_bg());
    ty += ch + desktop_u(4);
    int vis = NETEXP_LINES;
    int start = nx_count > vis ? nx_count - vis : 0;
    for (int i = 0; i < vis && start + i < nx_count; i++) {
        fb_draw_string_fit(tx, ty + (uint32_t)i * ch, cw, nx_lines[start + i],
                           desktop_color_fg(), desktop_color_bg());
    }
}

int desktop_netexp_key(int key) {
    if (key == '\n') {
        nx_run_ping();
        return 1;
    }
    if (key == '\t') {
        nx_run_nslookup();
        return 1;
    }
    if (key == '\b') {
        size_t n = strlen(nx_host);
        if (n)
            nx_host[n - 1] = '\0';
        nx_mark_dirty();
        return 1;
    }
    if (key >= 32 && key < 127) {
        size_t n = strlen(nx_host);
        if (n + 1 < sizeof(nx_host)) {
            nx_host[n] = (char)key;
            nx_host[n + 1] = '\0';
        }
        nx_mark_dirty();
        return 1;
    }
    return 0;
}

int desktop_netexp_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 2)
        return 0;
    int n = 0;
#define XADD(l, e, s, a) do { if (n >= max_items) return n; \
    items[n].label=(l); items[n].enabled=(e); items[n].separator=(s); items[n].action_id=(a); n++; } while (0)
    XADD("Copy local IP", 1, 0, CTX_ACT_NX_COPY_IP);
    XADD("Copy resolved IP", nx_last_ip != 0, 0, CTX_ACT_NX_COPY_RESOLVED);
    XADD("Ping host", 1, 0, CTX_ACT_NX_PING);
    XADD("Nslookup", 1, 0, CTX_ACT_NX_NSLOOKUP);
    XADD(NULL, 0, 1, CTX_ACT_NONE);
    XADD("Open Net Control", 1, 0, CTX_ACT_NX_NETCTL);
    XADD(NULL, 0, 1, CTX_ACT_NONE);
    XADD("Close window", 1, 0, CTX_ACT_CLOSE);
    return n;
#undef XADD
}

int desktop_netexp_ctx_action(int action_id) {
    struct net_info ni;
    net_get_info(&ni);
    char ip[32];
    net_format_ip(ni.ip, ip, sizeof(ip));
    switch (action_id) {
    case CTX_ACT_NX_COPY_IP:
        clipboard_set(ip, strlen(ip));
        notify_push("Local IP copied");
        dirty_bits |= DIRTY_TOAST;
        return 1;
    case CTX_ACT_NX_COPY_RESOLVED:
        if (nx_last_ip) {
            char addr[32];
            net_format_ip(nx_last_ip, addr, sizeof(addr));
            clipboard_set(addr, strlen(addr));
            notify_push("Resolved IP copied");
            dirty_bits |= DIRTY_TOAST;
        } else {
            notify_push("Run ping or nslookup first");
            dirty_bits |= DIRTY_TOAST;
        }
        return 1;
    case CTX_ACT_NX_PING:
        nx_run_ping();
        return 1;
    case CTX_ACT_NX_NSLOOKUP:
        nx_run_nslookup();
        return 1;
    case CTX_ACT_NX_NETCTL:
        desktop_open_app(APP_NETCTL);
        return 1;
    default:
        return 0;
    }
}
