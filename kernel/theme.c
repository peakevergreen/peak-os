#include "theme.h"
#include "console.h"
#include "fb.h"
#include "vfs.h"
#include "util.h"

static const struct peak_theme themes[] = {
    {
        .name = "evergreen",
        .bg = 0x000B1A12, .fg = 0x00E8ECF0, .dim = 0x009AC4AE,
        .accent = 0x003DA36A, .danger = 0x00C45C5C,
        .surface = 0x000E1F16, .border = 0x002A4A3A,
        .title = 0x001A3D2C, .cursor = 0x003DA36A,
    },
    {
        .name = "midnight",
        .bg = 0x000A0E1A, .fg = 0x00D8DEE9, .dim = 0x00889BB5,
        .accent = 0x005E81AC, .danger = 0x00BF616A,
        .surface = 0x00111827, .border = 0x003B4252,
        .title = 0x001E2A3A, .cursor = 0x0088C0D0,
    },
    {
        .name = "amber",
        .bg = 0x001A1208, .fg = 0x00FFB84D, .dim = 0x00C4893A,
        .accent = 0x00FF8C1A, .danger = 0x00FF5555,
        .surface = 0x00241A0C, .border = 0x005C3D1A,
        .title = 0x00302810, .cursor = 0x00FFCC66,
    },
    {
        .name = "paper",
        .bg = 0x00F4F1EA, .fg = 0x001A1A1A, .dim = 0x00666666,
        .accent = 0x002E6B4F, .danger = 0x00A33B3B,
        .surface = 0x00E8E4DA, .border = 0x00C4BDAE,
        .title = 0x00DDD7CB, .cursor = 0x002E6B4F,
    },
    {
        .name = "contrast",
        .bg = 0x00000000, .fg = 0x00FFFFFF, .dim = 0x00AAAAAA,
        .accent = 0x00FFFF00, .danger = 0x00FF0000,
        .surface = 0x00111111, .border = 0x00FFFFFF,
        .title = 0x00222222, .cursor = 0x00FFFF00,
    },
};

static int theme_idx;

void theme_apply_console(void) {
    const struct peak_theme *t = theme_get();
    console_set_color(t->fg, t->bg);
}

void theme_init(void) {
    theme_idx = 0;
    /* load persisted */
    char buf[32];
    size_t n = 0;
    if (vfs_read_file("/etc/peak/theme", buf, sizeof(buf) - 1, &n) == 0 && n > 0) {
        buf[n] = '\0';
        /* trim newline */
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = '\0';
        theme_set(buf);
    }
    theme_apply_console();
}

const struct peak_theme *theme_get(void) {
    return &themes[theme_idx];
}

const char *theme_name(void) {
    return themes[theme_idx].name;
}

int theme_set(const char *name) {
    for (int i = 0; i < (int)(sizeof(themes) / sizeof(themes[0])); i++) {
        if (!strcmp(themes[i].name, name)) {
            theme_idx = i;
            theme_apply_console();
            return 0;
        }
    }
    return -1;
}

void theme_next(void) {
    theme_idx = (theme_idx + 1) % (int)(sizeof(themes) / sizeof(themes[0]));
    theme_apply_console();
}

int theme_list(char *out, size_t out_len) {
    size_t o = 0;
    for (int i = 0; i < (int)(sizeof(themes) / sizeof(themes[0])); i++) {
        const char *n = themes[i].name;
        size_t l = strlen(n);
        if (o + l + 2 >= out_len)
            break;
        memcpy(out + o, n, l);
        o += l;
        if (i == theme_idx) {
            out[o++] = '*';
        }
        out[o++] = '\n';
    }
    out[o] = '\0';
    return 0;
}

void theme_persist(void) {
    const char *n = theme_name();
    char buf[64];
    size_t i = 0;
    while (n[i] && i + 2 < sizeof(buf)) {
        buf[i] = n[i];
        i++;
    }
    buf[i++] = '\n';
    buf[i] = '\0';
    vfs_write_file("/etc/peak/theme", buf, i);
}
