/* VFS-backed browser bookmarks at /var/peak/bookmarks (title|url lines). */
#include "browser.h"
#include "browser_internal.h"
#include "util.h"
#include "vfs.h"

#define BR_BOOKMARK_MAX  16
#define BR_BOOKMARK_PATH "/var/peak/bookmarks"

static struct {
    char title[BR_TITLE_MAX];
    char url[BR_URL_MAX];
    int used;
} bookmarks[BR_BOOKMARK_MAX];

static void bookmarks_persist(void) {
    (void)vfs_mkdir("/var/peak");
    char buf[BR_BOOKMARK_MAX * (BR_TITLE_MAX + BR_URL_MAX + 4)];
    size_t o = 0;
    for (int i = 0; i < BR_BOOKMARK_MAX && o + 4 < sizeof(buf); i++) {
        if (!bookmarks[i].used)
            continue;
        size_t tn = strlen(bookmarks[i].title);
        size_t un = strlen(bookmarks[i].url);
        if (o + tn + un + 3 >= sizeof(buf))
            break;
        memcpy(buf + o, bookmarks[i].title, tn);
        o += tn;
        buf[o++] = '|';
        memcpy(buf + o, bookmarks[i].url, un);
        o += un;
        buf[o++] = '\n';
    }
    if (o)
        vfs_write_file(BR_BOOKMARK_PATH, buf, o);
    else
        vfs_write_file(BR_BOOKMARK_PATH, "", 0);
}

void browser_bookmarks_init(void) {
    memset(bookmarks, 0, sizeof(bookmarks));
    (void)vfs_mkdir("/var/peak");
    char buf[4096];
    size_t n = 0;
    if (vfs_read_file(BR_BOOKMARK_PATH, buf, sizeof(buf) - 1, &n) != 0 || !n)
        return;
    buf[n] = '\0';
    const char *p = buf;
    int slot = 0;
    while (*p && slot < BR_BOOKMARK_MAX) {
        while (*p == '\n')
            p++;
        if (!*p)
            break;
        char title[BR_TITLE_MAX], url[BR_URL_MAX];
        size_t ti = 0, ui = 0;
        while (*p && *p != '|' && *p != '\n' && ti + 1 < sizeof(title))
            title[ti++] = *p++;
        title[ti] = '\0';
        if (*p == '|')
            p++;
        while (*p && *p != '\n' && ui + 1 < sizeof(url))
            url[ui++] = *p++;
        url[ui] = '\0';
        if (url[0]) {
            if (!title[0])
                snprintf(title, sizeof(title), "%s", url);
            bookmarks[slot].used = 1;
            snprintf(bookmarks[slot].title, sizeof(bookmarks[slot].title), "%s", title);
            snprintf(bookmarks[slot].url, sizeof(bookmarks[slot].url), "%s", url);
            slot++;
        }
        while (*p && *p != '\n')
            p++;
    }
}

int browser_bookmark_count(void) {
    int n = 0;
    for (int i = 0; i < BR_BOOKMARK_MAX; i++)
        if (bookmarks[i].used)
            n++;
    return n;
}

const char *browser_bookmark_title(int idx) {
    int n = 0;
    for (int i = 0; i < BR_BOOKMARK_MAX; i++) {
        if (!bookmarks[i].used)
            continue;
        if (n == idx)
            return bookmarks[i].title;
        n++;
    }
    return NULL;
}

const char *browser_bookmark_url(int idx) {
    int n = 0;
    for (int i = 0; i < BR_BOOKMARK_MAX; i++) {
        if (!bookmarks[i].used)
            continue;
        if (n == idx)
            return bookmarks[i].url;
        n++;
    }
    return NULL;
}

int browser_bookmark_add(const char *url, const char *title) {
    if (!url || !url[0])
        return -1;
    for (int i = 0; i < BR_BOOKMARK_MAX; i++) {
        if (bookmarks[i].used && !strcmp(bookmarks[i].url, url)) {
            if (title && title[0])
                snprintf(bookmarks[i].title, sizeof(bookmarks[i].title), "%s", title);
            bookmarks_persist();
            return 0;
        }
    }
    for (int i = 0; i < BR_BOOKMARK_MAX; i++) {
        if (!bookmarks[i].used) {
            bookmarks[i].used = 1;
            snprintf(bookmarks[i].url, sizeof(bookmarks[i].url), "%s", url);
            if (title && title[0])
                snprintf(bookmarks[i].title, sizeof(bookmarks[i].title), "%s", title);
            else
                snprintf(bookmarks[i].title, sizeof(bookmarks[i].title), "%s", url);
            bookmarks_persist();
            return 0;
        }
    }
    return -1;
}

void browser_bookmark_go(int idx) {
    const char *url = browser_bookmark_url(idx);
    if (url)
        browser_go(url);
}
