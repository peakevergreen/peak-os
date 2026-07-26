/*
 * Host stubs for linking kernel/agent_policy.c, agent_tools.c, and
 * agent_planner.c under PEAK_HOST_TEST — in-memory VFS + PeakVec no-ops.
 */
#include "heap.h"
#include "vfs.h"
#include "peak_errno.h"
#include "peakvec.h"
#include "ubin.h"
#include "util.h"
#include "sysmon.h"
#include "privacy.h"
#include "net.h"
#include "timer.h"
#include "sched.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HOST_VFS_FILES 48

struct host_file {
    int used;
    char path[VFS_PATH_MAX];
    char *data;
    size_t len;
};

static struct host_file g_files[HOST_VFS_FILES];
static char g_dirs[16][VFS_PATH_MAX];
static int g_dir_count;
static int g_net_client_grant;

void agent_host_vfs_reset(void) {
    for (int i = 0; i < HOST_VFS_FILES; i++) {
        free(g_files[i].data);
        memset(&g_files[i], 0, sizeof(g_files[i]));
    }
    g_dir_count = 0;
    g_net_client_grant = 0;
}

static struct host_file *host_find(const char *path) {
    for (int i = 0; i < HOST_VFS_FILES; i++) {
        if (g_files[i].used && !strcmp(g_files[i].path, path))
            return &g_files[i];
    }
    return NULL;
}

static struct host_file *host_alloc(const char *path) {
    struct host_file *f = host_find(path);
    if (f)
        return f;
    for (int i = 0; i < HOST_VFS_FILES; i++) {
        if (!g_files[i].used) {
            size_t pl = strlen(path);
            if (pl + 1 > sizeof(g_files[i].path))
                return NULL;
            memcpy(g_files[i].path, path, pl + 1);
            g_files[i].used = 1;
            g_files[i].data = NULL;
            g_files[i].len = 0;
            return &g_files[i];
        }
    }
    return NULL;
}

void *kmalloc(size_t size) {
    return malloc(size ? size : 1);
}

void *kzalloc(size_t size) {
    return calloc(1, size ? size : 1);
}

void *krealloc(void *ptr, size_t size) {
    return realloc(ptr, size ? size : 1);
}

void kfree(void *ptr) {
    free(ptr);
}

uint64_t heap_total_allocated(void) {
    return 0;
}

int vfs_normalize(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len < 2 || path[0] != '/')
        return PEAK_EINVAL;
    char parts[32][VFS_NAME_MAX];
    int depth = 0;
    size_t i = 1;
    while (path[i] && depth < 32) {
        while (path[i] == '/')
            i++;
        if (!path[i])
            break;
        char part[VFS_NAME_MAX];
        size_t j = 0;
        while (path[i] && path[i] != '/' && j + 1 < VFS_NAME_MAX)
            part[j++] = path[i++];
        part[j] = '\0';
        if (!strcmp(part, ".") || j == 0)
            continue;
        if (!strcmp(part, "..")) {
            if (depth > 0)
                depth--;
            continue;
        }
        memcpy(parts[depth], part, j + 1);
        depth++;
    }
    if (depth == 0) {
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }
    size_t o = 0;
    for (int p = 0; p < depth; p++) {
        if (o + 1 >= out_len)
            return PEAK_ENOSPC;
        out[o++] = '/';
        for (size_t k = 0; parts[p][k]; k++) {
            if (o + 1 >= out_len)
                return PEAK_ENOSPC;
            out[o++] = parts[p][k];
        }
    }
    out[o] = '\0';
    return 0;
}

int vfs_write_file(const char *path, const void *data, size_t len) {
    struct host_file *f = host_alloc(path);
    if (!f)
        return PEAK_ENOSPC;
    char *buf = (char *)malloc(len ? len : 1);
    if (!buf)
        return PEAK_ENOMEM;
    if (len)
        memcpy(buf, data, len);
    free(f->data);
    f->data = buf;
    f->len = len;
    return 0;
}

int vfs_read_at(const char *path, size_t off, void *buf, size_t buf_len, size_t *out_len) {
    struct host_file *f = host_find(path);
    if (!f)
        return PEAK_ENOENT;
    if (off >= f->len) {
        if (out_len)
            *out_len = 0;
        return 0;
    }
    size_t avail = f->len - off;
    size_t n = avail < buf_len ? avail : buf_len;
    if (n && buf)
        memcpy(buf, f->data + off, n);
    if (out_len)
        *out_len = n;
    return 0;
}

int vfs_read_file(const char *path, void *buf, size_t buf_len, size_t *out_len) {
    return vfs_read_at(path, 0, buf, buf_len, out_len);
}

int vfs_stat(const char *path, struct vfs_stat *st) {
    struct host_file *f = host_find(path);
    if (!f || !st)
        return PEAK_ENOENT;
    st->type = VFS_FILE;
    st->mode = VFS_MODE_FILE;
    st->size = f->len;
    st->refs = 1;
    st->nchildren = 0;
    st->name[0] = '\0';
    return 0;
}
void vfs_mode_string(enum vfs_type type, uint16_t mode, char *buf, size_t len) {
    if (!buf || len < 11) return;
    buf[0] = (type == VFS_DIR) ? 'd' : '-';
    static const char *triples[] = { "---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx" };
    buf[1]=triples[(mode>>6)&7][0]; buf[2]=triples[(mode>>6)&7][1]; buf[3]=triples[(mode>>6)&7][2];
    buf[4]=triples[(mode>>3)&7][0]; buf[5]=triples[(mode>>3)&7][1]; buf[6]=triples[(mode>>3)&7][2];
    buf[7]=triples[mode&7][0]; buf[8]=triples[mode&7][1]; buf[9]=triples[mode&7][2]; buf[10]=0;
}
int vfs_chmod(const char *path, uint16_t mode) { (void)path; (void)mode; return 0; }

int vfs_list(const char *path, char *out, size_t out_len) {
    if (!out || out_len < 2)
        return PEAK_EINVAL;
    (void)path;
    out[0] = '\0';
    return 0;
}

struct vfs_node *vfs_mkdir(const char *path) {
    if (!path || g_dir_count >= 16)
        return NULL;
    size_t pl = strlen(path);
    if (pl + 1 > VFS_PATH_MAX)
        return NULL;
    memcpy(g_dirs[g_dir_count], path, pl + 1);
    g_dir_count++;
    return (struct vfs_node *)(uintptr_t)g_dir_count;
}

int vfs_is_dir(const char *path) {
    if (!path)
        return 0;
    for (int i = 0; i < g_dir_count; i++) {
        if (!strcmp(g_dirs[i], path))
            return 1;
    }
    return 0;
}

int vfs_unlink(const char *path) {
    struct host_file *f = host_find(path);
    if (!f)
        return PEAK_ENOENT;
    free(f->data);
    f->used = 0;
    f->data = NULL;
    f->len = 0;
    return 0;
}

int vfs_walk(const char *path, vfs_walk_cb cb, void *ctx) {
    if (!path || !cb)
        return PEAK_ENOENT;
    size_t pl = strlen(path);
    for (int i = 0; i < HOST_VFS_FILES; i++) {
        if (!g_files[i].used)
            continue;
        if (strncmp(g_files[i].path, path, pl) != 0)
            continue;
        if (g_files[i].path[pl] != '\0' && g_files[i].path[pl] != '/')
            continue;
        struct vfs_node node;
        memset(&node, 0, sizeof(node));
        node.type = VFS_FILE;
        node.size = g_files[i].len;
        cb(g_files[i].path, &node, ctx);
    }
    return 0;
}

struct vfs_node *vfs_lookup(const char *path) {
    (void)path;
    return NULL;
}

int vfs_remove_tree(const char *path) {
    if (!path)
        return PEAK_ENOENT;
    for (int i = 0; i < g_dir_count; i++) {
        if (!strcmp(g_dirs[i], path)) {
            memmove(g_dirs[i], g_dirs[i + 1],
                    (size_t)(g_dir_count - i - 1) * sizeof(g_dirs[0]));
            g_dir_count--;
            return 0;
        }
    }
    return PEAK_ENOENT;
}

/* Write-approval stub: treat as auto-approved path for host tool tests. */
int agent_queue_write_approval(const char *path, const char *content) {
    (void)path;
    (void)content;
    return -1;
}

void agent_transcript_note_audit(const char *op, const char *target, const char *decision) {
    (void)op;
    (void)target;
    (void)decision;
}

void agent_transcript_note_tool(const char *msg) {
    (void)msg;
}

void itoa_u(uint64_t val, char *buf, int base) {
    if (base != 10 && base != 16)
        base = 10;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[32];
    int i = 0;
    while (val > 0) {
        int d = (int)(val % (uint64_t)base);
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        val /= (uint64_t)base;
    }
    int j = 0;
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = '\0';
}

void peakvec_embed_text(const char *text, int16_t *out_vec) {
    (void)text;
    if (out_vec)
        memset(out_vec, 0, (size_t)PEAKVEC_DIM * sizeof(int16_t));
}

int peakvec_upsert(const char *ns, const char *key, const int16_t *vec, const char *meta) {
    (void)ns;
    (void)key;
    (void)vec;
    (void)meta;
    return 0;
}

int peakvec_query(const char *ns, const int16_t *query, int top_k, struct peakvec_hit *out) {
    (void)ns;
    (void)query;
    (void)top_k;
    (void)out;
    return 0;
}

uint32_t sysmon_now_us(void) {
    return 0;
}

static struct sysmon_sample g_sysmon_sample;

void sysmon_poll(void) {
    g_sysmon_sample.uptime_secs = 42;
    g_sysmon_sample.mem_pct = 50;
    g_sysmon_sample.heap_pct = 25;
    g_sysmon_sample.load_pct = 10;
    g_sysmon_sample.idle_pct = 90;
    g_sysmon_sample.tasks = 3;
    g_sysmon_sample.rx_bps = 1024;
    g_sysmon_sample.tx_bps = 512;
    g_sysmon_sample.vfs_nodes = 12;
    g_sysmon_sample.mem_used_pages = 32;
    g_sysmon_sample.heap_used = 4096;
    g_sysmon_sample.ctx_switches = 100;
    g_sysmon_sample.irq_count = 50;
    g_sysmon_sample.gui_fps = 60;
    g_sysmon_sample.surf_pressure = 30;
    g_sysmon_sample.compose_us = 1200;
    g_sysmon_sample.present_us = 500;
    g_sysmon_sample.peakvec_us = 100;
    g_sysmon_sample.agent_audit_us = 50;
}

const struct sysmon_sample *sysmon_latest(void) {
    return &g_sysmon_sample;
}

void sysmon_format_rate(uint32_t bps, char *buf, size_t cap) {
    if (!buf || cap == 0)
        return;
    snprintf(buf, cap, "%ubps", (unsigned)bps);
}

void sysmon_format_bytes(uint64_t n, char *buf, size_t cap) {
    if (!buf || cap == 0)
        return;
    snprintf(buf, cap, "%luB", (unsigned long)n);
}

void sysmon_format_us(uint32_t us, char *buf, size_t cap) {
    if (!buf || cap == 0)
        return;
    snprintf(buf, cap, "%uus", (unsigned)us);
}

void sysmon_note_agent_audit_us(uint32_t us) { (void)us; }
void sysmon_note_peakvec_us(uint32_t us) { (void)us; }

void privacy_init(void) { g_net_client_grant = 0; }
void privacy_clear_session(void) { g_net_client_grant = 0; }
int privacy_persist_profile(void) { return 0; }
void privacy_set_persist_profile(int profile) { (void)profile; }
static int g_http_rc = 0;
static int g_http_status = 200;
static char g_http_body[512] = "hello from fetch stub";

int net_http_get(const char *url, char *body, size_t body_cap, int *status_out) {
    (void)url;
    if (status_out)
        *status_out = g_http_status;
    if (body && body_cap)
        snprintf(body, body_cap, "%s", g_http_body);
    return g_http_rc;
}

int net_http_needs_tls(void) { return 0; }
const char *net_http_tls_reject_name(void) { return "fetch: tls-handshake"; }

int privacy_net_kill_switch(void) { return 0; }
void privacy_set_net_kill_switch(int on) { (void)on; }
int privacy_net_client_allowed(void) { return g_net_client_grant; }
void privacy_grant_net_client(int remember) { (void)remember; g_net_client_grant = 1; }
int privacy_net_listen_allowed(int lan) { (void)lan; return 0; }
void privacy_grant_net_listen(int lan, int remember) { (void)lan; (void)remember; }
int privacy_listeners_localhost_only(void) { return 1; }
void privacy_set_listeners_localhost_only(int on) { (void)on; }
int net_ready(void) { return 1; }
uint32_t net_dns_resolve(const char *hostname, uint32_t timeout_ticks) {
    (void)hostname; (void)timeout_ticks; return 0x08080808u;
}
int net_tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ticks) {
    (void)ip; (void)port; (void)timeout_ticks; return 0;
}
void net_tcp_close(void) {}
void net_format_ip(uint32_t ip, char *buf, size_t cap) {
    (void)ip; if (buf && cap) snprintf(buf, cap, "8.8.8.8");
}
const char *net_last_error(void) { return NULL; }
uint64_t timer_ticks(void) { return 10; }

/* agent_tools fs.exec resolves /bin commands via ubin; host tests only need linkage. */
int ubin_run(const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    return 0;
}

/* Pass 76 sys.ps — empty task table is enough for host policy/tool tests. */
int sched_list_tasks(struct task *out, int max) {
    (void)out;
    (void)max;
    return 0;
}

void sched_sort_tasks(struct task *list, int n) {
    (void)list;
    (void)n;
}
