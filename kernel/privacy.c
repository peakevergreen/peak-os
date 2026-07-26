#include "privacy.h"
#include "cap.h"
#include "clipboard.h"
#include "notify.h"
#include "util.h"

static int persist_profile; /* private default for privacy-first */
static int net_kill;
static int net_client_grant;
static int net_listen_grant;
static int net_lan_grant;
static int listeners_localhost = 1;

void privacy_init(void) {
    persist_profile = 0;
    net_kill = 0;
    net_client_grant = 0;
    net_listen_grant = 0;
    net_lan_grant = 0;
    listeners_localhost = 1;
}

void privacy_clear_session(void) {
    net_client_grant = 0;
    net_listen_grant = 0;
    net_lan_grant = 0;
    listeners_localhost = 1;
    cap_set_current(cap_current() & ~(CAP_NET_CLIENT | CAP_NET_LISTEN | CAP_NET_LAN));
    clipboard_clear();
    notify_clear();
}

int privacy_persist_profile(void) {
    return persist_profile;
}

void privacy_set_persist_profile(int profile) {
    if (profile < 0)
        profile = 0;
    if (profile > 2)
        profile = 2;
    persist_profile = profile;
}

int privacy_net_kill_switch(void) {
    return net_kill;
}

void privacy_set_net_kill_switch(int on) {
    net_kill = on ? 1 : 0;
}

int privacy_net_client_allowed(void) {
    if (net_kill)
        return 0;
    if (!cap_check(CAP_NET_CLIENT))
        return 0;
    return net_client_grant;
}

void privacy_grant_net_client(int remember) {
    (void)remember; /* RAM-only by default */
    net_client_grant = 1;
    cap_set_current(cap_current() | CAP_NET_CLIENT);
}

int privacy_net_listen_allowed(int lan) {
    if (net_kill)
        return 0;
    if (!cap_check(CAP_NET_LISTEN))
        return 0;
    if (!net_listen_grant)
        return 0;
    if (lan && (!net_lan_grant || !cap_check(CAP_NET_LAN)))
        return 0;
    return 1;
}

void privacy_grant_net_listen(int lan, int remember) {
    (void)remember;
    net_listen_grant = 1;
    cap_set_current(cap_current() | CAP_NET_LISTEN);
    if (lan) {
        net_lan_grant = 1;
        cap_set_current(cap_current() | CAP_NET_LAN);
        listeners_localhost = 0;
    }
}

int privacy_listeners_localhost_only(void) {
    return listeners_localhost;
}

void privacy_set_listeners_localhost_only(int on) {
    listeners_localhost = on ? 1 : 0;
}

static const char *privacy_grant_names[] = {
    "Outbound client",
    "Listen (local)",
    "LAN listen",
};

int privacy_grant_list_count(void) {
    return 3;
}

const char *privacy_grant_label(int idx) {
    if (idx < 0 || idx >= privacy_grant_list_count())
        return "";
    return privacy_grant_names[idx];
}

int privacy_grant_active(int idx) {
    switch (idx) {
    case 0: return net_client_grant;
    case 1: return net_listen_grant;
    case 2: return net_lan_grant;
    default: return 0;
    }
}

void privacy_revoke_grant(int idx) {
    switch (idx) {
    case 0:
        net_client_grant = 0;
        cap_set_current(cap_current() & ~CAP_NET_CLIENT);
        break;
    case 1:
        net_listen_grant = 0;
        cap_set_current(cap_current() & ~CAP_NET_LISTEN);
        break;
    case 2:
        net_lan_grant = 0;
        listeners_localhost = 1;
        cap_set_current(cap_current() & ~CAP_NET_LAN);
        break;
    default:
        break;
    }
}

void privacy_revoke_net_client(void) {
    privacy_revoke_grant(0);
}
