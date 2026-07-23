/*
 * Host tests for DHCP option parsing, HTTP path/MIME helpers,
 * ARP cache helpers, and peak.conf network keys.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../kernel/include/dhcp_util.h"
#include "../../kernel/include/http_util.h"
#include "../../kernel/include/arp_util.h"
#include "peak_conf.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    /* IPv4 parse */
    uint32_t ip = 0;
    expect(peak_parse_ipv4("10.0.2.15", &ip) == 0 && ip == 0x0A00020Fu, "parse 10.0.2.15");
    expect(peak_parse_ipv4("256.0.0.1", &ip) != 0, "reject bad octet");
    expect(peak_parse_ipv4("1.2.3", &ip) != 0, "reject short");

    /* DHCP options */
    uint8_t opts[] = {
        99, 130, 83, 99,
        53, 1, 2,             /* OFFER */
        1, 4, 255, 255, 255, 0,
        3, 4, 10, 0, 2, 2,
        6, 4, 10, 0, 2, 3,
        54, 4, 10, 0, 2, 2,
        255
    };
    struct dhcp_lease lease;
    expect(dhcp_parse_options(opts, sizeof(opts), &lease) == 0, "parse offer opts");
    expect(lease.msg_type == 2, "msg OFFER");
    expect(lease.mask == 0xFFFFFF00u, "mask");
    expect(lease.gw == 0x0A000202u, "gw");
    expect(lease.dns == 0x0A000203u, "dns");
    expect(lease.server_id == 0x0A000202u, "server id");

    /* HTTP path normalize */
    char path[128];
    expect(http_normalize_path("/", path, sizeof(path)) == 0 && !strcmp(path, "/"), "root");
    expect(http_normalize_path("/style.css", path, sizeof(path)) == 0 &&
               !strcmp(path, "/style.css"),
           "css path");
    expect(http_normalize_path("/a/../b", path, sizeof(path)) == 0 && !strcmp(path, "/b"),
           "dotdot collapse");
    expect(http_normalize_path("/../etc/passwd", path, sizeof(path)) != 0, "reject escape");
    expect(http_normalize_path("relative", path, sizeof(path)) != 0, "reject relative");

    expect(!strcmp(http_mime_for_path("/x.html"), "text/html; charset=utf-8"), "html mime");
    expect(!strcmp(http_mime_for_path("/style.css"), "text/css; charset=utf-8"), "css mime");

    char method[16], rpath[64];
    expect(http_parse_request_line("GET /style.css HTTP/1.0\r\n", method, sizeof(method),
                                   rpath, sizeof(rpath)) == 0,
           "parse GET");
    expect(!strcmp(method, "GET") && !strcmp(rpath, "/style.css"), "GET fields");
    expect(http_parse_request_line("POST / HTTP/1.0\r\n", method, sizeof(method), rpath,
                                   sizeof(rpath)) == 0 &&
               !strcmp(method, "POST"),
           "parse POST");

    char origin[128];
    expect(http_parse_origin("https://example.com/path", origin, sizeof(origin)) == 0 &&
               !strcmp(origin, "https://example.com:443"),
           "parse origin https");
    expect(http_parse_origin("http://127.0.0.1:8080/x", origin, sizeof(origin)) == 0 &&
               !strcmp(origin, "http://127.0.0.1:8080"),
           "parse origin port");
    expect(http_same_origin("http://a.com/x", "http://a.com/y"), "same origin");
    expect(!http_same_origin("http://a.com", "http://b.com"), "diff origin");
    expect(http_cors_allows("http://a.com:80", "*", 0), "cors star");
    expect(!http_cors_allows("http://a.com:80", "*", 1), "cors star cred");
    expect(http_cors_allows("http://a.com:80", "http://a.com:80", 0), "cors echo");
    char resolved[128];
    expect(http_resolve_url("http://ex.com/a/b.html", "c.js", resolved, sizeof(resolved)) == 0 &&
               !strcmp(resolved, "http://ex.com:80/a/c.js"),
           "resolve relative");
    expect(http_resolve_url("http://ex.com/a/b.html", "/z.js", resolved, sizeof(resolved)) == 0 &&
               !strcmp(resolved, "http://ex.com:80/z.js"),
           "resolve absolute path");

    /* ARP open-addressed cache */
    {
        struct arp_entry cache[ARP_CACHE_MAX];
        uint8_t mac[6], out[6];
        memset(cache, 0, sizeof(cache));
        memset(mac, 0xAB, sizeof(mac));
        mac[5] = 0x01;
        expect(arp_cache_lookup(cache, ARP_CACHE_MAX, 0x0A000202u, out) != 0, "arp miss empty");
        arp_cache_store(cache, ARP_CACHE_MAX, 0x0A000202u, mac);
        expect(arp_cache_lookup(cache, ARP_CACHE_MAX, 0x0A000202u, out) == 0 &&
                   out[0] == 0xAB && out[5] == 0x01,
               "arp hit after store");
        mac[5] = 0x02;
        arp_cache_store(cache, ARP_CACHE_MAX, 0x0A000202u, mac);
        expect(arp_cache_lookup(cache, ARP_CACHE_MAX, 0x0A000202u, out) == 0 && out[5] == 0x02,
               "arp update same ip");
        /* Fill table and ensure lookup stays bounded / last write wins on home. */
        for (unsigned i = 0; i < ARP_CACHE_MAX + 4; i++) {
            uint32_t ip = 0x0A000100u + i;
            uint8_t m[6] = {0, 0, 0, 0, 0, (uint8_t)i};
            arp_cache_store(cache, ARP_CACHE_MAX, ip, m);
        }
        unsigned valid = 0;
        for (unsigned i = 0; i < ARP_CACHE_MAX; i++)
            if (cache[i].valid)
                valid++;
        expect(valid == ARP_CACHE_MAX, "arp cache stays bounded");
        expect(arp_cache_home(0x0A000202u) < ARP_CACHE_MAX, "arp home in range");
        expect(arp_cache_lookup(NULL, ARP_CACHE_MAX, 1, out) != 0, "arp reject null cache");
        expect(arp_cache_lookup(cache, 0, 1, out) != 0, "arp reject zero n");
    }

    /* peak.conf */
    struct peak_loader_conf conf;
    const char *text =
        "resolution=1280x720\n"
        "net_mode=static\n"
        "net_ip=192.168.1.50\n"
        "net_mask=255.255.255.0\n"
        "net_gw=192.168.1.1\n"
        "net_dns=1.1.1.1\n"
        "dhcp_timeout_ticks=100\n";
    peak_conf_parse(text, strlen(text), &conf);
    expect(conf.width == 1280 && conf.height == 720, "resolution");
    expect(conf.net.mode == PEAK_NET_STATIC, "static mode");
    expect(conf.net.ip == 0xC0A80132u, "net_ip");
    expect(conf.net.gw == 0xC0A80101u, "net_gw");
    expect(conf.net.dns == 0x01010101u, "net_dns");
    expect(conf.net.dhcp_timeout_ticks == 100, "dhcp timeout");

    peak_conf_defaults(&conf);
    expect(conf.net.mode == PEAK_NET_DHCP_FALLBACK, "default mode");
    expect(conf.net.ip == 0x0A00020Fu, "default ip");
    expect(PEAK_BOOT_VERSION == 4, "boot ABI v4");
    expect(sizeof(struct peak_bootinfo) > sizeof(struct peak_net_config), "bootinfo has net");

    if (fails) {
        fprintf(stderr, "%d lan test(s) failed\n", fails);
        return 1;
    }
    printf("OK — lan host unit tests passed\n");
    return 0;
}
