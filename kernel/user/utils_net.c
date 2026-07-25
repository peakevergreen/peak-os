/* /bin network utilities: ifconfig, ping, wget, tlsinfo. */
#include "libpeak.h"
#include "cap.h"
#include "privacy.h"
#include "shell.h"
#include "console.h"
#include "net.h"
#include "tls.h"
#include "tls_util.h"
#include "webpki.h"
#include "settings.h"
#include "random.h"
#include "timer.h"
#include "util.h"
#include "vfs.h"

static void net_print_failure(const char *tool, const char *what) {
    const char *detail = net_last_error();
    if (detail && detail[0])
        console_printf("%s: %s: %s\n", tool, what, detail);
    else
        peak_perror(tool, what);
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
    console_printf("  gateway %s  dns %s\n", gw, dns);
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
        peak_usage("wget", "[-O path] <url>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("wget", "network down");
        return 1;
    }
    const char *url = 0;
    const char *out_path = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-O") && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        if (argv[i][0] != '-')
            url = argv[i];
    }
    if (!url) {
        peak_usage("wget", "[-O path] <url>");
        return 1;
    }
    char body[8192];
    int st = 0;
    console_printf("GET %s\n", url);
    if (net_http_get(url, body, sizeof(body), &st) != 0) {
        const char *tls = net_http_tls_reject_name();
        const char *detail = net_last_error();
        if (tls && tls[0])
            console_printf("failed: TLS %s (HTTP status %d)\n", tls, st);
        else if (st > 0)
            console_printf("failed: HTTP %d\n", st);
        else if (detail && detail[0])
            console_printf("failed: %s (status %d)\n", detail, st);
        else
            console_printf("failed: connect/DNS/TLS error (status %d)\n", st);
        if (body[0]) {
            console_write(body);
            console_write("\n");
        }
        return 1;
    }
    console_printf("HTTP %d  %lu bytes\n", st, (unsigned long)strlen(body));
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
        peak_usage("curl", "[-o path] <url>");
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
        av[ac++] = argv[i];
    }
    av[ac] = 0;
    return uwget_main(ac, av);
}

int unslookup_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("nslookup", "<hostname>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("nslookup", "network down");
        return 1;
    }
    struct net_info ni;
    net_get_info(&ni);
    char dns[32];
    net_format_ip(ni.dns, dns, sizeof(dns));
    console_printf("Server:\t%s\n", dns);
    console_printf("Name:\t%s\n", argv[1]);
    uint32_t ip = net_dns_resolve(argv[1], 400);
    if (!ip) {
        net_print_failure("nslookup", "DNS failed");
        return 1;
    }
    char addr[32];
    net_format_ip(ip, addr, sizeof(addr));
    console_printf("Address:\t%s\n", addr);
    return 0;
}

int uhost_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("host", "<hostname>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("host", "network down");
        return 1;
    }
    uint32_t ip = net_dns_resolve(argv[1], 400);
    if (!ip) {
        net_print_failure("host", "DNS failed");
        return 1;
    }
    char addr[32];
    net_format_ip(ip, addr, sizeof(addr));
    console_printf("%s has address %s\n", argv[1], addr);
    return 0;
}

/* Parse host:port or host port. Returns 0 and fills ip/port; -2 if net down. */
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

int unc_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("nc", "<host> <port> | <host:port>");
        return argc < 2 ? 1 : 0;
    }
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
    /* Optional one-shot send from remaining argv joined, else stdin path. */
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
        peak_usage("tlsinfo", "[-r] [-m pattern host]");
        return 0;
    }
    int show_roots = 0;
    const char *match_pat = NULL;
    const char *match_host = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r"))
            show_roots = 1;
        else if (!strcmp(argv[i], "-m") && i + 2 < argc) {
            match_pat = argv[++i];
            match_host = argv[++i];
        } else {
            peak_usage("tlsinfo", "[-r] [-m pattern host]");
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

