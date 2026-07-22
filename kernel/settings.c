#include "settings.h"
#include "fb.h"
#include "util.h"
#include "vfs.h"

#define SETTINGS_PATH "/etc/peak/display"

static uint32_t gui_scale = 3;
static int show_brand = 1;
static int show_clock = 1;

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
        }
    }
    clamp_scale();
}

void settings_persist(void) {
    char buf[96];
    snprintf(buf, sizeof(buf), "scale=%u\nbrand=%d\nclock=%d\n",
             (unsigned)gui_scale, show_brand ? 1 : 0, show_clock ? 1 : 0);
    vfs_write_file(SETTINGS_PATH, buf, strlen(buf));
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
