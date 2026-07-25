#include "shell.h"
#include "console.h"
#include "peak_errno.h"
#include "util.h"
#include "vfs.h"

#define SHELL_HIST_MAX   64
#define SHELL_HIST_LINE  256
#define SHELL_HIST_PATH  "/var/peak/history"

static char hist[SHELL_HIST_MAX][SHELL_HIST_LINE];
static int hist_count;
static int hist_browse; /* index into hist[], or -1 when editing fresh line */

static void hist_trim(void) {
    if (hist_count <= SHELL_HIST_MAX)
        return;
    memmove(hist, hist + 1, (size_t)(SHELL_HIST_MAX - 1) * SHELL_HIST_LINE);
    hist_count = SHELL_HIST_MAX;
}

static void hist_persist(void) {
    (void)vfs_mkdir("/var/peak");
    char buf[SHELL_HIST_MAX * SHELL_HIST_LINE];
    size_t o = 0;
    for (int i = 0; i < hist_count && o + 2 < sizeof(buf); i++) {
        size_t n = strlen(hist[i]);
        if (o + n + 2 >= sizeof(buf))
            break;
        memcpy(buf + o, hist[i], n);
        o += n;
        buf[o++] = '\n';
    }
    if (o)
        vfs_write_file(SHELL_HIST_PATH, buf, o);
}

void shell_history_init(void) {
    hist_count = 0;
    hist_browse = -1;
    memset(hist, 0, sizeof(hist));
    (void)vfs_mkdir("/var/peak");
    char buf[SHELL_HIST_MAX * SHELL_HIST_LINE];
    size_t n = 0;
    if (vfs_read_file(SHELL_HIST_PATH, buf, sizeof(buf) - 1, &n) != 0 || !n)
        return;
    buf[n] = '\0';
    const char *p = buf;
    while (*p && hist_count < SHELL_HIST_MAX) {
        while (*p == '\n')
            p++;
        if (!*p)
            break;
        size_t i = 0;
        while (*p && *p != '\n' && i + 1 < SHELL_HIST_LINE)
            hist[hist_count][i++] = *p++;
        hist[hist_count][i] = '\0';
        if (i)
            hist_count++;
        while (*p && *p != '\n')
            p++;
    }
}

void shell_history_add(const char *line) {
    if (!line || !line[0])
        return;
    while (*line == ' ')
        line++;
    if (!*line)
        return;
    if (hist_count > 0 && !strcmp(hist[hist_count - 1], line))
        return;
    if (hist_count >= SHELL_HIST_MAX)
        hist_trim();
    size_t i = 0;
    for (; line[i] && i + 1 < SHELL_HIST_LINE; i++)
        hist[hist_count][i] = line[i];
    hist[hist_count][i] = '\0';
    hist_count++;
    hist_browse = -1;
    hist_persist();
}

const char *shell_history_last(void) {
    if (hist_count <= 0)
        return NULL;
    return hist[hist_count - 1];
}

const char *shell_history_get(int one_based) {
    if (one_based < 1 || one_based > hist_count)
        return NULL;
    return hist[one_based - 1];
}

int shell_history_prev(char *line, size_t line_cap) {
    if (!line || line_cap < 2 || hist_count <= 0)
        return 0;
    if (hist_browse < 0)
        hist_browse = hist_count;
    if (hist_browse <= 0)
        return 0;
    hist_browse--;
    size_t i = 0;
    for (; hist[hist_browse][i] && i + 1 < line_cap; i++)
        line[i] = hist[hist_browse][i];
    line[i] = '\0';
    return 1;
}

int shell_history_next(char *line, size_t line_cap) {
    if (!line || line_cap < 2 || hist_count <= 0 || hist_browse < 0)
        return 0;
    if (hist_browse + 1 >= hist_count) {
        hist_browse = -1;
        line[0] = '\0';
        return 1;
    }
    hist_browse++;
    size_t i = 0;
    for (; hist[hist_browse][i] && i + 1 < line_cap; i++)
        line[i] = hist[hist_browse][i];
    line[i] = '\0';
    return 1;
}

void shell_history_reset_browse(void) {
    hist_browse = -1;
}

void shell_history_list(void) {
    for (int i = 0; i < hist_count; i++) {
        char num[8];
        itoa_u((uint64_t)(i + 1), num, 10);
        console_write(num);
        console_write("  ");
        console_write(hist[i]);
        console_putc('\n');
    }
}
