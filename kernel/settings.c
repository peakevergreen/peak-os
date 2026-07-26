#include "settings.h"
#include "fb.h"
#include "peakdisk.h"
#include "privacy.h"
#include "notify.h"
#include "util.h"
#include "vfs_path_util.h"
#include "vfs.h"

#define SETTINGS_PATH "/etc/peak/display"

static uint32_t gui_scale = 3;
static int show_brand = 1;
static int show_clock = 1;
static int tls_tofu = 0; /* opt-in; WebPKI is default */
static int idle_lock_minutes = 5;

static void clamp_scale(void) {
    if (gui_scale < 1)
        gui_scale = 1;
    if (gui_scale > 4)
        gui_scale = 4;
}

void settings_init(void) {
    gui_scale = fb_recommend_scale();
    show_brand = 1;
    show_clock = 1;
    tls_tofu = 0;
    idle_lock_minutes = 5;

    char buf[128];
    size_t n = 0;
    if (vfs_read_file(SETTINGS_PATH, buf, sizeof(buf) - 1, &n) != 0 || n == 0) {
        clamp_scale();
        return;
    }
    buf[n] = '\0';

    for (char *p = buf; *p; ) {
        char *line = p;
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            *p++ = '\0';
        if (!strncmp(line, "scale=", 6)) {
            int v = 0;
            for (const char *c = line + 6; *c >= '0' && *c <= '9'; c++)
                v = v * 10 + (*c - '0');
            if (v >= 1 && v <= 4)
                gui_scale = (uint32_t)v;
        } else if (!strncmp(line, "brand=", 6)) {
            show_brand = (line[6] != '0');
        } else if (!strncmp(line, "clock=", 6)) {
            show_clock = (line[6] != '0');
        } else if (!strncmp(line, "tls_tofu=", 9)) {
            tls_tofu = (line[9] != '0');
        } else if (!strncmp(line, "idle_lock=", 10)) {
            int v = 0;
            for (const char *c = line + 10; *c >= '0' && *c <= '9'; c++)
                v = v * 10 + (*c - '0');
            if (v >= 1 && v <= 60)
                idle_lock_minutes = v;
        }
    }
    clamp_scale();
}

void settings_persist(void) {
    char buf[96];
    snprintf(buf, sizeof(buf), "scale=%u\nbrand=%d\nclock=%d\ntls_tofu=%d\nidle_lock=%d\n",
             (unsigned)gui_scale, show_brand ? 1 : 0, show_clock ? 1 : 0, tls_tofu ? 1 : 0,
             idle_lock_minutes);
    vfs_write_file(SETTINGS_PATH, buf, strlen(buf));
}

int settings_path_survives_reboot(const char *path) {
    return peakfs_path_allowed_for_profile(path, privacy_persist_profile());
}

void settings_notify_persist(const char *path, const char *label) {
    char msg[72];
    if (!settings_path_survives_reboot(path)) {
        snprintf(msg, sizeof(msg), "%s: session only (private/workspace)", label);
        notify_push(msg);
        return;
    }
    if (!peakdisk_available()) {
        snprintf(msg, sizeof(msg), "%s: saved in RAM (no disk)", label);
        notify_push(msg);
        return;
    }
    snprintf(msg, sizeof(msg), "%s: saved (survives disksave)", label);
    notify_push(msg);
}

uint32_t settings_gui_scale(void) {
    clamp_scale();
    return gui_scale;
}

void settings_set_gui_scale(uint32_t scale) {
    gui_scale = scale;
    clamp_scale();
    fb_set_ui_scale(gui_scale);
}

void settings_cycle_gui_scale(void) {
    gui_scale++;
    if (gui_scale > 4)
        gui_scale = 1;
    fb_set_ui_scale(gui_scale);
}

int settings_show_brand(void) { return show_brand; }

void settings_toggle_brand(void) { show_brand = !show_brand; }

int settings_show_clock(void) { return show_clock; }

void settings_toggle_clock(void) { show_clock = !show_clock; }

int settings_tls_tofu(void) { return tls_tofu; }

void settings_set_tls_tofu(int on) { tls_tofu = on ? 1 : 0; }

void settings_toggle_tls_tofu(void) { tls_tofu = !tls_tofu; }

int settings_idle_lock_minutes(void) {
    if (idle_lock_minutes < 1)
        idle_lock_minutes = 1;
    if (idle_lock_minutes > 60)
        idle_lock_minutes = 60;
    return idle_lock_minutes;
}

void settings_cycle_idle_lock_minutes(void) {
    idle_lock_minutes += 5;
    if (idle_lock_minutes > 60)
        idle_lock_minutes = 1;
}
