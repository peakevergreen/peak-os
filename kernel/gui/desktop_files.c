#include "desktop_internal.h"
#include "fb.h"
#include "keyboard.h"
#include "vfs.h"
#include "shell.h"
#include "notify.h"
#include "util.h"

static char files_cwd[VFS_PATH_MAX] = "/home/dev/workspace";
static int files_sel;
static int files_scroll;

void desktop_files_init(void) {
    files_sel = 0;
    files_scroll = 0;
}

static void files_clamp_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0) {
        files_sel = 0;
        files_scroll = 0;
        return;
    }
    if (files_sel < 0)
        files_sel = 0;
    if (files_sel >= n)
        files_sel = n - 1;
}

void desktop_files_draw(struct win *w) {
    files_clamp_sel();
    uint32_t ch = fb_cell_h();
    uint32_t th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(12);
    uint32_t ty = w->y + th + desktop_u(8);
    uint32_t inner = w->w > desktop_u(24) ? w->w - desktop_u(24) : w->w;
    fb_draw_string_fit(tx, ty, inner, files_cwd, desktop_color_dim(), desktop_color_bg());
    fb_draw_string_fit(tx, ty + ch, inner, "[n]ew [d]el [r]ename [u]p  wheel scroll",
                       desktop_color_dim(), desktop_color_bg());
    uint32_t area_h = w->h > th + ch * 2 + desktop_u(24) ? w->h - th - ch * 2 - desktop_u(24) : ch;
    int max_rows = (int)(area_h / ch);
    if (max_rows > FILES_ROWS)
        max_rows = FILES_ROWS;
    if (max_rows < 1)
        max_rows = 1;
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n < 0)
        n = 0;
    if (files_scroll > n)
        files_scroll = n > 0 ? n - 1 : 0;
    if (files_sel < files_scroll)
        files_scroll = files_sel;
    if (files_sel >= files_scroll + max_rows)
        files_scroll = files_sel - max_rows + 1;
    for (int i = 0; i < max_rows && files_scroll + i < n; i++) {
        int idx = files_scroll + i;
        uint32_t rowy = ty + ch * 2 + desktop_u(4) + (uint32_t)i * ch;
        uint32_t bg = (idx == files_sel) ? desktop_color_title() : desktop_color_bg();
        if (idx == files_sel)
            fb_fill_rect(tx, rowy, inner, ch, desktop_color_title());
        char label[VFS_NAME_MAX + 4];
        snprintf(label, sizeof(label), "%s%s", ents[idx].name,
                 ents[idx].type == VFS_DIR ? "/" : "");
        fb_draw_string_fit(tx, rowy, inner, label,
                           ents[idx].type == VFS_DIR ? desktop_color_accent() : desktop_color_fg(), bg);
    }
}

static void files_go_up(void) {
    char *slash = NULL;
    for (char *p = files_cwd; *p; p++)
        if (*p == '/')
            slash = p;
    if (!slash || slash == files_cwd) {
        files_cwd[0] = '/';
        files_cwd[1] = '\0';
    } else {
        *slash = '\0';
        if (!files_cwd[0]) {
            files_cwd[0] = '/';
            files_cwd[1] = '\0';
        }
    }
    files_sel = 0;
    files_scroll = 0;
}

static void files_new_file(void) {
    char path[VFS_PATH_MAX];
    for (int n = 1; n < 100; n++) {
        snprintf(path, sizeof(path), "%s/untitled%d.txt",
                 strcmp(files_cwd, "/") ? files_cwd : "", n);
        if (!strcmp(files_cwd, "/"))
            snprintf(path, sizeof(path), "/untitled%d.txt", n);
        if (!vfs_exists(path)) {
            vfs_write_file(path, "", 0);
            notify_push("Created file");
            dirty_bits |= DIRTY_WIN;
            desktop_mark_focus_surf_dirty();
            return;
        }
    }
}

static void files_delete_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n)
        return;
    char path[VFS_PATH_MAX];
    if (!strcmp(files_cwd, "/"))
        snprintf(path, sizeof(path), "/%s", ents[files_sel].name);
    else
        snprintf(path, sizeof(path), "%s/%s", files_cwd, ents[files_sel].name);
    if (ents[files_sel].type == VFS_DIR)
        vfs_rmdir(path);
    else
        vfs_unlink(path);
    if (files_sel > 0)
        files_sel--;
    notify_push("Deleted");
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static void files_rename_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n)
        return;
    char oldp[VFS_PATH_MAX], newp[VFS_PATH_MAX];
    if (!strcmp(files_cwd, "/"))
        snprintf(oldp, sizeof(oldp), "/%s", ents[files_sel].name);
    else
        snprintf(oldp, sizeof(oldp), "%s/%s", files_cwd, ents[files_sel].name);
    if (!strcmp(files_cwd, "/"))
        snprintf(newp, sizeof(newp), "/%s_renamed", ents[files_sel].name);
    else
        snprintf(newp, sizeof(newp), "%s/%s_renamed", files_cwd, ents[files_sel].name);
    vfs_rename(oldp, newp);
    notify_push("Renamed");
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static void files_activate(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n)
        return;
    if (ents[files_sel].type == VFS_DIR) {
        char next[VFS_PATH_MAX];
        if (!strcmp(files_cwd, "/"))
            snprintf(next, sizeof(next), "/%s", ents[files_sel].name);
        else
            snprintf(next, sizeof(next), "%s/%s", files_cwd, ents[files_sel].name);
        size_t i = 0;
        for (; next[i] && i + 1 < sizeof(files_cwd); i++)
            files_cwd[i] = next[i];
        files_cwd[i] = '\0';
        files_sel = 0;
    } else {
        char cmd[VFS_PATH_MAX + 8];
        if (!strcmp(files_cwd, "/"))
            snprintf(cmd, sizeof(cmd), "cat /%s", ents[files_sel].name);
        else
            snprintf(cmd, sizeof(cmd), "cat %s/%s", files_cwd, ents[files_sel].name);
        desktop_open_app(APP_TERM);
        shell_execute(cmd);
    }
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

int desktop_files_key(int key) {
    if (key == 'n' || key == 'N')
        files_new_file();
    else if (key == 'd' || key == 'D')
        files_delete_sel();
    else if (key == 'r' || key == 'R')
        files_rename_sel();
    else if (key == 'u' || key == 'U') {
        files_go_up();
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
    } else if (key == '\n')
        files_activate();
    else if (key == 'j' || key == 'J' || key == KEY_DOWN) {
        files_sel++;
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
    } else if ((key == 'k' || key == 'K' || key == KEY_UP) && files_sel > 0) {
        files_sel--;
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
    } else
        return 0;
    return 1;
}

void desktop_files_wheel(int wheel) {
    files_sel += wheel > 0 ? -1 : 1;
    if (files_sel < 0)
        files_sel = 0;
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

int desktop_files_click(struct win *w, int32_t mx, int32_t my, int dbl) {
    (void)mx;
    uint32_t ch = fb_cell_h();
    uint32_t content_y = w->y + desktop_title_h() + desktop_u(8) + ch * 2 + desktop_u(4);
    int row = (int)((my - (int32_t)content_y) / (int32_t)ch);
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (row < 0 || n <= 0)
        return 0;
    files_sel = files_scroll + row;
    if (files_sel >= n)
        files_sel = n - 1;
    if (dbl)
        files_activate();
    else
        dirty_bits |= DIRTY_WIN;
    return 1;
}
