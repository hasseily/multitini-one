/* SPDX-License-Identifier: GPL-3.0-only */
/* DHCP uses UDP port68 even with SIPR=0, as WIZnet's official ioLibrary.
 * Lease/TTL zero and short values retain their protocol meanings. */
#include "net_internal.h"
#include <ctype.h>
#include <limits.h>
static bool name_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return false;
    }
    return !*a && !*b;
}
static int dns_name(const uint8_t *p, size_t size, size_t *pos, char out[MT_NET_NAME]) {
    size_t at = *pos, end = 0, count = 0;
    unsigned jumps = 0;
    while (at < size) {
        unsigned c = p[at++];
        if (!c) {
            if (!end)
                end = at;
            out[count] = 0;
            *pos = end;
            return 0;
        }
        if ((c & 0xc0) == 0xc0) {
            unsigned to;
            if (at >= size || ++jumps > 16)
                return -1;
            to = ((c & 63) << 8) | p[at++];
            if (!end)
                end = at;
            if (to >= size)
                return -1;
            at = to;
            continue;
        }
        if (c > 63 || at + c > size || count + c + 1 >= MT_NET_NAME)
            return -1;
        if (count)
            out[count++] = '.';
        while (c--) {
            unsigned char ch = p[at++];
            if (!ch || ch == '.')
                return -1;
            out[count++] = (char)ch;
        }
    }
    return -1;
}
int mt_net_dns_parse(const uint8_t *p, size_t size, uint16_t id, const char *question,
                     uint8_t ip[4], uint32_t *ttl) {
    size_t pos = 12, answers;
    char name[MT_NET_NAME], current[MT_NET_NAME];
    unsigned i, hop, count;
    uint32_t minimum = UINT32_MAX;
    if (size < 12 || size > 512 || net_be16(p) != id || (p[2] & 0xf8) != 0x80 ||
        net_be16(p + 4) != 1)
        return -1;
    if (dns_name(p, size, &pos, name) || !name_equal(name, question) || pos + 4 > size ||
        net_be16(p + pos) != 1 || net_be16(p + pos + 2) != 1)
        return -1;
    pos += 4;
    if ((p[3] & 15) != 0)
        return -2;
    count = net_be16(p + 6);
    if (count > 64)
        return -1;
    answers = pos;
    memcpy(current, question, strlen(question) + 1);
    for (hop = 0; hop <= 4; hop++) {
        bool cname = false;
        pos = answers;
        for (i = 0; i < count; i++) {
            uint16_t type, cls, len;
            uint32_t life;
            size_t data;
            if (dns_name(p, size, &pos, name) || pos + 10 > size)
                return -1;
            type = net_be16(p + pos);
            cls = net_be16(p + pos + 2);
            life = net_be32(p + pos + 4);
            len = net_be16(p + pos + 8);
            pos += 10;
            data = pos;
            if (pos + len > size)
                return -1;
            pos += len;
            if (cls != 1 || !name_equal(name, current))
                continue;
            if (type == 1 && len == 4) {
                memcpy(ip, p + data, 4);
                if (life < minimum)
                    minimum = life;
                *ttl = minimum > 86400 ? 86400 : minimum;
                return net_zero(ip) ? -1 : 0;
            }
            if (type == 5) {
                size_t next = data;
                if (hop == 4 || dns_name(p, size, &next, name) || next > data + len)
                    return -1;
                memcpy(current, name, strlen(name) + 1);
                if (life < minimum)
                    minimum = life;
                cname = true;
                break;
            }
        }
        if (!cname)
            return -2;
    }
    return -2;
}
static bool valid_name(const char *name) {
    size_t i, n = strlen(name), label = 0;
    if (!n || n >= MT_NET_NAME)
        return false;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == '.') {
            if (!label || label > 63)
                return false;
            label = 0;
        } else {
            if (!isalnum(c) && c != '-' && c != '_')
                return false;
            label++;
        }
    }
    return label && label <= 63;
}
int mt_net_resolve_owned(mt_net_t *n, mt_net_owner_t owner, const char *name, uint8_t out[4]) {
    unsigned i;
    uint8_t numeric[4];
    if (mt_net_ipv4(name, numeric)) {
        memcpy(out, numeric, 4);
        return 0;
    }
    if (!valid_name(name) || !mt_net_available(n))
        return -2;
    if (n->query_owner == owner && name_equal(n->query, name) && !n->query_pending) {
        int result = n->query_result;
        if (result == 0)
            memcpy(out, n->query_ip, 4);
        n->query[0] = 0;
        return result;
    }
    for (i = 0; i < 8; i++)
        if (name_equal(n->cache[i].name, name) && n->cache[i].expires > n->now) {
            memcpy(out, n->cache[i].ip, 4);
            n->cache[i].used = n->now;
            return 0;
        }
    /* Preserve an uncached (including TTL-zero/error) completion until its
     * owner consumes or cancels it. A different target must not steal it. */
    if (n->query_pending || (n->query[0] && n->query_owner != owner))
        return MT_NET_AGAIN;
    if (net_zero(n->dns[0]) && net_zero(n->dns[1]))
        return -2;
    memcpy(n->query, name, strlen(name) + 1);
    n->query_owner = owner;
    n->query_pending = true;
    n->query_sent = false;
    n->query_server = net_zero(n->dns[0]) ? 1 : 0;
    n->query_attempt = 0;
    n->query_id = (uint16_t)net_random(n);
    n->query_result = MT_NET_AGAIN;
    n->query_due = 0;
    return MT_NET_AGAIN;
}
int mt_net_resolve(mt_net_t *n, const char *name, uint8_t out[4]) {
    return mt_net_resolve_owned(n, MT_NET_OWNER_SYSTEM, name, out);
}
void mt_net_resolve_cancel(mt_net_t *n) {
    n->query_pending = false;
    n->query_sent = false;
    n->query[0] = 0;
    n->query_result = -2;
}
void mt_net_resolve_cancel_owner(mt_net_t *n, mt_net_owner_t owner) {
    if (n->query_owner == owner)
        mt_net_resolve_cancel(n);
}
static void query_next(mt_net_t *n) {
    n->query_sent = false;
    n->query_id = (uint16_t)net_random(n);
    if (++n->query_attempt < 2)
        return;
    n->query_attempt = 0;
    if (++n->query_server < 2 && !net_zero(n->dns[n->query_server]))
        return;
    n->query_pending = false;
    n->query_result = -2;
}
static void dns_packet(mt_net_t *n, const uint8_t *p, size_t size, const uint8_t src[4],
                       uint16_t port) {
    uint8_t ip[4];
    uint32_t ttl;
    int result;
    unsigned i, slot = 0;
    uint64_t oldest = UINT64_MAX;
    if (!n->query_pending || port != 53 || memcmp(src, n->dns[n->query_server], 4))
        return;
    result = mt_net_dns_parse(p, size, n->query_id, n->query, ip, &ttl);
    if (result == -1)
        return;
    if (result) {
        if (size >= 4 && (p[3] & 15) == 3) {
            n->query_pending = false;
            n->query_result = -2;
        } else
            query_next(n);
        return;
    }
    n->query_pending = false;
    n->query_result = 0;
    memcpy(n->query_ip, ip, 4);
    if (!ttl)
        return;
    for (i = 0; i < 8; i++) {
        if (n->cache[i].expires <= n->now) {
            slot = i;
            break;
        }
        if (n->cache[i].used < oldest) {
            oldest = n->cache[i].used;
            slot = i;
        }
    }
    memcpy(n->cache[slot].name, n->query, strlen(n->query) + 1);
    memcpy(n->cache[slot].ip, ip, 4);
    n->cache[slot].expires = n->now + (uint64_t)ttl * NET_US;
    n->cache[slot].used = n->now;
}
void net_protocol_restart(mt_net_t *n) {
    static const uint8_t zero[4] = {0};
    n->dhcp_state = 1;
    n->dhcp_attempt = 0;
    n->dhcp_due = 0;
    n->lease_expiry = n->t1 = n->t2 = 0;
    n->xid = net_random(n);
    n->dhcp_tx_wait = false;
    memset(n->server, 0, 4);
    net_address_apply(n, zero, zero, zero, false);
}
void mt_net_dhcp_packet(mt_net_t *n, const uint8_t *p, size_t size, const uint8_t source[4],
                        uint16_t port) {
    uint8_t type = 0, server[4] = {0}, mask[4] = {0}, gateway[4] = {0}, dns[8] = {0};
    uint32_t lease = 0, t1 = 0, t2 = 0;
    size_t pos = 240;
    bool have_lease = false, have_mask = false, have_gateway = false, have_dns = false;
    (void)source;
    if (n->config.mode != MT_NET_DHCP || n->runtime_address || port != 67 || size < 240 ||
        p[0] != 2 || p[1] != 1 || p[2] != 6 || net_be32(p + 4) != n->xid ||
        memcmp(p + 28, n->config.mac, 6) || net_be32(p + 236) != 0x63825363)
        return;
    while (pos < size) {
        uint8_t key = p[pos++], len;
        if (!key)
            continue;
        if (key == 255)
            break;
        if (pos >= size)
            return;
        len = p[pos++];
        if (pos + len > size)
            return;
        switch (key) {
        case 53:
            if (len != 1)
                return;
            type = p[pos];
            break;
        case 54:
            if (len != 4)
                return;
            memcpy(server, p + pos, 4);
            break;
        case 1:
            if (len != 4)
                return;
            memcpy(mask, p + pos, 4);
            have_mask = true;
            break;
        case 3:
            if (len < 4 || len % 4)
                return;
            memcpy(gateway, p + pos, 4);
            have_gateway = true;
            break;
        case 6:
            if (len < 4 || len % 4)
                return;
            memcpy(dns, p + pos, len >= 8 ? 8 : 4);
            have_dns = true;
            break;
        case 51:
            if (len != 4)
                return;
            lease = net_be32(p + pos);
            have_lease = true;
            break;
        case 58:
            if (len != 4)
                return;
            t1 = net_be32(p + pos);
            break;
        case 59:
            if (len != 4)
                return;
            t2 = net_be32(p + pos);
            break;
        default:
            break;
        }
        pos += len;
    }
    if (net_zero(server))
        return;
    if (type == 2 && n->dhcp_state == 2 && !net_zero(p + 16)) {
        memcpy(n->offered, p + 16, 4);
        memcpy(n->server, server, 4);
        n->dhcp_state = 3;
        n->dhcp_attempt = 0;
        n->dhcp_due = 0;
        return;
    }
    if (n->dhcp_state != 3 && n->dhcp_state != 4 && n->dhcp_state != 5)
        return;
    if (n->dhcp_state != 5 && memcmp(server, n->server, 4))
        return;
    if (type == 6) {
        net_protocol_restart(n);
        return;
    }
    if (type != 5 || !have_lease || !lease)
        return;
    if (net_zero(p + 16) && net_zero(n->ip))
        return;
    if (n->dhcp_state == 4 || n->dhcp_state == 5) {
        if (!have_mask)
            memcpy(mask, n->mask, 4);
        if (!have_gateway)
            memcpy(gateway, n->gateway, 4);
        if (!have_dns)
            memcpy(dns, n->dns, 8);
    }
    memcpy(n->server, server, 4);
    memcpy(n->dns, dns, 8);
    n->dhcp_state = 6;
    n->dhcp_attempt = 0;
    if (lease == UINT32_MAX)
        n->lease_expiry = n->t1 = n->t2 = UINT64_MAX;
    else {
        uint64_t duration = (uint64_t)lease * NET_US;
        n->lease_expiry = n->now + duration;
        n->t1 = n->now + (t1 && t1 < lease ? (uint64_t)t1 * NET_US : duration / 2);
        n->t2 = n->now + (t2 && t2 < lease && (uint64_t)t2 * NET_US > n->t1 - n->now
                              ? (uint64_t)t2 * NET_US
                              : duration * 7 / 8);
        if (n->t2 <= n->t1)
            n->t1 = n->now + duration / 2;
    }
    net_address_apply(n, net_zero(p + 16) ? n->ip : p + 16, mask, gateway, false);
}
static size_t dhcp_build(mt_net_t *n) {
    uint8_t *p = n->packet;
    size_t at = 240, len = strlen(n->config.hostname);
    bool renew = n->dhcp_state == 4 || n->dhcp_state == 5;
    memset(p, 0, 240);
    p[0] = 1;
    p[1] = 1;
    p[2] = 6;
    net_put32(p + 4, n->xid);
    if (renew)
        memcpy(p + 12, n->ip, 4);
    if (n->dhcp_state != 4)
        net_put16(p + 10, 0x8000);
    memcpy(p + 28, n->config.mac, 6);
    net_put32(p + 236, 0x63825363);
    p[at++] = 53;
    p[at++] = 1;
    p[at++] = n->dhcp_state == 1 || n->dhcp_state == 2 ? 1 : 3;
    p[at++] = 61;
    p[at++] = 7;
    p[at++] = 1;
    memcpy(p + at, n->config.mac, 6);
    at += 6;
    if (len) {
        p[at++] = 12;
        p[at++] = (uint8_t)len;
        memcpy(p + at, n->config.hostname, len);
        at += len;
    }
    p[at++] = 55;
    p[at++] = 6;
    p[at++] = 1;
    p[at++] = 3;
    p[at++] = 6;
    p[at++] = 51;
    p[at++] = 58;
    p[at++] = 59;
    if (n->dhcp_state == 3) {
        p[at++] = 50;
        p[at++] = 4;
        memcpy(p + at, n->offered, 4);
        at += 4;
        p[at++] = 54;
        p[at++] = 4;
        memcpy(p + at, n->server, 4);
        at += 4;
    }
    p[at++] = 255;
    while (at < 300)
        p[at++] = 0;
    return at;
}
static size_t dns_build(mt_net_t *n) {
    uint8_t *p = n->packet;
    size_t at = 12, start = 0, i, len = strlen(n->query);
    memset(p, 0, 12);
    net_put16(p, n->query_id);
    p[2] = 1;
    p[5] = 1;
    for (i = 0; i <= len; i++)
        if (!n->query[i] || n->query[i] == '.') {
            p[at++] = (uint8_t)(i - start);
            memcpy(p + at, n->query + start, i - start);
            at += i - start;
            start = i + 1;
        }
    p[at++] = 0;
    net_put16(p + at, 1);
    net_put16(p + at + 2, 1);
    return at + 4;
}
void net_protocol_poll(mt_net_t *n) {
    mt_net_socket_t *s = &n->sockets[0];
    int got;
    size_t dg;
    if (n->budget < 100)
        return;
    if (n->config.mode == MT_NET_DHCP && !n->runtime_address && n->lease_expiry &&
        n->lease_expiry != UINT64_MAX) {
        if (n->now >= n->lease_expiry)
            net_protocol_restart(n);
        else if (n->now >= n->t2 && n->dhcp_state != 5) {
            n->dhcp_state = 5;
            n->dhcp_due = 0;
        } else if (n->now >= n->t1 && n->dhcp_state == 6) {
            n->dhcp_state = 4;
            n->dhcp_due = 0;
        }
    }
    if (s->state != MT_NET_S_CONNECTED)
        return;
    got = mt_net_receive_result(n, 0, n->packet, sizeof(n->packet), &dg);
    if (got >= 0) {
        if (dg == (size_t)got) {
            if (s->rx_port == 67)
                mt_net_dhcp_packet(n, n->packet, (size_t)got, s->rx_peer, s->rx_port);
            else if (s->rx_port == 53)
                dns_packet(n, n->packet, (size_t)got, s->rx_peer, s->rx_port);
        }
    }
    if (!s->receive_active && s->receive_result == MT_NET_AGAIN)
        (void)mt_net_receive(n, 0, MT_NET_PACKET);
    if (!n->link || s->send_active)
        return;
    if (n->config.mode == MT_NET_DHCP && !n->runtime_address && n->dhcp_state &&
        n->dhcp_state != 6 && n->now >= n->dhcp_due) {
        size_t len;
        uint64_t delay;
        if (n->dhcp_state == 3 && n->dhcp_attempt >= 3) {
            net_protocol_restart(n);
            return;
        }
        len = dhcp_build(n);
        s->port = 67;
        memset(s->peer, 255, 4);
        if (n->dhcp_state == 4)
            memcpy(s->peer, n->server, 4);
        if (mt_net_send(n, 0, n->packet, len))
            return;
        if (n->dhcp_state <= 2) {
            unsigned shift = n->dhcp_attempt > 4 ? 4 : n->dhcp_attempt;
            delay = (UINT64_C(4) << shift) * NET_US;
            delay = delay - NET_US + (net_random(n) % 2000001U);
            n->dhcp_state = 2;
        } else if (n->dhcp_state == 3)
            delay = 2 * NET_US;
        else {
            uint64_t until = n->dhcp_state == 4 ? n->t2 : n->lease_expiry;
            delay = until > n->now ? (until - n->now) / 2 : 0;
            if (delay < 60 * NET_US)
                delay = 60 * NET_US;
        }
        if (n->dhcp_attempt < 255)
            n->dhcp_attempt++;
        n->dhcp_due = n->now + delay;
        return;
    }
    if (n->query_pending) {
        if (!mt_net_available(n)) {
            n->query_pending = false;
            n->query_result = -2;
            return;
        }
        if (n->query_sent && n->now >= n->query_due)
            query_next(n);
        if (n->query_pending && !n->query_sent) {
            size_t len = dns_build(n);
            memcpy(s->peer, n->dns[n->query_server], 4);
            s->port = 53;
            if (!mt_net_send(n, 0, n->packet, len)) {
                n->query_sent = true;
                n->query_due = n->now + 2 * NET_US;
            }
        }
    }
}
