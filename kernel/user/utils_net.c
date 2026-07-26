/* /bin network utilities: ifconfig, ping, wget, traceroute-lite, tlsinfo. */
#include "libpeak.h"
#include "cap.h"
#include "privacy.h"
#include "shell.h"
#include "console.h"
#include "net.h"
#include "http_util.h"
#include "tls.h"
#include "tls_util.h"
#include "tls_session.h"
#include "webpki.h"
#include "settings.h"
#include "random.h"
#include "timer.h"
#include "util.h"

#define HTTP_BODY_MAX 32768
#include "vfs.h"

static void net_print_failure(const char *tool, const char *what) {
    const char *detail = net_last_error();
    if (detail && detail[0])
        console_printf("%s: %s: %s\n", tool, what, detail);
    else
        peak_perror(tool, what);
}

static int net_tcp_probe(uint32_t ip, uint16_t port, uint32_t timeout_ticks,
                         unsigned long *ms_out) {
    uint64_t t0 = timer_ticks();
    int cr = net_tcp_connect(ip, port, timeout_ticks);
    uint64_t dt = timer_ticks() - t0;
    if (ms_out)
        *ms_out = (unsigned long)(dt * 10);
    if (cr == 0)
        net_tcp_close();
    return cr;
}

static void wget_print_failure(int st, const char *body) {
    const char *tls_err = tls_last_error();
    const char *reject = net_http_tls_reject_name();
    const char *detail = net_last_error();
    if (tls_err && tls_err[0]) {
        console_printf("failed: TLS %s", tls_err);
        if (reject && reject[0])
            console_printf(" [%s]", reject);
        if (st > 0)
            console_printf(" (HTTP %d)", st);
        console_write("\n");
    } else if (reject && reject[0] && net_http_needs_tls()) {
        console_printf("failed: %s", reject);
        if (detail && detail[0])
            console_printf(" — %s", detail);
        if (st > 0)
            console_printf(" (HTTP %d)", st);
        console_write("\n");
    } else if (st > 0) {
        console_printf("failed: HTTP %d\n", st);
    } else if (detail && detail[0]) {
        console_printf("failed: %s (status %d)\n", detail, st);
    } else {
        console_printf("failed: connect/DNS/TLS error (status %d)\n", st);
    }
    if (body && body[0]) {
        console_write(body);
        console_write("\n");
    }
}

static int dns_lookup_aaaa_dig(const char *tool, const char *hostname, int verbose) {
    struct net_info ni;
    net_get_info(&ni);
    char dns[32];
    net_format_ip(ni.dns, dns, sizeof(dns));
    uint64_t t0 = timer_ticks();
    uint8_t addr[16];
    int rc = net_dns_resolve_aaaa(hostname, 400, addr);
    unsigned long qms = (unsigned long)((timer_ticks() - t0) * 10);
    if (rc != 0) {
        if (verbose) {
            console_printf(";; <<>> Peak %s %s <<>>\n", tool, hostname);
            console_printf(";; SERVER: %s#53\n", dns);
            console_printf(";; QUESTION SECTION:\n");
            console_printf(";%s.\t\tIN\tAAAA\n\n", hostname);
            console_printf(";; status: NOAAAA (Peak is IPv4-only for routing)\n");
        }
        net_print_failure(tool, "AAAA lookup failed");
        return 1;
    }
    char v6[64];
    net_format_ipv6(addr, v6, sizeof(v6));
    if (verbose) {
        console_printf(";; <<>> Peak %s %s <<>>\n", tool, hostname);
        console_printf(";; SERVER: %s#53\n", dns);
        console_printf(";; Query time: %lu msec\n\n", qms);
        console_printf(";; QUESTION SECTION:\n");
        console_printf(";%s.\t\tIN\tAAAA\n\n", hostname);
        console_printf(";; ANSWER SECTION:\n");
        console_printf("%s.\t\t30\tIN\tAAAA\t%s\n", hostname, v6);
        console_write(";; NOTE: AAAA shown for diagnostics; stack routes IPv4 only\n");
    } else {
        console_printf("%s has AAAA address %s\n", hostname, v6);
    }
    return 0;
}

static int dns_lookup_ptr_dig(const char *tool, const char *ipstr, int verbose) {
    uint32_t ip = net_dns_resolve(ipstr, 100);
    if (!ip) {
        /* parse dotted quad without DNS forward lookup */
        int dots = 0, ok = 1;
        uint32_t parts[4] = {0};
        int pi = 0;
        for (const char *p = ipstr; *p; p++) {
            if (*p == '.') {
                dots++;
                pi++;
            } else if (*p >= '0' && *p <= '9')
                parts[pi] = parts[pi] * 10 + (uint32_t)(*p - '0');
            else
                ok = 0;
        }
        if (ok && dots == 3)
            ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    }
    if (!ip) {
        peak_perror(tool, "need dotted IPv4 for -x");
        return 1;
    }
    struct net_info ni;
    net_get_info(&ni);
    char dns[32], rev[32];
    net_format_ip(ni.dns, dns, sizeof(dns));
    net_format_ip(ip, rev, sizeof(rev));
    char name[256];
    uint64_t t0 = timer_ticks();
    if (net_dns_reverse_ptr(ip, 400, name, sizeof(name)) != 0) {
        if (verbose)
            console_printf(";; reverse lookup %s failed\n", rev);
        net_print_failure(tool, "PTR lookup failed");
        return 1;
    }
    unsigned long qms = (unsigned long)((timer_ticks() - t0) * 10);
    if (verbose) {
        console_printf(";; <<>> Peak %s -x %s <<>>\n", tool, rev);
        console_printf(";; SERVER: %s#53\n", dns);
        console_printf(";; Query time: %lu msec\n\n", qms);
        console_printf(";; QUESTION SECTION:\n");
        console_printf(";%u.%u.%u.%u.in-addr.arpa.\tIN\tPTR\n\n",
                         (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                         (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
        console_printf(";; ANSWER SECTION:\n");
        console_printf("%s.\t30\tIN\tPTR\t%s.\n", rev, name);
    } else
        console_printf("%s domain name pointer %s.\n", rev, name);
    return 0;
}

static int dns_lookup_dig(const char *tool, const char *hostname, int verbose) {
    struct net_info ni;
    net_get_info(&ni);
    char dns[32];
    net_format_ip(ni.dns, dns, sizeof(dns));
    uint64_t t0 = timer_ticks();
    uint32_t ip = net_dns_resolve(hostname, 400);
    unsigned long qms = (unsigned long)((timer_ticks() - t0) * 10);
    if (!ip) {
        if (verbose) {
            console_printf(";; <<>> Peak %s %s <<>>\n", tool, hostname);
            console_printf(";; SERVER: %s#53\n", dns);
            console_printf(";; QUESTION SECTION:\n");
            console_printf(";%s.\t\tIN\tA\n\n", hostname);
            if (net_dns_last_negative_cached())
                console_printf(";; status: NXDOMAIN (cached negative, ~10s TTL)\n");
            else if (net_last_error_code() == PEAK_ETIMEOUT)
                console_printf(";; status: TIMEOUT (fresh query)\n");
            else
                console_printf(";; status: NXDOMAIN (fresh query)\n");
        } else if (net_dns_last_negative_cached()) {
            console_printf(";; NXDOMAIN cached for %s\n", hostname);
        }
        net_print_failure(tool, "DNS failed");
        return 1;
    }
    char addr[32];
    net_format_ip(ip, addr, sizeof(addr));
    if (verbose) {
        console_printf(";; <<>> Peak %s %s <<>>\n", tool, hostname);
        console_printf(";; SERVER: %s#53\n", dns);
        console_printf(";; Query time: %lu msec\n\n", qms);
        console_printf(";; QUESTION SECTION:\n");
        console_printf(";%s.\t\tIN\tA\n\n", hostname);
        console_printf(";; ANSWER SECTION:\n");
        console_printf("%s.\t\t30\tIN\tA\t%s\n", hostname, addr);
    } else {
        console_printf("%s.\t30\tIN\tA\t%s\n", hostname, addr);
        console_printf(";; Query time: %lu msec; SERVER: %s#53\n", qms, dns);
    }
    return 0;
}

int uifconfig_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct net_info ni;
    net_get_info(&ni);
    if (!ni.up) {
        console_write("e1000: down\n");
        return 1;
    }
    char ip[32], mask[32], gw[32], dns[32];
    net_format_ip(ni.ip, ip, sizeof(ip));
    net_format_ip(ni.mask, mask, sizeof(mask));
    net_format_ip(ni.gw, gw, sizeof(gw));
    net_format_ip(ni.dns, dns, sizeof(dns));
    console_printf("%s: flags=UP\n", ni.driver);
    console_printf("  ether %x:%x:%x:%x:%x:%x\n",
                   ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5]);
    console_printf("  inet %s  netmask %s  (%s)\n", ip, mask,
                   ni.addr_mode ? ni.addr_mode : "?");
    if (ni.gw)
        console_printf("  default route via %s\n", gw);
    else
        console_write("  default route: (none)\n");
    if (ni.dns)
        console_printf("  nameserver %s  (A-cache ~30s)\n", dns);
    else
        console_write("  nameserver: (none)\n");
    {
        uint32_t rf = random_status_flags();
        console_printf("  rng flags=0x%x%s%s%s%s\n",
                       (unsigned)rf,
                       (rf & RANDOM_READY_CRYPTO) ? " CRYPTO" : "",
                       (rf & RANDOM_READY_ANY) ? " ANY" : "",
                       (rf & RANDOM_FLAG_WEAK) ? " WEAK" : "",
                       (rf & RANDOM_FLAG_HW) ? " HW" : "");
    }
    return 0;
}

int uping_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("ping", "<host>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("ping", "network down");
        return 1;
    }
    uint32_t ip = net_dns_resolve(argv[1], 300);
    if (!ip) {
        net_print_failure("ping", "DNS failed");
        return 1;
    }
    char buf[32];
    net_format_ip(ip, buf, sizeof(buf));
    console_printf("PING %s (%s)\n", argv[1], buf);
    uint64_t t0 = timer_ticks();
    int cr = net_tcp_connect(ip, 80, 300);
    uint64_t dt = timer_ticks() - t0;
    if (cr == 0) {
        net_tcp_close();
        console_printf("tcp/:80 open from %s time=%lums\n", buf, (unsigned long)(dt * 10));
        return 0;
    }
    const char *why = net_last_error();
    if (why && why[0])
        console_printf("tcp/:80 failed from %s: %s\n", buf, why);
    else
        console_printf("tcp/:80 no response from %s (%s)\n", buf, peak_strerror(cr));
    console_printf("DNS ok - stack is talking to the network.\n");
    return 1;
}

int uwget_main(int argc, char **argv) {
    privacy_grant_net_client(0);
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("wget", "[-X METHOD] [-d data] [-i] [-O path] <url>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("wget", "network down");
        return 1;
    }
    const char *url = 0;
    const char *out_path = 0;
    const char *method = "GET";
    const char *post_body = 0;
    char hdr_extra[128];
    hdr_extra[0] = '\0';
    int show_headers = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-O") && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "-X") && i + 1 < argc) {
            method = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            post_body = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "-i")) {
            show_headers = 1;
            continue;
        }
        if (argv[i][0] != '-')
            url = argv[i];
    }
    if (!url) {
        peak_usage("wget", "[-X METHOD] [-d data] [-i] [-O path] <url>");
        return 1;
    }
    char body[HTTP_BODY_MAX];
    char hdrs[2048];
    int st = 0;
    struct net_http_request req;
    memset(&req, 0, sizeof(req));
    if (post_body && (!method[0] || !strcmp(method, "GET")))
        method = "POST";
    snprintf(req.method, sizeof(req.method), "%.7s", method ? method : "GET");
    req.url = url;
    req.body = post_body;
    req.body_len = post_body ? strlen(post_body) : 0;
    if (post_body)
        snprintf(hdr_extra, sizeof(hdr_extra),
                 "Content-Type: application/x-www-form-urlencoded");
    req.headers = hdr_extra[0] ? hdr_extra : NULL;
    console_printf("%s %s\n", req.method, url);
    console_write("fetching...\n");
    if (net_http_request(&req, body, sizeof(body), &st, hdrs, sizeof(hdrs)) != 0) {
        wget_print_failure(st, body);
        return 1;
    }
    if (net_http_last_h2())
        console_printf("HTTP/2 %d\n", st);
    else
        console_printf("HTTP %d\n", st);
    if (show_headers && hdrs[0]) {
        console_write(hdrs);
    } else {
        char ct[128];
        if (http_header_value(hdrs, "content-type", ct, sizeof(ct)) == 0)
            console_printf("  Content-Type: %s\n", ct);
    }
    console_printf("  %lu bytes", (unsigned long)strlen(body));
    if (net_http_last_body_truncated()) {
        console_printf(" [truncated: received=%lu limit=%lu policy=client-buffer]",
                       (unsigned long)net_http_last_body_total(),
                       (unsigned long)sizeof(body));
    }
    console_write("\n");
    if (out_path) {
        char abs[256];
        if (shell_resolve_path(out_path, abs, sizeof(abs))) {
            peak_perror("wget", "bad -O path");
            return 1;
        }
        if (vfs_write_file(abs, body, strlen(body)) != 0) {
            peak_perror("wget", "cannot write -O file");
            return 1;
        }
        console_printf("saved %s\n", abs);
        return 0;
    }
    size_t show = strlen(body);
    if (show > 1500)
        show = 1500;
    for (size_t i = 0; i < show; i++)
        console_putc(body[i]);
    if (strlen(body) > show)
        console_write("\n... truncated ...\n");
    else
        console_write("\n");
    return 0;
}

int ucurl_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("curl", "[-X METHOD] [-d data] [-i] [-o path] <url>");
        return argc < 2 ? 1 : 0;
    }
    char *av[16];
    int ac = 0;
    av[ac++] = (char *)"wget";
    for (int i = 1; i < argc && ac < 15; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            av[ac++] = (char *)"-O";
            av[ac++] = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "-X") && i + 1 < argc) {
            av[ac++] = (char *)"-X";
            av[ac++] = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            av[ac++] = (char *)"-d";
            av[ac++] = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "-i")) {
            av[ac++] = (char *)"-i";
            continue;
        }
        av[ac++] = argv[i];
    }
    av[ac] = 0;
    return uwget_main(ac, av);
}

int unslookup_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("nslookup", "[-6|-x] <host|ip>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("nslookup", "network down");
        return 1;
    }
    int want_aaaa = 0;
    int want_ptr = 0;
    const char *target = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-6")) {
            want_aaaa = 1;
            continue;
        }
        if (!strcmp(argv[i], "-x")) {
            want_ptr = 1;
            continue;
        }
        if (argv[i][0] != '-')
            target = argv[i];
    }
    if (!target) {
        peak_usage("nslookup", "[-6|-x] <host|ip>");
        return 1;
    }
    if (want_ptr)
        return dns_lookup_ptr_dig("nslookup", target, 1);
    if (want_aaaa)
        return dns_lookup_aaaa_dig("nslookup", target, 1);
    return dns_lookup_dig("nslookup", target, 1);
}

int uhost_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("host", "[-6|-x] <host|ip>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("host", "network down");
        return 1;
    }
    int want_aaaa = 0;
    int want_ptr = 0;
    const char *target = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-6")) {
            want_aaaa = 1;
            continue;
        }
        if (!strcmp(argv[i], "-x")) {
            want_ptr = 1;
            continue;
        }
        if (argv[i][0] != '-')
            target = argv[i];
    }
    if (!target) {
        peak_usage("host", "[-6|-x] <host|ip>");
        return 1;
    }
    if (want_ptr)
        return dns_lookup_ptr_dig("host", target, 0);
    if (want_aaaa)
        return dns_lookup_aaaa_dig("host", target, 0);
    return dns_lookup_dig("host", target, 0);
}

int utraceroute_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("traceroute", "<host> [-m max_hops]");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("traceroute", "network down");
        return 1;
    }
    const char *host = argv[1];
    int max_hops = 4;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            max_hops = peak_atoi(argv[++i]);
            if (max_hops < 2)
                max_hops = 2;
            if (max_hops > 8)
                max_hops = 8;
        }
    }
    struct net_info ni;
    net_get_info(&ni);
    char lip[32], gw[32], dest[32];
    net_format_ip(ni.ip, lip, sizeof(lip));
    net_format_ip(ni.gw, gw, sizeof(gw));
    console_printf("traceroute to %s, %d hops max, TCP :80\n", host, max_hops);
    int hop = 1;
    int failed = 0;
    if (hop <= max_hops) {
        console_printf("%2d  %s  local  <1 ms\n", hop, lip);
        hop++;
    }
    if (hop <= max_hops) {
        if (ni.gw) {
            unsigned long ms = 0;
            int pr = net_tcp_probe(ni.gw, 80, 200, &ms);
            if (pr == 0)
                console_printf("%2d  %s  gateway  %lu ms  open\n", hop, gw, ms);
            else {
                const char *why = net_last_error();
                if (why && why[0])
                    console_printf("%2d  %s  gateway  %lu ms  %s\n", hop, gw, ms, why);
                else
                    console_printf("%2d  %s  gateway  %lu ms  no reply\n", hop, gw, ms);
            }
        } else {
            console_printf("%2d  *  no default gateway\n", hop);
        }
        hop++;
    }
    uint32_t dest_ip = 0;
    if (hop <= max_hops) {
        uint64_t t0 = timer_ticks();
        dest_ip = net_dns_resolve(host, 400);
        unsigned long ms = (unsigned long)((timer_ticks() - t0) * 10);
        if (dest_ip) {
            net_format_ip(dest_ip, dest, sizeof(dest));
            console_printf("%2d  %s  dns  %lu ms  resolved\n", hop, dest, ms);
        } else {
            net_print_failure("traceroute", "DNS failed");
            failed = 1;
        }
        hop++;
    } else {
        dest_ip = net_dns_resolve(host, 400);
        if (dest_ip)
            net_format_ip(dest_ip, dest, sizeof(dest));
        else
            failed = 1;
    }
    if (!failed && dest_ip && hop <= max_hops) {
        unsigned long ms = 0;
        int pr = net_tcp_probe(dest_ip, 80, 300, &ms);
        if (pr == 0)
            console_printf("%2d  %s  destination  %lu ms  open\n", hop, dest, ms);
        else {
            const char *why = net_last_error();
            if (why && why[0])
                console_printf("%2d  %s  destination  %lu ms  %s\n", hop, dest, ms, why);
            else
                console_printf("%2d  %s  destination  %lu ms  closed\n", hop, dest, ms);
            failed = 1;
        }
        hop++;
    } else if (!failed && dest_ip) {
        unsigned long ms = 0;
        if (net_tcp_probe(dest_ip, 80, 300, &ms) != 0)
            failed = 1;
    }
    while (hop <= max_hops) {
        console_printf("%2d  *  hop timeout / loss (not probed)\n", hop);
        hop++;
    }
    return failed ? 1 : 0;
}

static int nc_parse_target(int argc, char **argv, uint32_t *ip, uint16_t *port) {
    char hostbuf[96];
    int port_i = 0;
    hostbuf[0] = '\0';
    if (argc >= 3 && argv[1][0] != '-') {
        size_t hl = strlen(argv[1]);
        if (hl >= sizeof(hostbuf))
            return -1;
        memcpy(hostbuf, argv[1], hl + 1);
        port_i = peak_atoi(argv[2]);
    } else if (argc >= 2) {
        const char *s = argv[1];
        const char *colon = s;
        while (*colon && *colon != ':')
            colon++;
        if (!*colon || colon == s)
            return -1;
        size_t hl = (size_t)(colon - s);
        if (hl >= sizeof(hostbuf))
            return -1;
        memcpy(hostbuf, s, hl);
        hostbuf[hl] = '\0';
        port_i = peak_atoi(colon + 1);
    } else {
        return -1;
    }
    if (!hostbuf[0] || port_i <= 0 || port_i >= 65536)
        return -1;
    if (!net_ready())
        return -2;
    *ip = net_dns_resolve(hostbuf, 400);
    *port = (uint16_t)port_i;
    return *ip ? 0 : -1;
}

static int nc_listen_mode(int argc, char **argv) {
    uint16_t port = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l") && i + 1 < argc)
            port = (uint16_t)peak_atoi(argv[++i]);
    }
    if (!port) return 0;
    console_printf("nc: listen mode on :%u (accept one client, 400ms timeout)\n", (unsigned)port);
    if (!net_ready()) {
        peak_perror("nc", "network down");
        return 1;
    }
    int lid = net_tcp_listen(port);
    if (lid < 0) {
        net_print_failure("nc", "listen failed");
        return 1;
    }
    console_write("nc: waiting for connection (400ms)…\n");
    uint64_t deadline = timer_ticks() + 40;
    int fd = -1;
    while (timer_ticks() < deadline) {
        net_poll();
        fd = net_tcp_accept(lid);
        if (fd >= 0)
            break;
    }
    if (fd < 0) {
        net_print_failure("nc", "accept timeout");
        net_tcp_unlisten(port);
        return 1;
    }
    console_write("nc: client connected\n");
    net_tcp_close();
    return 0;
}

int unc_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("nc", "-l <port> | <host> <port> | <host:port>");
        return argc < 2 ? 1 : 0;
    }
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-l")) return nc_listen_mode(argc, argv);
    uint32_t ip = 0;
    uint16_t port = 0;
    int pr = nc_parse_target(argc, argv, &ip, &port);
    if (pr == -2) {
        peak_perror("nc", "network down");
        return 1;
    }
    if (pr != 0) {
        peak_perror("nc", "bad host/port or DNS failed");
        return 1;
    }
    char addr[32];
    net_format_ip(ip, addr, sizeof(addr));
    console_printf("nc: connect %s:%u\n", addr, (unsigned)port);
    if (net_tcp_connect(ip, port, 400) != 0) {
        net_print_failure("nc", "connect failed");
        return 1;
    }
    char payload[512];
    size_t plen = 0;
    int has_colon = 0;
    for (const char *p = argv[1]; *p; p++) {
        if (*p == ':') {
            has_colon = 1;
            break;
        }
    }
    if (argc >= 4 && argv[1][0] != '-' && !has_colon) {
        plen = peak_join_args(argc, argv, 3, payload, sizeof(payload));
        if (plen == (size_t)-1)
            plen = 0;
    } else {
        const char *sin = shell_stdin_path();
        if (sin) {
            size_t n = 0;
            if (vfs_read_file(sin, payload, sizeof(payload) - 1, &n) == 0) {
                payload[n] = '\0';
                plen = n;
            }
        }
    }
    if (plen > 0) {
        if (net_tcp_send(payload, plen) != 0) {
            net_print_failure("nc", "send failed");
            net_tcp_close();
            return 1;
        }
    }
    char rbuf[1024];
    size_t got = 0;
    if (net_tcp_recv(rbuf, sizeof(rbuf) - 1, &got, 200) == 0 && got > 0) {
        rbuf[got] = '\0';
        for (size_t i = 0; i < got; i++)
            console_putc(rbuf[i]);
        if (rbuf[got - 1] != '\n')
            console_write("\n");
    }
    net_tcp_close();
    return 0;
}
int utlsinfo_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("tlsinfo", "[-r] [-s] [-m pattern host]");
        return 0;
    }
    int show_roots = 0;
    int show_sessions = 0;
    const char *match_pat = NULL;
    const char *match_host = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r"))
            show_roots = 1;
        else if (!strcmp(argv[i], "-s"))
            show_sessions = 1;
        else if (!strcmp(argv[i], "-m") && i + 2 < argc) {
            match_pat = argv[++i];
            match_host = argv[++i];
        } else {
            peak_usage("tlsinfo", "[-r] [-s] [-m pattern host]");
            return 1;
        }
    }
    if (match_pat && match_host) {
        int m = tls_hostname_matches_sni(match_pat, match_host);
        console_printf("hostname_match %s %s => %s\n", match_pat, match_host,
                       m ? "yes" : "no");
        return m ? 0 : 1;
    }
    console_printf("trust: webpki_roots=%d pins=%d tofu=%s\n",
                   webpki_root_count, tls_trust_pin_count(),
                   settings_tls_tofu() ? "on" : "off");
    console_printf("session: connected=%d cert_verified=%d hostname_matched=%d\n",
                   tls_ready(), tls_cert_verified(), tls_hostname_matched());
    console_printf("session_cache: used=%d/%d\n",
                   tls_session_used_count(), tls_session_max_slots());
    if (show_sessions) {
        int n = tls_session_used_count();
        for (int i = 0; i < n; i++) {
            char sni[TLS_SESSION_SNI_MAX];
            struct tls_session_meta meta;
            size_t tlen = 0;
            if (tls_session_entry_info(i, sni, sizeof(sni), &meta, &tlen) != 0)
                continue;
            console_printf("  [%d] sni=%s tls=%s cipher=0x%04x ticket_len=%u res_master=%s binder=hkdf\n",
                           i, sni, meta.tls13 ? "1.3" : "1.2", meta.cipher,
                           (unsigned)tlen, meta.res_master_len ? "yes" : "no");
        }
    }
    {
        const char *fail = tls_cert_fail_reason();
        if (fail && fail[0])
            console_printf("cert_fail: %s\n", fail);
    }
    {
        int code = tls_last_error_code();
        console_printf("last_error: code=%d (%s) msg=%s\n",
                       code, tls_err_name(code), tls_last_error());
    }
    if (show_roots) {
        console_printf("webpki roots (%d):\n", webpki_root_count);
        for (int i = 0; i < webpki_root_count; i++) {
            char hex[65];
            if (webpki_root_sha256(i, hex, sizeof(hex)) == 0)
                console_printf("  [%d] sha256=%s\n", i, hex);
        }
    } else if (webpki_root_count > 0) {
        console_write("hint: tlsinfo -r lists embedded root SHA-256 digests\n");
    }
    return 0;
}

