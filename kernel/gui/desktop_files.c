#include "desktop_internal.h"
#include "fb.h"
#include "keyboard.h"
#include "theme.h"
#include "vfs.h"
#include "clipboard.h"
#include "notify.h"
#include "util.h"

static char files_cwd[VFS_PATH_MAX] = "/home/dev/workspace";
static int files_sel, files_sel_anchor, files_scroll, files_del_arm, files_ctx_empty;

void desktop_files_init(void) {
    files_sel = files_sel_anchor = files_scroll = files_del_arm = files_ctx_empty = 0;
}

static void files_disarm_del(void) { files_del_arm = 0; }

void desktop_files_goto(const char *dir, const char *select_name) {
    if (!dir || !dir[0])
        return;
    desktop_open_app(APP_FILES);
    size_t i = 0;
    for (; dir[i] && i + 1 < sizeof(files_cwd); i++)
        files_cwd[i] = dir[i];
    files_cwd[i] = '\0';
    files_sel = files_sel_anchor = 0;
    files_scroll = 0;
    files_disarm_del();
    if (select_name && select_name[0]) {
        struct vfs_dirent ents[FILES_ROWS];
        int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
        for (int j = 0; j < n; j++) {
            if (ents[j].type == VFS_FILE && !strcmp(ents[j].name, select_name)) {
                files_sel = files_sel_anchor = j;
                break;
            }
        }
    }
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static int files_entry_count(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    return n < 0 ? 0 : n;
}

static int files_in_sel_range(int idx) {
    int lo = files_sel_anchor < files_sel ? files_sel_anchor : files_sel;
    int hi = files_sel_anchor > files_sel ? files_sel_anchor : files_sel;
    return idx >= lo && idx <= hi;
}

static void files_move_sel(int delta, int extend) {
    int n = files_entry_count();
    if (n <= 0) return;
    if (!extend) files_sel_anchor = files_sel;
    files_sel += delta;
    if (files_sel < 0) files_sel = 0;
    if (files_sel >= n) files_sel = n - 1;
    if (!extend) files_sel_anchor = files_sel;
    files_disarm_del();
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static void files_clamp_sel(void) {
    int n = files_entry_count();
    if (n <= 0) { files_sel = files_sel_anchor = 0; files_scroll = 0; return; }
    if (files_sel < 0) files_sel = 0;
    if (files_sel >= n) files_sel = n - 1;
}

static void files_build_path(int idx, char *path, size_t cap) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (idx < 0 || idx >= n || !path || cap == 0) { if (path && cap) path[0] = '\0'; return; }
    if (!strcmp(files_cwd, "/")) snprintf(path, cap, "/%s", ents[idx].name);
    else snprintf(path, cap, "%s/%s", files_cwd, ents[idx].name);
}

static int files_ext_is_ci(const char *name, const char *ext) {
    if (!name || !ext) return 0;
    size_t nl = strlen(name), el = strlen(ext);
    if (nl <= el) return 0;
    const char *s = name + nl - el;
    for (size_t i = 0; i < el; i++) {
        char a = s[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int files_is_text(const char *name) {
    static const char *exts[] = {
        ".txt", ".md", ".c", ".h", ".json", ".log", ".sh", ".py", ".js",
        ".html", ".css", ".xml", ".yaml", ".yml", ".toml", ".rs", ".go",
        ".java", ".cpp", ".hpp", ".conf", ".cfg", ".ini", ".def", ".inc", NULL
    };
    for (int i = 0; exts[i]; i++) if (files_ext_is_ci(name, exts[i])) return 1;
    return 0;
}

static int files_is_image(const char *name) {
    return files_ext_is_ci(name, ".ppm") || files_ext_is_ci(name, ".bmp");
}

static int files_path_is_file(const char *path) {
    struct vfs_stat st;
    return path && path[0] && vfs_stat(path, &st) == 0 && st.type == VFS_FILE;
}

static int files_is_volumes_dir(const char *name, enum vfs_type type) {
    if (type != VFS_DIR || !name) return 0;
    return !strcmp(name, "Volumes") || !strcmp(name, "volumes");
}

static void files_format_size(size_t sz, char *buf, size_t cap) {
    if (sz >= 1024 * 1024) snprintf(buf, cap, "%luk", (unsigned long)(sz / 1024));
    else if (sz >= 1024) snprintf(buf, cap, "%luK", (unsigned long)(sz / 1024));
    else snprintf(buf, cap, "%lu", (unsigned long)sz);
}

void desktop_files_draw(struct win *w) {
    files_clamp_sel();
    uint32_t ch = fb_cell_h(), th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(12), ty = w->y + th + desktop_u(8);
    uint32_t inner = w->w > desktop_u(24) ? w->w - desktop_u(24) : w->w;
    uint32_t name_w = inner > desktop_u(120) ? inner - desktop_u(72) : inner / 2;
    uint32_t size_x = tx + name_w;
    {
        char crumb[VFS_PATH_MAX]; const char *segs[16]; int nseg = 0;
        static char tmp[VFS_PATH_MAX]; size_t i = 0;
        for (; files_cwd[i] && i + 1 < sizeof(tmp); i++) tmp[i] = files_cwd[i];
        tmp[i] = '\0';
        if (tmp[0] == '/' && !tmp[1]) snprintf(crumb, sizeof(crumb), "/");
        else {
            char *p = tmp; if (*p == '/') p++;
            while (*p && nseg < 16) {
                segs[nseg++] = p;
                while (*p && *p != '/') p++;
                if (*p == '/') *p++ = '\0';
            }
            int start = nseg > 3 ? nseg - 3 : 0; size_t o = 0;
            if (start > 0) { crumb[o++]='.'; crumb[o++]='.'; crumb[o++]='.'; }
            for (int s = start; s < nseg; s++) {
                if (o && o + 3 < sizeof(crumb)) { crumb[o++]=' '; crumb[o++]='>'; crumb[o++]=' '; }
                for (const char *q = segs[s]; *q && o + 1 < sizeof(crumb); q++) crumb[o++] = *q;
            }
            crumb[o] = '\0';
        }
        fb_draw_string_fit(tx, ty, inner, crumb, desktop_color_dim(), desktop_color_bg());
    }
    if (files_del_arm)
        fb_draw_string_fit(tx, ty + ch, inner, "Delete: press d again to confirm · Esc cancel",
                           theme_get()->danger, desktop_color_bg());
    else
        fb_draw_string_fit(tx, ty + ch, inner,
                           "[n]ew [d]el [r]ename [u]p Home/End · Shift+arrows select",
                           desktop_color_dim(), desktop_color_bg());
    fb_draw_string_fit(size_x, ty + ch * 2 + desktop_u(2), desktop_u(64), "size", desktop_color_dim(), desktop_color_bg());
    uint32_t area_h = w->h > th + ch * 3 + desktop_u(24) ? w->h - th - ch * 3 - desktop_u(24) : ch;
    int max_rows = (int)(area_h / ch);
    if (max_rows > FILES_ROWS) max_rows = FILES_ROWS;
    if (max_rows < 1) max_rows = 1;
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n < 0) n = 0;
    if (n == 0) {
        fb_draw_string(tx, ty + ch * 3 + desktop_u(4), "This folder is empty", desktop_color_dim(), desktop_color_bg());
        fb_draw_string(tx, ty + ch * 4 + desktop_u(4), "n new file · u go up · right-click", desktop_color_dim(), desktop_color_bg());
        return;
    }
    if (files_scroll > n) files_scroll = n > 0 ? n - 1 : 0;
    if (files_sel < files_scroll) files_scroll = files_sel;
    if (files_sel >= files_scroll + max_rows) files_scroll = files_sel - max_rows + 1;
    for (int i = 0; i < max_rows && files_scroll + i < n; i++) {
        int idx = files_scroll + i;
        uint32_t rowy = ty + ch * 3 + desktop_u(4) + (uint32_t)i * ch;
        int selected = files_in_sel_range(idx);
        uint32_t bg = selected ? desktop_color_title() : desktop_color_bg();
        if (selected) fb_fill_rect(tx, rowy, inner, ch, desktop_color_title());
        char label[VFS_NAME_MAX + 4];
        snprintf(label, sizeof(label), "%s%s", ents[idx].name, ents[idx].type == VFS_DIR ? "/" : "");
        fb_draw_string_fit(tx, rowy, name_w, label,
                           ents[idx].type == VFS_DIR ? desktop_color_accent() : desktop_color_fg(), bg);
        char path[VFS_PATH_MAX], szbuf[16];
        files_build_path(idx, path, sizeof(path));
        if (ents[idx].type == VFS_DIR) {
            struct vfs_stat st;
            if (vfs_stat(path, &st) == 0) snprintf(szbuf, sizeof(szbuf), "%u", (unsigned)st.nchildren);
            else szbuf[0] = '-', szbuf[1] = '\0';
        } else {
            struct vfs_stat st;
            if (vfs_stat(path, &st) == 0) files_format_size(st.size, szbuf, sizeof(szbuf));
            else szbuf[0] = '-', szbuf[1] = '\0';
        }
        fb_draw_string_fit(size_x, rowy, inner > name_w ? inner - name_w : desktop_u(64), szbuf, desktop_color_dim(), bg);
    }
    if (n > max_rows) {
        char obuf[24]; int below = n - (files_scroll + max_rows);
        if (below > 0) snprintf(obuf, sizeof(obuf), "+%d below", below);
        else if (files_scroll > 0) snprintf(obuf, sizeof(obuf), "+%d above", files_scroll);
        else obuf[0] = '\0';
        if (obuf[0]) fb_draw_string_fit(tx, ty + ch * 3 + desktop_u(4) + (uint32_t)max_rows * ch, inner, obuf, desktop_color_dim(), desktop_color_bg());
    }
}

static void files_go_up(void) {
    char *slash = NULL;
    for (char *p = files_cwd; *p; p++) if (*p == '/') slash = p;
    if (!slash || slash == files_cwd) { files_cwd[0] = '/'; files_cwd[1] = '\0'; }
    else { *slash = '\0'; if (!files_cwd[0]) { files_cwd[0] = '/'; files_cwd[1] = '\0'; } }
    files_sel = files_sel_anchor = 0; files_scroll = 0;
}

static void files_new_file(void) {
    char path[VFS_PATH_MAX];
    for (int n = 1; n < 100; n++) {
        if (!strcmp(files_cwd, "/")) snprintf(path, sizeof(path), "/untitled%d.txt", n);
        else snprintf(path, sizeof(path), "%s/untitled%d.txt", files_cwd, n);
        if (!vfs_exists(path)) {
            vfs_write_file(path, "", 0); notify_push("Created file");
            dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); return;
        }
    }
}

static void files_delete_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n) return;
    if (!files_del_arm) {
        files_del_arm = 1; notify_push("Press d again to confirm delete");
        dirty_bits |= DIRTY_TOAST | DIRTY_WIN; desktop_mark_focus_surf_dirty(); return;
    }
    files_del_arm = 0;
    char path[VFS_PATH_MAX]; files_build_path(files_sel, path, sizeof(path));
    if (ents[files_sel].type == VFS_DIR) vfs_rmdir(path); else vfs_unlink(path);
    if (files_sel > 0) files_sel--;
    files_sel_anchor = files_sel;
    notify_push("Deleted"); dirty_bits |= DIRTY_TOAST | DIRTY_WIN; desktop_mark_focus_surf_dirty();
}

static void files_rename_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n) return;
    char oldp[VFS_PATH_MAX], newp[VFS_PATH_MAX];
    files_build_path(files_sel, oldp, sizeof(oldp));
    if (!strcmp(files_cwd, "/")) snprintf(newp, sizeof(newp), "/%s_renamed", ents[files_sel].name);
    else snprintf(newp, sizeof(newp), "%s/%s_renamed", files_cwd, ents[files_sel].name);
    vfs_rename(oldp, newp); notify_push("Renamed");
    dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty();
}

static void files_copy_sel_path(void) {
    char path[VFS_PATH_MAX]; files_build_path(files_sel, path, sizeof(path));
    if (!path[0]) return;
    clipboard_set(path, strlen(path)); notify_push("Path copied"); dirty_bits |= DIRTY_TOAST;
}

static int files_open_text(const char *path, const char *name) {
    if (!files_is_text(name) || !files_path_is_file(path)) {
        notify_push("Not a readable text file"); dirty_bits |= DIRTY_TOAST; return -1;
    }
    desktop_notepad_open(path); return 0;
}

static int files_open_image(const char *path, const char *name) {
    if (!files_is_image(name) || !files_path_is_file(path)) {
        notify_push("Not a readable image file"); dirty_bits |= DIRTY_TOAST; return -1;
    }
    desktop_images_open(path); return 0;
}

static void files_activate(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n) return;
    char path[VFS_PATH_MAX]; files_build_path(files_sel, path, sizeof(path));
    if (ents[files_sel].type == VFS_DIR) {
        if (files_is_volumes_dir(ents[files_sel].name, ents[files_sel].type)) {
            desktop_disks_show(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); return;
        }
        size_t i = 0;
        for (; path[i] && i + 1 < sizeof(files_cwd); i++) files_cwd[i] = path[i];
        files_cwd[i] = '\0'; files_sel = files_sel_anchor = 0; files_scroll = 0;
    } else if (files_is_text(ents[files_sel].name)) files_open_text(path, ents[files_sel].name);
    else if (files_is_image(ents[files_sel].name)) files_open_image(path, ents[files_sel].name);
    else { notify_push("No handler for file type"); dirty_bits |= DIRTY_TOAST; }
    dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty();
}

static int files_hit_row(struct win *w, int32_t my, int *row_out) {
    int row = (int)((my - (int32_t)(w->y + desktop_title_h() + desktop_u(8) + fb_cell_h() * 3 + desktop_u(4))) / (int32_t)fb_cell_h());
    if (row_out) *row_out = row; return row >= 0;
}

void desktop_files_ctx_prepare(struct win *w, int32_t mx, int32_t my) {
    (void)mx; int row; struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || !files_hit_row(w, my, &row)) { files_ctx_empty = 1; return; }
    files_ctx_empty = 0; files_sel = files_scroll + row;
    if (files_sel >= n) files_sel = n - 1;
    if (files_sel < 0) files_sel = 0;
    if (!keyboard_shift_down()) files_sel_anchor = files_sel;
    dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty();
}

int desktop_files_ctx_menu(struct ctx_menu_item *items, int max_items) {
    if (!items || max_items < 2) return 0;
    int n = 0;
#define FADD(lbl, en, sep, act) do { if (n >= max_items) return n; \
    items[n].label=(lbl); items[n].enabled=(en); items[n].separator=(sep); items[n].action_id=(act); n++; } while (0)
    if (files_ctx_empty || files_entry_count() <= 0) {
        FADD("New file", 1, 0, CTX_ACT_FILES_NEW);
        FADD("Go up", strcmp(files_cwd, "/") != 0, 0, CTX_ACT_FILES_GO_UP);
        FADD("Copy folder path", 1, 0, CTX_ACT_FILES_COPY_PATH);
    } else {
        struct vfs_dirent ents[FILES_ROWS]; int ec = vfs_readdir(files_cwd, ents, FILES_ROWS);
        if (files_sel < 0 || files_sel >= ec) return desktop_files_ctx_menu(items, max_items);
        FADD("Open", 1, 0, CTX_ACT_FILES_OPEN);
        if (ents[files_sel].type == VFS_FILE && files_is_text(ents[files_sel].name)) FADD("Open with Notepad", 1, 0, CTX_ACT_FILES_OPEN_NPAD);
        if (ents[files_sel].type == VFS_FILE && files_is_image(ents[files_sel].name)) FADD("Open with Images", 1, 0, CTX_ACT_FILES_OPEN_IMG);
        if (files_is_volumes_dir(ents[files_sel].name, ents[files_sel].type)) FADD("Open Disks", 1, 0, CTX_ACT_FILES_OPEN_DISK);
        FADD(NULL, 0, 1, CTX_ACT_NONE);
        FADD("Copy path", 1, 0, CTX_ACT_FILES_COPY_PATH);
        FADD("Rename", 1, 0, CTX_ACT_FILES_RENAME);
        FADD("Delete", 1, 0, CTX_ACT_FILES_DELETE);
        FADD(NULL, 0, 1, CTX_ACT_NONE);
        FADD("New file", 1, 0, CTX_ACT_FILES_NEW);
        FADD("Go up", strcmp(files_cwd, "/") != 0, 0, CTX_ACT_FILES_GO_UP);
    }
    FADD("Close window", 1, 0, CTX_ACT_CLOSE); return n;
#undef FADD
}

int desktop_files_ctx_action(int action_id) {
    switch (action_id) {
    case CTX_ACT_FILES_NEW: files_disarm_del(); files_new_file(); return 1;
    case CTX_ACT_FILES_GO_UP: files_disarm_del(); files_go_up(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); return 1;
    case CTX_ACT_FILES_COPY_PATH:
        if (files_ctx_empty || files_entry_count() <= 0) clipboard_set(files_cwd, strlen(files_cwd));
        else files_copy_sel_path(); return 1;
    case CTX_ACT_FILES_OPEN: files_disarm_del(); files_activate(); return 1;
    case CTX_ACT_FILES_DELETE: files_delete_sel(); return 1;
    case CTX_ACT_FILES_RENAME: files_disarm_del(); files_rename_sel(); return 1;
    case CTX_ACT_FILES_OPEN_NPAD: {
        char path[VFS_PATH_MAX]; struct vfs_dirent ents[FILES_ROWS]; int ec = vfs_readdir(files_cwd, ents, FILES_ROWS);
        if (files_sel < 0 || files_sel >= ec) return 1;
        files_build_path(files_sel, path, sizeof(path)); files_open_text(path, ents[files_sel].name); return 1;
    }
    case CTX_ACT_FILES_OPEN_IMG: {
        char path[VFS_PATH_MAX]; struct vfs_dirent ents[FILES_ROWS]; int ec = vfs_readdir(files_cwd, ents, FILES_ROWS);
        if (files_sel < 0 || files_sel >= ec) return 1;
        files_build_path(files_sel, path, sizeof(path)); files_open_image(path, ents[files_sel].name); return 1;
    }
    case CTX_ACT_FILES_OPEN_DISK: desktop_disks_show(); return 1;
    default: return 0;
    }
}

int desktop_files_key(int key) {
    int extend = keyboard_shift_down();
    if (key == 'n' || key == 'N') { files_disarm_del(); files_new_file(); }
    else if (key == 'd' || key == 'D') files_delete_sel();
    else if (key == 'r' || key == 'R') { files_disarm_del(); files_rename_sel(); }
    else if (key == 'u' || key == 'U') { files_disarm_del(); files_go_up(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); }
    else if (key == '\n') { files_disarm_del(); files_activate(); }
    else if (key == KEY_HOME) { files_sel = 0; if (!extend) files_sel_anchor = 0; files_disarm_del(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); }
    else if (key == KEY_END) { int n = files_entry_count(); files_sel = n > 0 ? n - 1 : 0; if (!extend) files_sel_anchor = files_sel; files_disarm_del(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); }
    else if (key == 'j' || key == 'J' || key == KEY_DOWN) files_move_sel(1, extend);
    else if (key == 'k' || key == 'K' || key == KEY_UP) files_move_sel(-1, extend);
    else if (key == 27) { files_disarm_del(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); }
    else return 0;
    return 1;
}

void desktop_files_wheel(int wheel) { files_move_sel(wheel > 0 ? -1 : 1, 0); }

int desktop_files_click(struct win *w, int32_t mx, int32_t my, int dbl) {
    (void)mx; int row;
    if (!files_hit_row(w, my, &row)) return 0;
    struct vfs_dirent ents[FILES_ROWS]; int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0) return 0;
    files_sel = files_scroll + row; if (files_sel >= n) files_sel = n - 1;
    if (!keyboard_shift_down()) files_sel_anchor = files_sel;
    if (dbl) files_activate(); else dirty_bits |= DIRTY_WIN;
    return 1;
}
