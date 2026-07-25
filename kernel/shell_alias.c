/* Shell aliases persisted at /var/peak/aliases (name=value lines). */
#include "shell.h"
#include "console.h"
#include "util.h"
#include "vfs.h"

#define ALIAS_MAX  32
#define ALIAS_KEY  32
#define ALIAS_VAL  96
#define ALIAS_PATH "/var/peak/aliases"

static struct {
    char key[ALIAS_KEY];
    char val[ALIAS_VAL];
    int used;
} aliases[ALIAS_MAX];

static void alias_persist(void) {
    (void)vfs_mkdir("/var/peak");
    char buf[ALIAS_MAX * (ALIAS_KEY + ALIAS_VAL + 2)];
    size_t o = 0;
    for (int i = 0; i < ALIAS_MAX && o + 4 < sizeof(buf); i++) {
        if (!aliases[i].used)
            continue;
        size_t kn = strlen(aliases[i].key);
        size_t vn = strlen(aliases[i].val);
        if (o + kn + vn + 3 >= sizeof(buf))
            break;
        memcpy(buf + o, aliases[i].key, kn);
        o += kn;
        buf[o++] = '=';
        memcpy(buf + o, aliases[i].val, vn);
        o += vn;
        buf[o++] = '\n';
    }
    if (o)
        vfs_write_file(ALIAS_PATH, buf, o);
    else
        vfs_write_file(ALIAS_PATH, "", 0);
}

void shell_alias_init(void) {
    memset(aliases, 0, sizeof(aliases));
    (void)vfs_mkdir("/var/peak");
    char buf[2048];
    size_t n = 0;
    if (vfs_read_file(ALIAS_PATH, buf, sizeof(buf) - 1, &n) != 0 || !n)
        return;
    buf[n] = '\0';
    const char *p = buf;
    while (*p) {
        while (*p == '\n')
            p++;
        if (!*p)
            break;
        char key[ALIAS_KEY], val[ALIAS_VAL];
        size_t ki = 0, vi = 0;
        while (*p && *p != '=' && *p != '\n' && ki + 1 < sizeof(key))
            key[ki++] = *p++;
        key[ki] = '\0';
        if (*p == '=')
            p++;
        while (*p && *p != '\n' && vi + 1 < sizeof(val))
            val[vi++] = *p++;
        val[vi] = '\0';
        if (ki)
            (void)shell_alias_set(key, val);
        while (*p && *p != '\n')
            p++;
    }
}

const char *shell_alias_lookup(const char *name) {
    if (!name || !name[0])
        return NULL;
    for (int i = 0; i < ALIAS_MAX; i++)
        if (aliases[i].used && !strcmp(aliases[i].key, name))
            return aliases[i].val;
    return NULL;
}

int shell_alias_set(const char *name, const char *val) {
    if (!name || !name[0] || !val)
        return -1;
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (aliases[i].used && !strcmp(aliases[i].key, name)) {
            size_t j = 0;
            for (; val[j] && j + 1 < ALIAS_VAL; j++)
                aliases[i].val[j] = val[j];
            aliases[i].val[j] = '\0';
            alias_persist();
            return 0;
        }
    }
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (!aliases[i].used) {
            size_t j = 0;
            for (; name[j] && j + 1 < ALIAS_KEY; j++)
                aliases[i].key[j] = name[j];
            aliases[i].key[j] = '\0';
            j = 0;
            for (; val[j] && j + 1 < ALIAS_VAL; j++)
                aliases[i].val[j] = val[j];
            aliases[i].val[j] = '\0';
            aliases[i].used = 1;
            alias_persist();
            return 0;
        }
    }
    return -1;
}

void shell_alias_list(void) {
    for (int i = 0; i < ALIAS_MAX; i++) {
        if (!aliases[i].used)
            continue;
        console_write(aliases[i].key);
        console_write("='");
        console_write(aliases[i].val);
        console_write("'\n");
    }
}
