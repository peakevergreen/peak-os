#include "ctr.h"
#include "ctr_internal.h"
#include "net.h"
#include "privacy.h"
#include "vfs.h"
#include "util.h"

struct ctr_container containers[CTR_MAX_CONTAINERS];
int ctr_ready;

/* Most recently built image this boot, so `ctr run` needs no arguments. */
char last_image[CTR_TAG_MAX];
char last_port[16];

uint16_t ctr_parse_port(const char *port) {
    uint32_t v = 0;
    if (!port || !port[0])
        return 0;
    for (const char *p = port; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        v = v * 10u + (uint32_t)(*p - '0');
        if (v > 65535)
            return 0;
    }
    return (uint16_t)v;
}

struct ctr_container *ctr_find_by_name(const char *name) {
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++) {
        if (containers[i].used && !strcmp(containers[i].name, name))
            return &containers[i];
    }
    return NULL;
}

struct ctr_container *ctr_find_by_port(const char *port) {
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++) {
        if (containers[i].used && containers[i].running &&
            !strcmp(containers[i].port, port))
            return &containers[i];
    }
    return NULL;
}

struct ctr_container *ctr_alloc_slot(void) {
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++) {
        if (!containers[i].used)
            return &containers[i];
    }
    return NULL;
}

void ctr_init(void) {
    if (ctr_ready)
        return;
    vfs_mkdir("/var/lib");
    vfs_mkdir("/var/lib/peak-ctr");
    vfs_mkdir(CTR_IMG_ROOT);
    memset(containers, 0, sizeof(containers));
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++)
        containers[i].listen_id = -1;
    ctr_ready = 1;
}

int ctr_run(const char *image, const char *name, const char *port,
            char *log, size_t log_cap) {
    ctr_init();
    if (log && log_cap)
        log[0] = '\0';
    if (!image || !name || !port)
        return -1;
    /* Explicit ctr run = localhost listen consent (not LAN). */
    privacy_set_listeners_localhost_only(1);
    privacy_grant_net_listen(0, 0);

    uint16_t port_num = ctr_parse_port(port);
    if (!port_num) {
        ctr_log_append(log, log_cap, "invalid port");
        return -1;
    }

    char rootfs[CTR_PATH_MAX];
    ctr_image_rootfs_path(image, rootfs, sizeof(rootfs));
    if (!vfs_is_dir(rootfs)) {
        ctr_log_append(log, log_cap, "image not found — run ctr build first");
        return -1;
    }

    if (ctr_find_by_port(port)) {
        struct ctr_container *busy = ctr_find_by_port(port);
        if (!name || strcmp(busy->name, name) != 0) {
            ctr_log_append(log, log_cap, "port already in use");
            return -1;
        }
    }

    struct ctr_container *c = ctr_find_by_name(name);
    if (!c) {
        c = ctr_alloc_slot();
        if (!c) {
            ctr_log_append(log, log_cap, "too many containers");
            return -1;
        }
        memset(c, 0, sizeof(*c));
        c->used = 1;
        c->listen_id = -1;
        snprintf(c->name, sizeof(c->name), "%s", name);
    }

    if (c->running && c->listen_id >= 0)
        net_tcp_unlisten(c->port_num);

    int lid = net_tcp_listen(port_num);
    if (lid < 0) {
        ctr_log_append(log, log_cap, "TCP listen failed");
        return -1;
    }

    snprintf(c->image, sizeof(c->image), "%s", image);
    snprintf(c->port, sizeof(c->port), "%s", port);
    snprintf(c->rootfs, sizeof(c->rootfs), "%s", rootfs);
    c->port_num = port_num;
    c->listen_id = lid;
    c->running = 1;
    c->log[0] = '\0';
    ctr_log_append(c->log, sizeof(c->log), "container started (in-guest Peak runtime)");

    struct net_info ni;
    net_get_info(&ni);
    char ipb[32];
    net_format_ip(ni.ip, ipb, sizeof(ipb));
    char msg[160];
    snprintf(msg, sizeof(msg), "listening on http://%s:%s/ (and 127.0.0.1)",
             ni.up && ni.ip ? ipb : "0.0.0.0", port);
    ctr_log_append(c->log, sizeof(c->log), msg);

    if (log && log_cap)
        snprintf(log, log_cap, "%s", c->log);
    return 0;
}

int ctr_stop(const char *name) {
    ctr_init();
    struct ctr_container *c = ctr_find_by_name(name);
    if (!c)
        return -1;
    if (c->running && c->port_num)
        net_tcp_unlisten(c->port_num);
    c->running = 0;
    c->listen_id = -1;
    ctr_log_append(c->log, sizeof(c->log), "stopped");
    return 0;
}

int ctr_ps(char *out, size_t out_cap) {
    ctr_init();
    if (!out || !out_cap)
        return -1;
    size_t o = 0;
    o += (size_t)snprintf(out + o, out_cap - o, "NAME\tIMAGE\tPORT\tSTATUS\n");
    for (int i = 0; i < CTR_MAX_CONTAINERS && o + 1 < out_cap; i++) {
        if (!containers[i].used)
            continue;
        const char *st = "Exited";
        if (containers[i].running) {
            st = net_tcp_listening(containers[i].port_num) ? "Up/listen" : "Up";
        }
        o += (size_t)snprintf(out + o, out_cap - o, "%s\t%s\t%s\t%s\n",
                              containers[i].name, containers[i].image,
                              containers[i].port, st);
    }
    return 0;
}

int ctr_logs(const char *name, char *out, size_t out_cap) {
    ctr_init();
    struct ctr_container *c = ctr_find_by_name(name);
    if (!c || !out || !out_cap)
        return -1;
    snprintf(out, out_cap, "%s", c->log);
    return 0;
}

int ctr_ping(char *out, size_t out_cap) {
    ctr_init();
    if (out && out_cap)
        snprintf(out, out_cap, "{\"ok\":true,\"runtime\":\"peak-in-guest\"}");
    return 0;
}
