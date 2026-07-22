#ifdef PEAK_HOST_TEST
#include "../include/http_util.h"
#include <string.h>
#include <stdio.h>
#else
#include "http_util.h"
#include "util.h"
#endif

int http_normalize_path(const char *in, char *out, size_t out_cap) {
    if (!in || !out || out_cap < 2)
        return -1;
    if (in[0] != '/')
        return -1;

    char parts[32][64];
    int depth = 0;
    size_t i = 1;
    while (in[i] && depth < 32) {
        while (in[i] == '/')
            i++;
        if (!in[i] || in[i] == '?' || in[i] == '#')
            break;
        char part[64];
        size_t j = 0;
        while (in[i] && in[i] != '/' && in[i] != '?' && in[i] != '#' &&
               j + 1 < sizeof(part))
            part[j++] = in[i++];
        part[j] = '\0';
        if (!j || !strcmp(part, "."))
            continue;
        if (!strcmp(part, "..")) {
            if (depth > 0)
                depth--;
            else
                return -1; /* escape above root */
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
        size_t pl = strlen(parts[p]);
        if (o + 1 + pl + 1 >= out_cap)
            return -1;
        out[o++] = '/';
        memcpy(out + o, parts[p], pl);
        o += pl;
    }
    out[o] = '\0';
    return 0;
}

const char *http_mime_for_path(const char *path) {
    if (!path)
        return "application/octet-stream";
    const char *dot = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '.')
            dot = p;
    if (!dot)
        return "application/octet-stream";
    if (!strcmp(dot, ".html") || !strcmp(dot, ".htm"))
        return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css"))
        return "text/css; charset=utf-8";
    if (!strcmp(dot, ".js"))
        return "application/javascript";
    if (!strcmp(dot, ".json"))
        return "application/json";
    if (!strcmp(dot, ".txt") || !strcmp(dot, ".md"))
        return "text/plain; charset=utf-8";
    if (!strcmp(dot, ".png"))
        return "image/png";
    if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg"))
        return "image/jpeg";
    if (!strcmp(dot, ".gif"))
        return "image/gif";
    if (!strcmp(dot, ".svg"))
        return "image/svg+xml";
    if (!strcmp(dot, ".ico"))
        return "image/x-icon";
    return "application/octet-stream";
}

int http_parse_request_line(const char *req, char *method, size_t method_cap,
                            char *path, size_t path_cap) {
    if (!req || !method || !path || method_cap < 2 || path_cap < 2)
        return -1;
    method[0] = '\0';
    path[0] = '\0';
    const char *p = req;
    while (*p == ' ' || *p == '\t')
        p++;
    size_t mi = 0;
    while (*p && *p != ' ' && *p != '\t' && mi + 1 < method_cap)
        method[mi++] = *p++;
    method[mi] = '\0';
    if (!mi)
        return -1;
    while (*p == ' ' || *p == '\t')
        p++;
    size_t pi = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
           pi + 1 < path_cap)
        path[pi++] = *p++;
    path[pi] = '\0';
    if (!pi)
        return -1;
    return 0;
}

static const char *find_substr(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0])
        return hay;
    size_t n = 0;
    while (needle[n])
        n++;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] == needle[i])
            i++;
        if (i == n)
            return p;
    }
    return NULL;
}

static char *find_last_char(char *s, char ch) {
    char *last = NULL;
    if (!s)
        return NULL;
    for (; *s; s++)
        if (*s == ch)
            last = s;
    return last;
}

static int parse_url_parts(const char *url, char *scheme, size_t scheme_cap, char *host,
                           size_t host_cap, uint16_t *port_out) {
    if (!url || !scheme || !host || !port_out)
        return -1;
    scheme[0] = host[0] = '\0';
    const char *p = url;
    const char *colon = find_substr(p, "://");
    if (!colon || (size_t)(colon - p) + 1 >= scheme_cap)
        return -1;
    size_t sl = (size_t)(colon - p);
    memcpy(scheme, p, sl);
    scheme[sl] = '\0';
    p = colon + 3;
    uint16_t port = 0;
    if (!strcmp(scheme, "http"))
        port = 80;
    else if (!strcmp(scheme, "https"))
        port = 443;
    else
        return -1;
    size_t hi = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':' &&
           hi + 1 < host_cap)
        host[hi++] = *p++;
    host[hi] = '\0';
    if (!hi)
        return -1;
    if (*p == ':') {
        p++;
        port = 0;
        while (*p >= '0' && *p <= '9') {
            port = (uint16_t)(port * 10 + (uint16_t)(*p - '0'));
            p++;
        }
    }
    *port_out = port;
    return 0;
}

int http_parse_origin(const char *url, char *origin, size_t cap) {
    if (!url || !origin || cap < 12)
        return -1;
    char scheme[12], host[128];
    uint16_t port;
    if (parse_url_parts(url, scheme, sizeof(scheme), host, sizeof(host), &port) != 0)
        return -1;
    snprintf(origin, cap, "%s://%s:%u", scheme, host, (unsigned)port);
    return 0;
}

int http_same_origin(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    char oa[192], ob[192];
    if (http_parse_origin(a, oa, sizeof(oa)) != 0)
        return 0;
    if (http_parse_origin(b, ob, sizeof(ob)) != 0)
        return 0;
    return !strcmp(oa, ob);
}

int http_cors_allows(const char *page_origin, const char *resp_acao, int credentialed) {
    if (!resp_acao || !resp_acao[0])
        return 0;
    while (*resp_acao == ' ' || *resp_acao == '\t')
        resp_acao++;
    if (resp_acao[0] == '*' && resp_acao[1] == '\0')
        return credentialed ? 0 : 1;
    if (!page_origin)
        return 0;
    char oa[192];
    if (http_parse_origin(page_origin, oa, sizeof(oa)) != 0)
        return 0;
    char acao[192];
    size_t i = 0;
    while (resp_acao[i] && resp_acao[i] != '\r' && resp_acao[i] != '\n' &&
           i + 1 < sizeof(acao)) {
        acao[i] = resp_acao[i];
        i++;
    }
    acao[i] = '\0';
    while (i > 0 && (acao[i - 1] == ' ' || acao[i - 1] == '\t'))
        acao[--i] = '\0';
    return !strcmp(oa, acao);
}

int http_resolve_url(const char *base, const char *rel, char *out, size_t out_cap) {
    if (!base || !rel || !out || out_cap < 8)
        return -1;
    if (!strncmp(rel, "http://", 7) || !strncmp(rel, "https://", 8)) {
        snprintf(out, out_cap, "%s", rel);
        return 0;
    }
    char scheme[12], host[128];
    uint16_t port;
    if (parse_url_parts(base, scheme, sizeof(scheme), host, sizeof(host), &port) != 0)
        return -1;
    char path[256];
    const char *bp = find_substr(base, "://");
    if (!bp)
        return -1;
    bp += 3;
    while (*bp && *bp != '/')
        bp++;
    if (*bp)
        snprintf(path, sizeof(path), "%s", bp);
    else
        snprintf(path, sizeof(path), "/");
    if (rel[0] == '/') {
        snprintf(out, out_cap, "%s://%s:%u%s", scheme, host, (unsigned)port, rel);
        return 0;
    }
    if (rel[0] == '?' || rel[0] == '#') {
        char dir[256];
        snprintf(dir, sizeof(dir), "%s", path);
        char *q = strchr(dir, '?');
        if (q)
            *q = '\0';
        q = strchr(dir, '#');
        if (q)
            *q = '\0';
        char *slash = find_last_char(dir, '/');
        if (slash)
            slash[1] = '\0';
        else
            snprintf(dir, sizeof(dir), "/");
        snprintf(out, out_cap, "%s://%s:%u%s%s", scheme, host, (unsigned)port, dir, rel);
        return 0;
    }
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", path);
    char *q = strchr(dir, '?');
    if (q)
        *q = '\0';
    q = strchr(dir, '#');
    if (q)
        *q = '\0';
    char *slash = find_last_char(dir, '/');
    if (slash)
        slash[1] = '\0';
    else
        snprintf(dir, sizeof(dir), "/");
    char combined[320];
    snprintf(combined, sizeof(combined), "%s%s", dir, rel);
    if (combined[0] != '/')
        return -1;
    char norm[256];
    if (http_normalize_path(combined, norm, sizeof(norm)) != 0)
        return -1;
    snprintf(out, out_cap, "%s://%s:%u%s", scheme, host, (unsigned)port, norm);
    return 0;
}
