/*
 * Host corpus for TCP/HTTP parser utilities: headers, redirects,
 * partial reads, checksums, and malformed inputs.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../kernel/include/http_util.h"
#include "../../kernel/include/tcp_util.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void test_http_url_and_status(void) {
    int https = 0;
    char host[64], path[128];
    uint16_t port = 0;

    expect(http_parse_url("http://ex.com:8080/a/b", &https, host, sizeof(host), &port, path,
                          sizeof(path)) == 0 &&
               !https && port == 8080 && !strcmp(host, "ex.com") && !strcmp(path, "/a/b"),
           "parse url with port");
    expect(http_parse_url("https://ex.com/x", &https, host, sizeof(host), &port, path,
                          sizeof(path)) == 0 &&
               https && port == 443 && !strcmp(path, "/x"),
           "parse https default port");
    expect(http_parse_url("http://ex.com?q=1", &https, host, sizeof(host), &port, path,
                          sizeof(path)) == 0 &&
               !strcmp(host, "ex.com") && !strcmp(path, "?q=1"),
           "host stops before query");
    expect(http_parse_url("http://ex.com#frag", &https, host, sizeof(host), &port, path,
                          sizeof(path)) == 0 &&
               !strcmp(host, "ex.com") && !strcmp(path, "#frag"),
           "host stops before fragment");
    expect(http_parse_url("http://ex.com:99999/", &https, host, sizeof(host), &port, path,
                          sizeof(path)) != 0,
           "reject port overflow");
    expect(http_parse_url("ftp://ex.com/", &https, host, sizeof(host), &port, path,
                          sizeof(path)) != 0,
           "reject non-http scheme");
    expect(http_parse_url("http:///", &https, host, sizeof(host), &port, path, sizeof(path)) !=
               0,
           "reject empty host");
    expect(http_parse_url(NULL, &https, host, sizeof(host), &port, path, sizeof(path)) != 0,
           "reject null url");

    int st = -1;
    expect(http_parse_status("HTTP/1.0 302 Found\r\n", &st) == 0 && st == 302, "status 302");
    expect(http_parse_status("HTTP/1.1 200 OK\r\n", &st) == 0 && st == 200, "status 200");
    expect(http_parse_status("HTTP/1.0 OK\r\n", &st) != 0, "reject missing status");
    expect(http_parse_status("NotHTTP/1.0 200\r\n", &st) != 0, "reject non-HTTP");
    expect(http_parse_status("HTTP/1.0 99\r\n", &st) != 0, "reject short status");
    expect(http_parse_status(NULL, &st) != 0, "reject null status buf");

    expect(http_is_redirect(301) && http_is_redirect(302) && http_is_redirect(303),
           "redirect 301-303");
    expect(http_is_redirect(307) && http_is_redirect(308), "redirect 307-308");
    expect(!http_is_redirect(200) && !http_is_redirect(404), "non-redirects");
}

static void test_http_headers_partial(void) {
    const char *full =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Location: /next\r\n"
        "\r\n"
        "<html>hi</html>";

    expect(http_headers_len(full, 10) == 0, "partial: too short");
    expect(http_headers_len(full, 40) == 0, "partial: no delimiter yet");
    size_t hlen = http_headers_len(full, strlen(full));
    expect(hlen > 0 && hlen < strlen(full), "complete headers len");
    expect(hlen == (size_t)(strstr(full, "\r\n\r\n") - full) + 4, "headers end at CRLFCRLF");

    /* Simulate TCP partial reads appending into a buffer. */
    char buf[256];
    size_t got = 0;
    const char *chunks[] = {
        "HTTP/1.0 302 Found\r\nLoc",
        "ation: /b\r\n",
        "\r\n",
        "body",
    };
    int complete = 0;
    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        size_t n = strlen(chunks[i]);
        expect(got + n < sizeof(buf), "chunk fits");
        memcpy(buf + got, chunks[i], n);
        got += n;
        buf[got] = '\0';
        if (http_headers_len(buf, got) > 0) {
            complete = 1;
            break;
        }
    }
    expect(complete, "headers complete after partial chunks");

    char loc[64];
    expect(http_header_value(buf, "location", loc, sizeof(loc)) == 0 && !strcmp(loc, "/b"),
           "location after partial assemble");
    expect(http_header_value(buf, "LOCATION", loc, sizeof(loc)) == 0 && !strcmp(loc, "/b"),
           "header key case-insensitive");
    expect(http_header_value(buf, "missing", loc, sizeof(loc)) != 0, "missing header");

    char hdr[128];
    http_copy_response_headers(full, hdr, sizeof(hdr));
    expect(strstr(hdr, "Content-Type: text/html") != NULL, "copy has content-type");
    expect(strstr(hdr, "<html>") == NULL, "copy excludes body");

    char msg[256];
    snprintf(msg, sizeof(msg), "%s", full);
    http_strip_headers(msg);
    expect(!strcmp(msg, "<html>hi</html>"), "strip leaves body");

    /* Incomplete message: strip/copy must be no-ops. */
    char partial[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n";
    char partial_copy[64] = "sentinel";
    http_copy_response_headers(partial, partial_copy, sizeof(partial_copy));
    expect(partial_copy[0] == '\0', "copy no-op when incomplete");
    char keep[64];
    snprintf(keep, sizeof(keep), "%s", partial);
    http_strip_headers(keep);
    expect(!strcmp(keep, partial), "strip no-op when incomplete");

    /* Malformed / edge header lookups */
    expect(http_header_value(NULL, "x", loc, sizeof(loc)) != 0, "null raw");
    expect(http_header_value(full, "", loc, sizeof(loc)) != 0, "empty key");
    expect(http_header_value(full, "location", loc, 2) != 0, "tiny out truncates reject");
}

static void test_http_redirects(void) {
    char out[256];

    expect(http_join_redirect(0, "ex.com", 8080, "/a/b.html", "/z", out, sizeof(out)) == 0 &&
               !strcmp(out, "http://ex.com:8080/z"),
           "absolute-path redirect keeps port");
    expect(http_join_redirect(0, "ex.com", 8080, "/a/b.html", "c.js", out, sizeof(out)) == 0 &&
               !strcmp(out, "http://ex.com:8080/a/c.js"),
           "relative redirect keeps port");
    expect(http_join_redirect(1, "ex.com", 443, "/old", "https://other/new", out,
                              sizeof(out)) == 0 &&
               !strcmp(out, "https://other/new"),
           "absolute location copied");
    expect(http_join_redirect(0, "ex.com", 80, "/dir/page", "../x", out, sizeof(out)) == 0 &&
               !strcmp(out, "http://ex.com:80/x"),
           "relative .. normalized");
    expect(http_join_redirect(0, "ex.com", 80, "/", "../../../../etc/passwd", out,
                              sizeof(out)) != 0,
           "reject escape redirect");
    expect(http_join_redirect(0, NULL, 80, "/", "/x", out, sizeof(out)) != 0, "null host");

    expect(http_blocks_active_mixed("https://a.com/", "http://b.com/x") == 1, "mixed block");
    expect(http_blocks_active_mixed("https://a.com/", "https://b.com/x") == 0, "https ok");
    expect(http_blocks_active_mixed("http://a.com/", "http://b.com/x") == 0, "http page ok");
    expect(http_blocks_active_mixed(NULL, "http://b.com/x") == 0, "null page");
}

static void test_tcp_header_and_checksum(void) {
    /* Minimal 20-byte TCP header: sport=1234 dport=80 seq=1 ack=2 off=5 flags=ACK win=1024 */
    uint8_t seg[24] = {
        0x04, 0xD2, /* 1234 */
        0x00, 0x50, /* 80 */
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x02,
        0x50, 0x10, /* data_off=5, ACK */
        0x04, 0x00, /* window */
        0x00, 0x00, /* checksum */
        0x00, 0x00, /* urgent */
        'h', 'i', '!', '!'
    };
    struct tcp_hdr_info h;
    expect(tcp_parse_header(seg, 24, &h) == 0, "parse ok");
    expect(h.sport == 1234 && h.dport == 80, "ports");
    expect(h.seq == 1 && h.ack == 2, "seq/ack");
    expect(h.data_off == 20 && h.flags == 0x10 && h.window == 1024, "off/flags/win");
    expect(h.dlen == 4 && h.data[0] == 'h' && h.data[3] == '!', "payload");

    expect(tcp_parse_header(seg, 19, &h) != 0, "reject truncated");
    expect(tcp_parse_header(NULL, 20, &h) != 0, "reject null pkt");
    expect(tcp_parse_header(seg, 20, NULL) != 0, "reject null out");

    /* data_off = 0 words → 0 bytes: illegal */
    uint8_t bad_off[20];
    memcpy(bad_off, seg, 20);
    bad_off[12] = 0x00;
    expect(tcp_parse_header(bad_off, 20, &h) != 0, "reject data_off < 20");

    /* data_off claims 24 bytes but len is 20 */
    bad_off[12] = 0x60;
    expect(tcp_parse_header(bad_off, 20, &h) != 0, "reject data_off > len");

    /* Options: data_off = 6 (24 bytes), payload 2 */
    uint8_t with_opt[26];
    memset(with_opt, 0, sizeof(with_opt));
    memcpy(with_opt, seg, 20);
    with_opt[12] = 0x60; /* 6 words */
    with_opt[24] = 'A';
    with_opt[25] = 'B';
    expect(tcp_parse_header(with_opt, 26, &h) == 0 && h.data_off == 24 && h.dlen == 2 &&
               h.data[0] == 'A',
           "options header");

    /* Checksum: empty buffer → 0xFFFF */
    expect(net_checksum("", 0) == 0xFFFF, "empty checksum");
    uint8_t one = 0x01;
    expect(net_checksum(&one, 1) == (uint16_t)~0x0100, "odd-length checksum");

    /* TCP checksum over header+payload with zeroed checksum field should verify to 0
     * when recomputed over the filled segment (classic ones'-complement property). */
    uint16_t csum = net_tcp_checksum(0x0A00020Fu, 0x0A000202u, seg, 24);
    seg[16] = (uint8_t)(csum >> 8);
    seg[17] = (uint8_t)(csum & 0xFF);
    expect(net_tcp_checksum(0x0A00020Fu, 0x0A000202u, seg, 24) == 0, "tcp csum verifies");
}

int main(void) {
    test_http_url_and_status();
    test_http_headers_partial();
    test_http_redirects();
    test_tcp_header_and_checksum();

    if (fails) {
        fprintf(stderr, "%d http/tcp parser test(s) failed\n", fails);
        return 1;
    }
    printf("OK — http/tcp parser host corpus passed\n");
    return 0;
}
