#include "net_internal.h"
#include "tcp_util.h"
#include "peak_errno.h"
#include "cap.h"
#include "privacy.h"
#include "timer.h"
#include "util.h"

int net_tcp_find_slot(uint32_t src, uint16_t sport, uint16_t dport) {
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].state == TCP_CLOSED || tcps[i].state == TCP_LISTEN)
            continue;
        if (tcps[i].remote_ip == src && tcps[i].remote_port == sport &&
            tcps[i].local_port == dport)
            return i;
    }
    return -1;
}

int net_tcp_find_listener(uint16_t port) {
    for (int i = 0; i < NET_LISTEN_MAX; i++) {
        if (listens[i].used && listens[i].port == port)
            return i;
    }
    return -1;
}

int net_tcp_alloc_slot(void) {
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].state == TCP_CLOSED)
            return i;
    }
    return -1;
}

int net_tcp_send_seg_slot(int slot, uint8_t flags, const void *data, uint16_t dlen) {
    if (slot < 0 || slot >= NET_TCP_MAX)
        return PEAK_EINVAL;
    struct tcp_conn *c = &tcps[slot];
    uint8_t seg[1500];
    uint16_t hdr = 20;
    uint16_t total = (uint16_t)(hdr + dlen);
    if (total > sizeof(seg))
        return PEAK_ENOBUFS;
    memset(seg, 0, hdr);
    seg[0] = (uint8_t)(c->local_port >> 8);
    seg[1] = (uint8_t)(c->local_port & 0xFF);
    seg[2] = (uint8_t)(c->remote_port >> 8);
    seg[3] = (uint8_t)(c->remote_port & 0xFF);
    uint32_t seq = htonl(c->snd_nxt);
    memcpy(seg + 4, &seq, 4);
    uint32_t ack = htonl(c->rcv_nxt);
    memcpy(seg + 8, &ack, 4);
    seg[12] = (uint8_t)((hdr / 4) << 4);
    seg[13] = flags;
    /* Advertise actual receive space so senders are not throttled to a
     * stale 8 KiB window (and stop sending into a full buffer). */
    size_t space = sizeof(c->rx) - c->rx_len;
    uint16_t win = space > 0xFFFF ? 0xFFFF : (uint16_t)space;
    seg[14] = (uint8_t)(win >> 8);
    seg[15] = (uint8_t)(win & 0xFF);
    if (dlen)
        memcpy(seg + hdr, data, dlen);
    uint16_t csum = net_tcp_checksum(local_ip, c->remote_ip, seg, total);
    seg[16] = (uint8_t)(csum >> 8);
    seg[17] = (uint8_t)(csum & 0xFF);
    int r = net_ip_send(c->remote_ip, IP_TCP, seg, total);
    if (r == 0) {
        if (flags & TCP_SYN)
            c->snd_nxt++;
        if (flags & TCP_FIN)
            c->snd_nxt++;
        c->snd_nxt += dlen;
    }
    return r;
}

int net_tcp_send_seg(uint8_t flags, const void *data, uint16_t dlen) {
    return net_tcp_send_seg_slot(tcp_cur, flags, data, dlen);
}

void net_handle_tcp(uint32_t src, const uint8_t *pkt, uint16_t len) {
    struct tcp_hdr_info h;
    if (tcp_parse_header(pkt, len, &h) != 0)
        return;
    uint16_t sport = h.sport;
    uint16_t dport = h.dport;
    uint32_t seq = h.seq;
    uint8_t flags = h.flags;
    uint16_t dlen = h.dlen;
    const uint8_t *data = h.data;

    int slot = net_tcp_find_slot(src, sport, dport);
    if (slot < 0) {
        /* Passive open: SYN to a listening port */
        if ((flags & TCP_SYN) && !(flags & TCP_ACK) && net_tcp_find_listener(dport) >= 0) {
            slot = net_tcp_alloc_slot();
            if (slot < 0)
                return;
            struct tcp_conn *nc = &tcps[slot];
            memset(nc, 0, sizeof(*nc));
            nc->remote_ip = src;
            nc->remote_port = sport;
            nc->local_port = dport;
            nc->rcv_nxt = seq + 1;
            nc->snd_nxt = 0x2000u + (uint32_t)(timer_ticks() & 0xFFFF);
            nc->is_server = 1;
            nc->accepted = 0;
            nc->state = TCP_ESTABLISHED;
            net_tcp_send_seg_slot(slot, TCP_SYN | TCP_ACK, NULL, 0);
        }
        return;
    }

    struct tcp_conn *c = &tcps[slot];

    if (flags & TCP_RST) {
        c->state = TCP_CLOSED;
        return;
    }

    if (c->state == TCP_SYN_SENT && (flags & TCP_SYN) && (flags & TCP_ACK)) {
        c->rcv_nxt = seq + 1;
        c->snd_nxt = h.ack;
        c->state = TCP_ESTABLISHED;
        net_tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
        return;
    }

    if (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT ||
        c->state == TCP_CLOSE_WAIT) {
        if (dlen > 0 && seq == c->rcv_nxt) {
            size_t space = sizeof(c->rx) - c->rx_len;
            if ((size_t)dlen > space) {
                net_tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
            } else {
                memcpy(c->rx + c->rx_len, data, dlen);
                c->rx_len += dlen;
                c->rcv_nxt += dlen;
                net_tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
            }
        } else if (dlen > 0 && seq + dlen > c->rcv_nxt) {
            net_tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
        }
        if ((flags & TCP_FIN) && c->state != TCP_CLOSE_WAIT) {
            c->rcv_nxt++;
            c->got_fin = 1;
            net_tcp_send_seg_slot(slot, TCP_ACK, NULL, 0);
            c->state = TCP_CLOSE_WAIT;
        }
    }
}

int net_tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ticks) {
    attempt_stats.tcp++;
    if (!net_ready())
        return PEAK_ENETDOWN;
    if (!privacy_net_client_allowed())
        return PEAK_EACCES;
    int slot = -1;
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].state == TCP_CLOSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return PEAK_EBUSY;
    tcp_cur = slot;
    memset(&tcps[slot], 0, sizeof(tcps[slot]));
    tcp_remote_ip = ip;
    tcp_remote_port = port;
    tcp_local_port = ephem_port++;
    if (ephem_port < 40000)
        ephem_port = 40000;
    tcp_snd_nxt = 0x1000u + (uint32_t)(timer_ticks() & 0xFFFF);
    tcp_rcv_nxt = 0;
    tcp_rx_len = 0;
    tcp_got_fin = 0;
    tcp_state = TCP_SYN_SENT;
    uint32_t syn_seq = tcp_snd_nxt;
    if (net_tcp_send_seg(TCP_SYN, NULL, 0) != 0)
        return PEAK_EIO;
    uint64_t start = timer_ticks();
    uint64_t last_syn = start;
    while (!net_timed_out(start, timeout_ticks)) {
        net_poll();
        if (tcp_state == TCP_ESTABLISHED)
            return 0;
        if (tcp_state == TCP_CLOSED)
            return PEAK_ENOTCONN;
        /* Retransmit SYN with the original ISN (send_seg advances snd_nxt). */
        if (tcp_state == TCP_SYN_SENT &&
            net_timed_out(last_syn, NET_TCP_SYN_RETRY_TICKS)) {
            tcp_snd_nxt = syn_seq;
            if (net_tcp_send_seg(TCP_SYN, NULL, 0) != 0)
                return PEAK_EIO;
            last_syn = timer_ticks();
        }
        hlt_if_enabled();
    }
    tcp_state = TCP_CLOSED;
    return PEAK_ETIMEOUT;
}

int net_tcp_active_count(void) {
    int n = 0;
    for (int i = 0; i < NET_TCP_MAX; i++)
        if (tcps[i].state != TCP_CLOSED)
            n++;
    return n;
}

int net_tcp_listen(uint16_t port) {
    if (!port)
        return PEAK_EINVAL;
    /* LAN expose requires CAP_NET_LAN; localhost-only is the default. */
    int want_lan = !privacy_listeners_localhost_only();
    if (!privacy_net_listen_allowed(want_lan))
        return PEAK_EACCES;
    int existing = net_tcp_find_listener(port);
    if (existing >= 0)
        return existing;
    for (int i = 0; i < NET_LISTEN_MAX; i++) {
        if (!listens[i].used) {
            listens[i].used = 1;
            listens[i].port = port;
            return i;
        }
    }
    return PEAK_EBUSY;
}

void net_tcp_unlisten(uint16_t port) {
    for (int i = 0; i < NET_LISTEN_MAX; i++) {
        if (listens[i].used && listens[i].port == port)
            listens[i].used = 0;
    }
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].is_server && tcps[i].local_port == port &&
            tcps[i].state != TCP_CLOSED) {
            tcps[i].state = TCP_CLOSED;
            tcps[i].rx_len = 0;
        }
    }
}

int net_tcp_listening(uint16_t port) {
    return net_tcp_find_listener(port) >= 0;
}

int net_tcp_accept(int listen_id) {
    if (listen_id < 0 || listen_id >= NET_LISTEN_MAX || !listens[listen_id].used)
        return PEAK_EINVAL;
    uint16_t port = listens[listen_id].port;
    for (int i = 0; i < NET_TCP_MAX; i++) {
        if (tcps[i].is_server && !tcps[i].accepted &&
            tcps[i].local_port == port &&
            (tcps[i].state == TCP_ESTABLISHED || tcps[i].state == TCP_CLOSE_WAIT)) {
            tcps[i].accepted = 1;
            return i;
        }
    }
    return PEAK_EAGAIN;
}

int net_tcp_fd_send(int fd, const void *data, size_t len) {
    if (fd < 0 || fd >= NET_TCP_MAX)
        return PEAK_EINVAL;
    if (tcps[fd].state != TCP_ESTABLISHED)
        return PEAK_ENOTCONN;
    const uint8_t *p = data;
    while (len) {
        uint16_t chunk = len > TCP_MSS ? TCP_MSS : (uint16_t)len;
        if (net_tcp_send_seg_slot(fd, TCP_PSH | TCP_ACK, p, chunk) != 0)
            return PEAK_EIO;
        p += chunk;
        len -= chunk;
        net_poll();
    }
    return 0;
}

int net_tcp_fd_recv(int fd, void *buf, size_t cap, size_t *out_len,
                    uint32_t timeout_ticks) {
    if (out_len)
        *out_len = 0;
    if (fd < 0 || fd >= NET_TCP_MAX)
        return PEAK_EINVAL;
    uint64_t start = timer_ticks();
    while (tcps[fd].rx_len == 0) {
        net_poll();
        if (tcps[fd].got_fin && tcps[fd].rx_len == 0)
            return PEAK_ENOTCONN;
        if (tcps[fd].state == TCP_CLOSED)
            return PEAK_ENOTCONN;
        if (net_timed_out(start, timeout_ticks))
            return PEAK_ETIMEOUT;
        hlt_if_enabled();
    }
    size_t n = tcps[fd].rx_len < cap ? tcps[fd].rx_len : cap;
    memcpy(buf, tcps[fd].rx, n);
    memmove(tcps[fd].rx, tcps[fd].rx + n, tcps[fd].rx_len - n);
    tcps[fd].rx_len -= n;
    if (out_len)
        *out_len = n;
    return 0;
}

void net_tcp_fd_close(int fd) {
    if (fd < 0 || fd >= NET_TCP_MAX)
        return;
    if (tcps[fd].state == TCP_ESTABLISHED || tcps[fd].state == TCP_CLOSE_WAIT) {
        net_tcp_send_seg_slot(fd, TCP_FIN | TCP_ACK, NULL, 0);
        for (int i = 0; i < 10; i++)
            net_poll_idle();
    }
    tcps[fd].state = TCP_CLOSED;
    tcps[fd].rx_len = 0;
    tcps[fd].is_server = 0;
    tcps[fd].accepted = 0;
}

int net_tcp_send(const void *data, size_t len) {
    if (tcp_state != TCP_ESTABLISHED)
        return PEAK_ENOTCONN;
    const uint8_t *p = data;
    while (len) {
        uint16_t chunk = len > TCP_MSS ? TCP_MSS : (uint16_t)len;
        if (net_tcp_send_seg(TCP_PSH | TCP_ACK, p, chunk) != 0)
            return PEAK_EIO;
        p += chunk;
        len -= chunk;
        net_poll();
    }
    return 0;
}

int net_tcp_recv(void *buf, size_t cap, size_t *out_len, uint32_t timeout_ticks) {
    if (out_len)
        *out_len = 0;
    uint64_t start = timer_ticks();
    while (tcp_rx_len == 0) {
        net_poll();
        if (tcp_got_fin && tcp_rx_len == 0)
            return PEAK_ENOTCONN;
        if (tcp_state == TCP_CLOSED)
            return PEAK_ENOTCONN;
        if (net_timed_out(start, timeout_ticks))
            return PEAK_ETIMEOUT;
        hlt_if_enabled();
    }
    size_t n = tcp_rx_len < cap ? tcp_rx_len : cap;
    memcpy(buf, tcp_rx, n);
    memmove(tcp_rx, tcp_rx + n, tcp_rx_len - n);
    tcp_rx_len -= n;
    if (out_len)
        *out_len = n;
    return 0;
}

void net_tcp_close(void) {
    if (tcp_state == TCP_ESTABLISHED || tcp_state == TCP_CLOSE_WAIT) {
        net_tcp_send_seg(TCP_FIN | TCP_ACK, NULL, 0);
        for (int i = 0; i < 20; i++)
            net_poll_idle();
    }
    tcp_state = TCP_CLOSED;
    tcp_rx_len = 0;
}
