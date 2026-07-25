#include "desktop_internal.h"
#include "fb.h"
#include "theme.h"
#include "peakdisk.h"
#include "privacy.h"
#include "pmm.h"
#include "vfs.h"
#include "notify.h"
#include "util.h"

static int disks_confirm_save;

void desktop_disks_init(void) {
    disks_confirm_save = 0;
}

void desktop_disks_show(void) {
    desktop_open_app(APP_DISKS);
}

static void disks_refresh(void) {
    disks_confirm_save = 0;
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static void disks_save(void) {
    if (!peakdisk_available()) {
        notify_push("No block device");
        dirty_bits |= DIRTY_TOAST;
        return;
    }
    if (privacy_persist_profile() <= 0) {
        notify_push("Enable Privacy workspace persist first");
        dirty_bits |= DIRTY_TOAST;
        return;
    }
    if (!disks_confirm_save) {
        disks_confirm_save = 1;
        notify_push("Click Save again to confirm");
        dirty_bits |= DIRTY_TOAST;
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
        return;
    }
    disks_confirm_save = 0;
    if (peakdisk_save_async() == 0)
        notify_push("Saving workspace to disk…");
    else {
        const char *why = peakdisk_last_error();
        char msg[72];
        if (why && why[0])
            snprintf(msg, sizeof(msg), "Save failed: %s", why);
        else
            snprintf(msg, sizeof(msg), "Save failed");
        notify_push(msg);
    }
    dirty_bits |= DIRTY_TOAST;
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

void desktop_disks_draw(struct win *w) {
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(12);
    uint32_t ty = w->y + th + desktop_u(10);
    uint32_t row = ch + desktop_u(4);
    uint32_t cw = w->w > desktop_u(24) ? w->w - desktop_u(24) : w->w;
    char line[72];

    fb_draw_string(tx, ty, "PeakDisk", desktop_color_accent(), desktop_color_bg());
    ty += row;
    fb_draw_string(tx, ty, peakdisk_available() ? "Block device: present" : "Block device: none",
                   peakdisk_available() ? desktop_color_fg() : desktop_color_dim(), desktop_color_bg());
    ty += row;
    if (peakdisk_busy())
        fb_draw_string(tx, ty, "Status: saving…", desktop_color_accent(), desktop_color_bg());
    else
        fb_draw_string(tx, ty, "Status: idle", desktop_color_dim(), desktop_color_bg());
    ty += row * 2;
    fb_draw_string(tx, ty, "Workspace", desktop_color_accent(), desktop_color_bg());
    ty += row;
    snprintf(line, sizeof(line), "VFS nodes: %d", vfs_node_count());
    fb_draw_string(tx, ty, line, desktop_color_fg(), desktop_color_bg());
    ty += row;
    snprintf(line, sizeof(line), "Tree bytes: %llu",
             (unsigned long long)vfs_tree_bytes("/"));
    fb_draw_string(tx, ty, line, desktop_color_fg(), desktop_color_bg());
    ty += row;
    snprintf(line, sizeof(line), "RAM free: %llu / %llu pages",
             (unsigned long long)pmm_free_pages(), (unsigned long long)pmm_total_pages());
    fb_draw_string(tx, ty, line, desktop_color_fg(), desktop_color_bg());
    ty += row * 2;
    fb_draw_string(tx, ty, "Persistence", desktop_color_accent(), desktop_color_bg());
    ty += row;
    switch (privacy_persist_profile()) {
    case 0:
        fb_draw_string(tx, ty, "private — nothing written", desktop_color_fg(), desktop_color_bg());
        break;
    case 1:
        fb_draw_string(tx, ty, "workspace — /home saved", desktop_color_fg(), desktop_color_bg());
        break;
    default:
        fb_draw_string(tx, ty, "full — home + settings saved", desktop_color_fg(), desktop_color_bg());
        break;
    }
    ty += row;
    if (disks_confirm_save)
        fb_draw_string_fit(tx, ty, cw, "Save: click again to confirm", theme_get()->danger,
                           desktop_color_bg());
    else
        fb_draw_string_fit(tx, ty, cw, "Right-click for Save / Refresh", desktop_color_dim(),
                           desktop_color_bg());
    (void)cw;
}

int desktop_disks_key(int key) {
    if (key == 'r' || key == 'R') {
        disks_refresh();
        return 1;
    }
    if (key == 's' || key == 'S') {
        disks_save();
        return 1;
    }
    if (key == 27) {
        disks_confirm_save = 0;
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
        return 1;
    }
    return 0;
}

int desktop_disks_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 2)
        return 0;
    int n = 0;
#define DADD(l, e, s, a) do { if (n >= max_items) return n; \
    items[n].label=(l); items[n].enabled=(e); items[n].separator=(s); items[n].action_id=(a); n++; } while (0)
    DADD("Refresh", 1, 0, CTX_ACT_DISK_REFRESH);
    DADD("Save workspace", peakdisk_available(), 0, CTX_ACT_DISK_SAVE);
    DADD(NULL, 0, 1, CTX_ACT_NONE);
    DADD("Properties", 1, 0, CTX_ACT_DISK_PROPS);
    DADD(NULL, 0, 1, CTX_ACT_NONE);
    DADD("Close window", 1, 0, CTX_ACT_CLOSE);
    return n;
#undef DADD
}

int desktop_disks_ctx_action(int action_id) {
    switch (action_id) {
    case CTX_ACT_DISK_REFRESH:
        disks_refresh();
        notify_push("Refreshed");
        dirty_bits |= DIRTY_TOAST;
        return 1;
    case CTX_ACT_DISK_SAVE:
        disks_save();
        return 1;
    case CTX_ACT_DISK_PROPS: {
        char msg[80];
        snprintf(msg, sizeof(msg), "nodes=%d disk=%s", vfs_node_count(),
                 peakdisk_available() ? "yes" : "no");
        notify_push(msg);
        dirty_bits |= DIRTY_TOAST;
        return 1;
    }
    default:
        return 0;
    }
}
