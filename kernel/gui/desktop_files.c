#include "desktop_internal.h"
#include "fb.h"
#include "keyboard.h"
#include "theme.h"
#include "vfs.h"
#include "clipboard.h"
#include "notify.h"
#include "util.h"

static char files_cwd[VFS_PATH_MAX] = "/home/dev/workspace";
static char files_clip_path[VFS_PATH_MAX];
static int files_sel, files_sel_anchor, files_scroll, files_del_arm, files_ctx_empty;
static int files_clip_cut;

#define FILES_CRUMB_MAX 16
static struct {
    uint32_t x, y, w, h;
    char path[VFS_PATH_MAX];
} files_crumb_hits[FILES_CRUMB_MAX];
static int files_crumb_hit_n;
static int files_a11y_crumb;
static int files_crumb_focus;

static void files_draw_focus_ring(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t c = desktop_color_accent();
    uint32_t t = desktop_u(2);
    if (t < 2)
        t = 2;
    fb_fill_rect(x, y, w, t, c);
    fb_fill_rect(x, y + h - t, w, t, c);
    fb_fill_rect(x, y, t, h, c);
    fb_fill_rect(x + w - t, y, t, h, c);
}

void desktop_files_init(void) {
    files_sel = files_sel_anchor = files_scroll = files_del_arm = files_ctx_empty = 0;
    files_clip_cut = 0;
    files_clip_path[0] = '\0';
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


static void files_chdir_to(const char *path) {
    if (!path || !path[0])
        return;
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(files_cwd); i++)
        files_cwd[i] = path[i];
    files_cwd[i] = '\0';
    files_sel = files_sel_anchor = 0;
    files_scroll = 0;
    files_disarm_del();
    dirty_bits |= DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
}

static void files_crumb_hit_add(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *path) {
    if (files_crumb_hit_n >= FILES_CRUMB_MAX || !path)
        return;
    files_crumb_hits[files_crumb_hit_n].x = x;
    files_crumb_hits[files_crumb_hit_n].y = y;
    files_crumb_hits[files_crumb_hit_n].w = w;
    files_crumb_hits[files_crumb_hit_n].h = h;
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(files_crumb_hits[0].path); i++)
        files_crumb_hits[files_crumb_hit_n].path[i] = path[i];
    files_crumb_hits[files_crumb_hit_n].path[i] = '\0';
    files_crumb_hit_n++;
}

static void files_draw_crumbs(uint32_t tx, uint32_t ty, uint32_t inner) {
    files_crumb_hit_n = 0;
    uint32_t ch = fb_cell_h();
    uint32_t cw = fb_cell_w();
    uint32_t cx = tx;
    uint32_t max_x = tx + inner;
    const char *segs[FILES_CRUMB_MAX];
    char seg_paths[FILES_CRUMB_MAX][VFS_PATH_MAX];
    int nseg = 0;
    static char tmp[VFS_PATH_MAX];
    size_t i = 0;
    for (; files_cwd[i] && i + 1 < sizeof(tmp); i++)
        tmp[i] = files_cwd[i];
    tmp[i] = '\0';

    if (tmp[0] == '/' && !tmp[1]) {
        fb_draw_string(tx, ty, "/", desktop_color_accent(), desktop_color_bg());
        files_crumb_hit_add(tx, ty, cw, ch, "/");
        if (files_a11y_crumb && files_crumb_focus == 0)
            files_draw_focus_ring(tx, ty, cw, ch);
        return;
    }

    char *p = tmp;
    if (*p == '/')
        p++;
    while (*p && nseg < FILES_CRUMB_MAX) {
        segs[nseg] = p;
        while (*p && *p != '/')
            p++;
        if (*p == '/')
            *p++ = '\0';
        if (nseg == 0)
            snprintf(seg_paths[nseg], sizeof(seg_paths[0]), "/%s", segs[nseg]);
        else
            snprintf(seg_paths[nseg], sizeof(seg_paths[0]), "%s/%s", seg_paths[nseg - 1], segs[nseg]);
        nseg++;
    }

    int start = nseg > 3 ? nseg - 3 : 0;
    if (start > 0) {
        char ell_path[VFS_PATH_MAX];
        if (start == 1)
            snprintf(ell_path, sizeof(ell_path), "/");
        else
            snprintf(ell_path, sizeof(ell_path), "%s", seg_paths[start - 1]);
        fb_draw_string(cx, ty, "...", desktop_color_dim(), desktop_color_bg());
        files_crumb_hit_add(cx, ty, cw * 3, ch, ell_path);
        if (files_a11y_crumb && files_crumb_focus == files_crumb_hit_n - 1)
            files_draw_focus_ring(cx, ty, cw * 3, ch);
        cx += cw * 3 + desktop_u(4);
    }

    for (int s = start; s < nseg; s++) {
        if (s > start) {
            fb_draw_string(cx, ty, ">", desktop_color_dim(), desktop_color_bg());
            cx += cw + desktop_u(2);
        }
        const char *lab = segs[s];
        uint32_t lw = (uint32_t)strlen(lab) * cw;
        if (cx + lw > max_x)
            break;
        uint32_t fg = (s == nseg - 1) ? desktop_color_accent() : desktop_color_dim();
        fb_draw_string(cx, ty, lab, fg, desktop_color_bg());
        files_crumb_hit_add(cx, ty, lw, ch, seg_paths[s]);
        if (files_a11y_crumb && files_crumb_focus == files_crumb_hit_n - 1)
            files_draw_focus_ring(cx, ty, lw, ch);
        cx += lw + desktop_u(4);
    }
}

static int files_crumb_click(struct win *w, int32_t mx, int32_t my) {
    uint32_t th = desktop_title_h();
    uint32_t ty = w->y + th + desktop_u(8);
    uint32_t ch = fb_cell_h();
    if ((uint32_t)my < ty || (uint32_t)my >= ty + ch)
        return 0;
    for (int i = 0; i < files_crumb_hit_n; i++) {
if (desktop_point_in(mx, my, files_crumb_hits[i].x, files_crumb_hits[i].y,
                             files_crumb_hits[i].w, files_crumb_hits[i].h)) {
            files_chdir_to(files_crumb_hits[i].path);
            return 1;
        }
    }
    return 0;
}

static uint32_t files_status_h(void) {
    return fb_cell_h() + desktop_u(4);
}

static const char *files_basename(const char *path) {
    const char *base = path;
    if (!path) return "";
    for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
    return base;
}

static void files_build_dest(const char *name, char *dest, size_t cap) {
    if (!name || !name[0] || !dest || cap == 0) {
        if (dest && cap) dest[0] = '\0';
        return;
    }
    if (!strcmp(files_cwd, "/")) snprintf(dest, cap, "/%s", name);
    else snprintf(dest, cap, "%s/%s", files_cwd, name);
}

static void files_clip_clear(void) {
    files_clip_cut = 0;
    files_clip_path[0] = '\0';
}

static void files_clip_store(const char *path, int cut) {
    if (!path || !path[0]) return;
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(files_clip_path); i++)
        files_clip_path[i] = path[i];
    files_clip_path[i] = '\0';
    files_clip_cut = cut ? 1 : 0;
    clipboard_set(files_clip_path, i);
}

static int files_sel_path(char *path, size_t cap) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n || !path || cap == 0) {
        if (path && cap) path[0] = '\0';
        return 0;
    }
    files_build_path(files_sel, path, cap);
    return path[0] != '\0';
}

static void files_copy_sel(void) {
    char path[VFS_PATH_MAX];
    if (!files_sel_path(path, sizeof(path))) return;
    files_clip_store(path, 0);
    notify_push("Copied"); dirty_bits |= DIRTY_TOAST;
}

static void files_cut_sel(void) {
    char path[VFS_PATH_MAX];
    if (!files_sel_path(path, sizeof(path))) return;
    files_clip_store(path, 1);
    notify_push("Cut — paste to move"); dirty_bits |= DIRTY_TOAST;
}

static int files_paste_into_cwd(void) {
    char src[VFS_PATH_MAX];
    size_t n = clipboard_get(src, sizeof(src));
    if (!n) {
        notify_push("Nothing to paste"); dirty_bits |= DIRTY_TOAST;
        return -1;
    }
    if (files_clip_cut && files_clip_path[0] && !strcmp(src, files_clip_path))
        ; /* cut marker matches clipboard text */
    else if (files_clip_path[0])
        files_clip_cut = 0;

    struct vfs_stat st;
    if (vfs_stat(src, &st) != 0) {
        notify_push("Paste source missing"); dirty_bits |= DIRTY_TOAST;
        files_clip_clear();
        return -1;
    }

    const char *base = files_basename(src);
    char dest[VFS_PATH_MAX];
    files_build_dest(base, dest, sizeof(dest));

    if (!strcmp(src, dest)) {
        notify_push("Already here"); dirty_bits |= DIRTY_TOAST;
        return 0;
    }

    if (vfs_exists(dest)) {
        char alt[VFS_NAME_MAX + 8];
        for (int i = 1; i < 100; i++) {
            snprintf(alt, sizeof(alt), "%s~%d", base, i);
            files_build_dest(alt, dest, sizeof(dest));
            if (!vfs_exists(dest)) break;
        }
        if (vfs_exists(dest)) {
            notify_push("Paste failed — name exists"); dirty_bits |= DIRTY_TOAST;
            return -1;
        }
    }

    int rc;
    if (files_clip_cut) {
        rc = vfs_rename(src, dest);
        if (rc == 0) {
            files_clip_clear();
            clipboard_clear();
            notify_push("Moved");
        } else {
            notify_push("Move failed"); dirty_bits |= DIRTY_TOAST;
            return -1;
        }
    } else if (st.type == VFS_DIR) {
        rc = vfs_copy_tree(src, dest);
        notify_push(rc == 0 ? "Folder pasted" : "Paste failed");
        if (rc != 0) { dirty_bits |= DIRTY_TOAST; return -1; }
    } else {
        rc = vfs_copy_file(src, dest);
        notify_push(rc == 0 ? "Pasted" : "Paste failed");
        if (rc != 0) { dirty_bits |= DIRTY_TOAST; return -1; }
    }

    dirty_bits |= DIRTY_TOAST | DIRTY_WIN;
    desktop_mark_focus_surf_dirty();
    return 0;
}

static void files_draw_status(struct win *w, uint32_t tx, uint32_t sy, uint32_t inner) {
    (void)w;
    char status[128];
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n < 0) n = 0;

    if (files_clip_cut && files_clip_path[0]) {
        snprintf(status, sizeof(status), "Cut: %s  ·  Ctrl+V paste here  Esc cancel",
                 files_basename(files_clip_path));
    } else if (n <= 0) {
        snprintf(status, sizeof(status), "Empty folder  ·  n new  Ctrl+V paste  u up  right-click");
    } else if (files_sel >= 0 && files_sel < n) {
        char path[VFS_PATH_MAX], szbuf[16];
        files_build_path(files_sel, path, sizeof(path));
        if (ents[files_sel].type == VFS_DIR) {
            struct vfs_stat st;
            if (vfs_stat(path, &st) == 0)
                snprintf(szbuf, sizeof(szbuf), "%u items", (unsigned)st.nchildren);
            else
                szbuf[0] = '-', szbuf[1] = '\0';
        } else {
            struct vfs_stat st;
            if (vfs_stat(path, &st) == 0) files_format_size(st.size, szbuf, sizeof(szbuf));
            else szbuf[0] = '-', szbuf[1] = '\0';
        }
        int lo = files_sel_anchor < files_sel ? files_sel_anchor : files_sel;
        int hi = files_sel_anchor > files_sel ? files_sel_anchor : files_sel;
        if (lo != hi)
            snprintf(status, sizeof(status), "%d selected  ·  Ctrl+C/X/V  n d r u",
                     hi - lo + 1);
        else
            snprintf(status, sizeof(status), "%s%s  %s  ·  Ctrl+C/X/V  Enter open",
                     ents[files_sel].name, ents[files_sel].type == VFS_DIR ? "/" : "", szbuf);
    } else {
        snprintf(status, sizeof(status), "%d items  ·  Ctrl+V paste  n new  u up", n);
    }

    fb_fill_rect(tx, sy, inner, files_status_h(), desktop_color_surface());
    fb_draw_string_fit(tx + desktop_u(4), sy + desktop_u(2), inner - desktop_u(8), status,
                       desktop_color_dim(), desktop_color_surface());
}

void desktop_files_draw(struct win *w) {
    files_clamp_sel();
    uint32_t ch = fb_cell_h(), th = desktop_title_h();
    uint32_t tx = w->x + desktop_u(12), ty = w->y + th + desktop_u(8);
    uint32_t inner = w->w > desktop_u(24) ? w->w - desktop_u(24) : w->w;
    uint32_t name_w = inner > desktop_u(120) ? inner - desktop_u(72) : inner / 2;
    uint32_t size_x = tx + name_w;
    files_draw_crumbs(tx, ty, inner);
    if (files_del_arm)
        fb_draw_string_fit(tx, ty + ch, inner, "Delete: press d again to confirm · Esc cancel",
                           theme_get()->danger, desktop_color_bg());
    else
        fb_draw_string_fit(tx, ty + ch, inner,
                           "[n]ew [d]el [r]ename [u]p · Ctrl+C/X/V · Shift+arrows select",
                           desktop_color_dim(), desktop_color_bg());
    fb_draw_string_fit(size_x, ty + ch * 2 + desktop_u(2), desktop_u(64), "size", desktop_color_dim(), desktop_color_bg());
    uint32_t status_h = files_status_h();
    uint32_t area_h = w->h > th + ch * 3 + desktop_u(24) + status_h
                          ? w->h - th - ch * 3 - desktop_u(24) - status_h
                          : ch;
    int max_rows = (int)(area_h / ch);
    if (max_rows > FILES_ROWS) max_rows = FILES_ROWS;
    if (max_rows < 1) max_rows = 1;
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n < 0) n = 0;
    if (n == 0) {
        fb_draw_string(tx, ty + ch * 3 + desktop_u(4), "This folder is empty", desktop_color_dim(), desktop_color_bg());
        fb_draw_string(tx, ty + ch * 4 + desktop_u(4), "n new file · Ctrl+V paste · u go up", desktop_color_dim(), desktop_color_bg());
        fb_draw_string(tx, ty + ch * 5 + desktop_u(4), "Copy or cut elsewhere, then paste here", desktop_color_dim(), desktop_color_bg());
        files_draw_status(w, tx, w->y + w->h - status_h - desktop_u(4), inner);
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
        if (selected && !files_a11y_crumb)
            files_draw_focus_ring(tx, rowy, inner, ch);
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
    files_draw_status(w, tx, w->y + w->h - status_h - desktop_u(4), inner);
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
    files_clip_store(path, 0);
    notify_push("Path copied"); dirty_bits |= DIRTY_TOAST;
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
        FADD("Paste", clipboard_has(), 0, CTX_ACT_FILES_PASTE);
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
        FADD("Copy", 1, 0, CTX_ACT_FILES_COPY_PATH);
        FADD("Cut", 1, 0, CTX_ACT_FILES_CUT);
        FADD("Paste", clipboard_has(), 0, CTX_ACT_FILES_PASTE);
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
        if (files_ctx_empty || files_entry_count() <= 0) {
            files_clip_store(files_cwd, 0);
            notify_push("Path copied"); dirty_bits |= DIRTY_TOAST;
        } else files_copy_sel_path();
        return 1;
    case CTX_ACT_FILES_CUT: files_disarm_del(); files_cut_sel(); return 1;
    case CTX_ACT_FILES_PASTE: files_disarm_del(); files_paste_into_cwd(); return 1;
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
    if (key == KEY_TAB || key == '\t') {
        files_a11y_crumb = !files_a11y_crumb;
        if (files_a11y_crumb && files_crumb_hit_n > 0)
            files_crumb_focus = files_crumb_hit_n - 1;
        dirty_bits |= DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
        return 1;
    }
    if (files_a11y_crumb) {
        if (key == KEY_LEFT && files_crumb_focus > 0) {
            files_crumb_focus--;
            dirty_bits |= DIRTY_WIN;
            desktop_mark_focus_surf_dirty();
            return 1;
        }
        if (key == KEY_RIGHT && files_crumb_focus + 1 < files_crumb_hit_n) {
            files_crumb_focus++;
            dirty_bits |= DIRTY_WIN;
            desktop_mark_focus_surf_dirty();
            return 1;
        }
        if (key == '\n' && files_crumb_focus >= 0 && files_crumb_focus < files_crumb_hit_n) {
            files_chdir_to(files_crumb_hits[files_crumb_focus].path);
            return 1;
        }
    }
    if (keyboard_ctrl_down()) {
        if (key == 'c' || key == 'C') { files_disarm_del(); files_copy_sel(); return 1; }
        if (key == 'x' || key == 'X') { files_disarm_del(); files_cut_sel(); return 1; }
        if (key == 'v' || key == 'V') { files_disarm_del(); files_paste_into_cwd(); return 1; }
    }
    if (key == 'n' || key == 'N') { files_disarm_del(); files_new_file(); }
    else if (key == 'd' || key == 'D') files_delete_sel();
    else if (key == 'r' || key == 'R') { files_disarm_del(); files_rename_sel(); }
    else if (key == 'u' || key == 'U') { files_disarm_del(); files_go_up(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); }
    else if (key == '\n') { files_disarm_del(); files_activate(); }
    else if (key == KEY_HOME) { files_sel = 0; if (!extend) files_sel_anchor = 0; files_disarm_del(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); }
    else if (key == KEY_END) { int n = files_entry_count(); files_sel = n > 0 ? n - 1 : 0; if (!extend) files_sel_anchor = files_sel; files_disarm_del(); dirty_bits |= DIRTY_WIN; desktop_mark_focus_surf_dirty(); }
    else if (key == 'j' || key == 'J' || key == KEY_DOWN) files_move_sel(1, extend);
    else if (key == 'k' || key == 'K' || key == KEY_UP) files_move_sel(-1, extend);
    else if (key == 27) {
        files_disarm_del();
        if (files_clip_cut) {
            files_clip_clear();
            notify_push("Cut cancelled");
            dirty_bits |= DIRTY_TOAST | DIRTY_WIN;
            desktop_mark_focus_surf_dirty();
        } else {
            dirty_bits |= DIRTY_WIN;
            desktop_mark_focus_surf_dirty();
        }
    }
    else return 0;
    return 1;
}


static int files_drag_active;
static char files_drag_path[VFS_PATH_MAX];
static char files_drag_name[64];

void desktop_files_drag_begin_sel(void) {
    struct vfs_dirent ents[FILES_ROWS];
    int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0 || files_sel < 0 || files_sel >= n)
        return;
    if (ents[files_sel].type == VFS_DIR)
        return;
    files_build_path(files_sel, files_drag_path, sizeof(files_drag_path));
    snprintf(files_drag_name, sizeof(files_drag_name), "%s", ents[files_sel].name);
    files_drag_active = 1;
}

int desktop_files_drag_active(void) {
    return files_drag_active;
}

void desktop_files_drop_at(int32_t mx, int32_t my) {
    if (!files_drag_active)
        return;
    files_drag_active = 0;
    for (int i = 0; i < MAX_WINS; i++) {
        struct win *w = &wins[i];
        if (!w->open || w->minimized)
            continue;
        if (w->kind != APP_NOTEPAD && w->kind != APP_IMAGES)
            continue;
        if (!desktop_point_in(mx, my, w->x, w->y, w->w, w->h))
            continue;
        if (w->kind == APP_NOTEPAD)
            files_open_text(files_drag_path, files_drag_name);
        else
            files_open_image(files_drag_path, files_drag_name);
        notify_push("Drag-open");
        dirty_bits |= DIRTY_TOAST | DIRTY_WIN;
        desktop_mark_focus_surf_dirty();
        return;
    }
}

void desktop_files_wheel(int wheel) { files_move_sel(wheel > 0 ? -1 : 1, 0); }

int desktop_files_click(struct win *w, int32_t mx, int32_t my, int dbl) {
    if (files_crumb_click(w, mx, my))
        return 1;
    int row;
    if (!files_hit_row(w, my, &row)) return 0;
    struct vfs_dirent ents[FILES_ROWS]; int n = vfs_readdir(files_cwd, ents, FILES_ROWS);
    if (n <= 0) return 0;
    files_sel = files_scroll + row; if (files_sel >= n) files_sel = n - 1;
    if (!keyboard_shift_down()) files_sel_anchor = files_sel;
    if (dbl) files_activate();
    else {
        desktop_files_drag_begin_sel();
        dirty_bits |= DIRTY_WIN;
    }
    return 1;
}
