#include <stdio.h>
#include <string.h>

/* Mirror desktop_app_title without linking GUI. */
enum app_kind {
    APP_TERM = 0, APP_FILES, APP_SETTINGS, APP_AGENT, APP_GAME, APP_BROWSER,
    APP_MONITOR, APP_NOTEPAD, APP_IMAGES, APP_DISKS, APP_NETEXP, APP_NETCTL,
};

static const char *app_title(enum app_kind k) {
    switch (k) {
    case APP_TERM: return "Terminal";
    case APP_FILES: return "Files";
    case APP_SETTINGS: return "Settings";
    case APP_AGENT: return "Agent";
    case APP_GAME: return "Peak Runner";
    case APP_BROWSER: return "Browser";
    case APP_MONITOR: return "Monitor";
    case APP_NOTEPAD: return "Notepad";
    case APP_IMAGES: return "Images";
    case APP_DISKS: return "Disks";
    case APP_NETEXP: return "Net Explorer";
    case APP_NETCTL: return "Net Control";
    }
    return "Window";
}

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    for (int k = APP_TERM; k <= APP_NETCTL; k++) {
        const char *t = app_title((enum app_kind)k);
        expect(t && t[0], "title non-empty");
        expect(strcmp(t, "Window") != 0, "known app title");
    }
    expect(!strcmp(app_title(APP_TERM), "Terminal"), "terminal title");
    expect(!strcmp(app_title(APP_NETCTL), "Net Control"), "netctl title");

    if (fails) {
        fprintf(stderr, "%d desktop title test(s) failed\n", fails);
        return 1;
    }
    printf("test_desktop_titles: ok\n");
    return 0;
}
