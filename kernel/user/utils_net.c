#include "libpeak.h"
#include "shell.h"
#include "console.h"
#include "net.h"
#include "timer.h"
#include "util.h"

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
        peak_perror("ping", "DNS failed");
        return 1;
    }
    char buf[32];
    net_format_ip(ip, buf, sizeof(buf));
    console_printf("PING %s (%s)\n", argv[1], buf);
    uint64_t t0 = timer_ticks();
    int ok = (net_tcp_connect(ip, 80, 300) == 0);
    uint64_t dt = timer_ticks() - t0;
    if (ok) {
        net_tcp_close();
        console_printf("tcp/:80 open from %s time=%lums\n", buf, (unsigned long)(dt * 10));
        return 0;
    }
    console_printf("tcp/:80 no response from %s (host may filter)\n", buf);
    console_printf("DNS ok - stack is talking to the network.\n");
    return 1;
}

int uwget_main(int argc, char **argv) {
    extern void privacy_grant_net_client(int remember);
    privacy_grant_net_client(0);
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("wget", "<url>");
        return argc < 2 ? 1 : 0;
    }
    if (!net_ready()) {
        peak_perror("wget", "network down");
        return 1;
    }
    char body[8192];
    int st = 0;
    console_printf("GET %s\n", argv[1]);
    if (net_http_get(argv[1], body, sizeof(body), &st) != 0) {
        console_printf("failed (status %d)\n", st);
        if (body[0])
            console_write(body);
        console_write("\n");
        return 1;
    }
    console_printf("HTTP %d  %lu bytes\n", st, (unsigned long)strlen(body));
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
