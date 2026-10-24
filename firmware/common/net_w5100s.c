/* SPDX-License-Identifier: GPL-3.0-only */
/* Original cooperative W5100S driver. Register semantics: WIZnet W5100S
 * datasheet1.2.8. No polling loops wait for network/command completion. */
#include "net_internal.h"
#define SBASE(s) ((uint16_t)(0x0400U + (unsigned)(s)->hardware * 0x100U))
static int io(mt_net_t *n, uint16_t a, bool w, uint8_t *v) {
    if (n->fault)
        return -5;
    if (!n->budget)
        return MT_NET_AGAIN;
    n->budget--;
    n->transactions++;
    if (n->ops.access(n->ops.context, a, w, *v, v)) {
        n->fault = true;
        return -5;
    }
    return 0;
}
static uint8_t rd(mt_net_t *n, uint16_t a) {
    uint8_t v = 0;
    (void)io(n, a, false, &v);
    return v;
}
static void wr(mt_net_t *n, uint16_t a, uint8_t v) { (void)io(n, a, true, &v); }
static uint16_t rd16(mt_net_t *n, uint16_t a) {
    uint16_t h = rd(n, a);
    return (uint16_t)((h << 8) | rd(n, a + 1));
}
static void wr16(mt_net_t *n, uint16_t a, uint16_t v) {
    wr(n, a, (uint8_t)(v >> 8));
    wr(n, a + 1, (uint8_t)v);
}
static int stable16(mt_net_t *n, uint16_t a) {
    uint16_t last = rd16(n, a);
    unsigned i;
    for (i = 0; i < 8; i++) {
        uint16_t next = rd16(n, a);
        if (next == last)
            return next;
        last = next;
    }
    return MT_NET_AGAIN;
}
static void command(mt_net_t *n, mt_net_socket_t *s, uint8_t c) {
    wr(n, SBASE(s) + 1, c);
    s->command_pending = true;
    s->command = c;
    s->command_deadline = n->now + 10000;
}
static bool valid(mt_net_t *n, int id) {
    return id >= 0 && id < 16 && n->sockets[id].state != MT_NET_S_FREE;
}
static void free_hw(mt_net_t *n, mt_net_socket_t *s) {
    if (s->hardware >= 0) {
        unsigned hw = (unsigned)s->hardware;
        wr(n, SBASE(s) + 1, 0x10);
        n->hardware[hw] = -2;
        n->close_deadline[hw] = n->now + 10000;
        s->hardware = -1;
    }
    s->command_pending = false;
}
static void dead(mt_net_t *n, mt_net_socket_t *s, int error) {
    if (s->owner == MT_NET_OWNER_SYSTEM)
        n->fault = true;
    free_hw(n, s);
    s->state = MT_NET_S_CLOSED;
    s->error = error;
    s->send_active = false;
    s->send_result = -error;
}
uint32_t net_random(mt_net_t *n) {
    uint64_t x = n->random ^ n->now;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    n->random = x ? x : 1;
    return (uint32_t)((x * UINT64_C(2685821657736338717)) >> 32);
}
void mt_net_init(mt_net_t *n, const mt_net_ops_t *ops, const mt_net_config_t *c) {
    unsigned i;
    memset(n, 0, sizeof(*n));
    n->ops = *ops;
    n->next_id = 1;
    n->random = UINT64_C(0x9e3779b97f4a7c15);
    for (i = 0; i < 16; i++)
        n->sockets[i].hardware = -1;
    for (i = 0; i < 4; i++)
        n->hardware[i] = -1;
    mt_net_configure(n, c);
}
void mt_net_shutdown(mt_net_t *n) {
    unsigned i;
    n->ops.run(n->ops.context, false);
    n->ready = false;
    n->enabled = false;
    n->query_pending = false;
    n->query_result = -1;
    for (i = 0; i < 4; i++)
        n->hardware[i] = -1;
    for (i = 0; i < 16; i++) {
        mt_net_socket_t *s = &n->sockets[i];
        s->hardware = -1;
        s->command_pending = false;
        if (s->state != MT_NET_S_FREE) {
            s->state = MT_NET_S_CLOSED;
            s->error = 104;
            s->send_active = false;
            s->send_result = -104;
        }
    }
}
void mt_net_configure(mt_net_t *n, const mt_net_config_t *c) {
    mt_net_shutdown(n);
    n->config = *c;
    n->fault = false;
    n->failures = 0;
    n->init_stage = 0;
    n->runtime_address = false;
    n->enabled = c->mode != MT_NET_OFF && c->mac_valid;
    n->retry_at = 0;
    memset(n->cache, 0, sizeof(n->cache));
    memset(n->ip, 0, 4);
    memcpy(n->dns, c->dns, 8);
    n->ops.run(n->ops.context, n->enabled);
}
bool mt_net_available(const mt_net_t *n) {
    return n->enabled && n->ready && n->link && !net_zero(n->ip) && !n->fault;
}
void mt_net_close_owner(mt_net_t *n, mt_net_owner_t owner) {
    unsigned i;
    for (i = 1; i < 16; i++)
        if (n->sockets[i].owner == owner && n->sockets[i].state != MT_NET_S_FREE) {
            mt_net_socket_t *s = &n->sockets[i];
            s->send_active = false;
            s->receive_active = false;
            if (s->hardware < 0)
                s->state = MT_NET_S_FREE;
            else {
                s->state = MT_NET_S_CLOSING;
                s->stage = 2;
                s->deadline = 0;
            }
        }
}
static void close_clients(mt_net_t *n) {
    unsigned i;
    for (i = 1; i < 16; i++)
        if (n->sockets[i].state != MT_NET_S_FREE && n->sockets[i].state != MT_NET_S_CLOSED)
            dead(n, &n->sockets[i], 104);
    n->query_pending = false;
    n->query_result = -1;
    memset(n->cache, 0, sizeof(n->cache));
}
void net_address_apply(mt_net_t *n, const uint8_t *ip, const uint8_t *mask, const uint8_t *gateway,
                       bool runtime) {
    unsigned i;
    if (memcmp(n->ip, ip, 4) || memcmp(n->mask, mask, 4) || memcmp(n->gateway, gateway, 4))
        close_clients(n);
    memcpy(n->ip, ip, 4);
    memcpy(n->mask, mask, 4);
    memcpy(n->gateway, gateway, 4);
    n->runtime_address = runtime;
    for (i = 0; i < 4; i++) {
        wr(n, (uint16_t)(0x000f + i), ip[i]);
        wr(n, (uint16_t)(0x0005 + i), mask[i]);
        wr(n, (uint16_t)(0x0001 + i), gateway[i]);
    }
}
void mt_net_set_address(mt_net_t *n, const uint8_t a[12]) {
    n->budget = 1024;
    net_address_apply(n, a, a + 4, a + 8, true);
    n->dhcp_state = 0;
}
void mt_net_renew(mt_net_t *n) {
    if (!n->enabled)
        return;
    n->runtime_address = false;
    n->budget = 1024;
    if (n->config.mode == MT_NET_STATIC)
        net_address_apply(n, n->config.ip, n->config.mask, n->config.gateway, false);
    else {
        if (!net_zero(n->ip) && n->lease_expiry > n->now) {
            n->dhcp_state = 4;
            n->dhcp_due = 0;
        } else
            net_protocol_restart(n);
    }
}
static int alloc(mt_net_t *n, mt_net_owner_t owner, bool udp, const uint8_t ip[4], uint16_t port,
                 bool listener, bool system) {
    int hw, id, j;
    mt_net_socket_t *s;
    for (hw = system ? 0 : 1; hw < (system ? 1 : 4); hw++)
        if (n->hardware[hw] == -1)
            break;
    if (hw == (system ? 1 : 4)) {
        if (!system) {
            bool reclaiming = false;
            for (unsigned k = 1; k < 4; k++) {
                if (n->hardware[k] == -2)
                    reclaiming = true;
                if (n->hardware[k] > 0) {
                    mt_net_socket_t *old = &n->sockets[(unsigned)n->hardware[k]];
                    if (old->state == MT_NET_S_CLOSING) {
                        old->stage = 2;
                        old->deadline = 0;
                        reclaiming = true;
                    }
                }
            }
            if (reclaiming)
                return MT_NET_AGAIN;
        }
        return -24;
    }
    id = 0;
    if (!system) {
        for (j = 0; j < 15; j++) {
            id = n->next_id;
            n->next_id = n->next_id == 15 ? 1 : n->next_id + 1;
            if (n->sockets[id].state == MT_NET_S_FREE)
                break;
        }
        if (j == 15)
            return -24;
    }
    s = &n->sockets[id];
    memset(s, 0, sizeof(*s));
    s->hardware = (int8_t)hw;
    s->owner = owner;
    s->udp = udp;
    s->listen = listener;
    memcpy(s->peer, ip, 4);
    s->port = port;
    s->local_port = system ? 68 : listener ? port : n->ephemeral++;
    if (!system && !listener) {
        for (unsigned attempt = 0; attempt < 16; attempt++) {
            bool used = false;
            for (unsigned k = 1; k < 16; k++)
                if (&n->sockets[k] != s && n->sockets[k].state != MT_NET_S_FREE &&
                    n->sockets[k].state != MT_NET_S_CLOSED &&
                    n->sockets[k].local_port == s->local_port)
                    used = true;
            if (!used)
                break;
            if (n->ephemeral < 49152)
                n->ephemeral = 49152;
            s->local_port = n->ephemeral++;
        }
    }
    if (n->ephemeral < 49152)
        n->ephemeral = 49152;
    s->state = MT_NET_S_OPENING;
    s->deadline = n->now + 10 * NET_US;
    s->send_result = MT_NET_AGAIN;
    s->receive_result = MT_NET_AGAIN;
    n->hardware[hw] = (int8_t)id;
    return id;
}
int mt_net_open(mt_net_t *n, mt_net_owner_t o, bool udp, const uint8_t ip[4], uint16_t p,
                bool listener) {
    if (!mt_net_available(n))
        return -100;
    if (!p || (!udp && !listener && (net_zero(ip) || !memcmp(ip, "\xff\xff\xff\xff", 4))))
        return -22;
    return alloc(n, o, udp, ip, p, listener, false);
}
mt_net_socket_state_t mt_net_state(mt_net_t *n, int id, int *error) {
    if (!valid(n, id)) {
        if (error)
            *error = 9;
        return MT_NET_S_FREE;
    }
    if (error)
        *error = n->sockets[id].error;
    return n->sockets[id].state;
}
int mt_net_close(mt_net_t *n, int id) {
    mt_net_socket_t *s;
    if (!valid(n, id) || id == 0)
        return -9;
    s = &n->sockets[id];
    s->send_active = false;
    s->receive_active = false;
    if (s->hardware < 0)
        s->state = MT_NET_S_FREE;
    else {
        s->state = MT_NET_S_CLOSING;
        s->stage = s->udp ? 2 : 0;
        s->deadline = s->udp ? 0 : n->now + NET_US;
    }
    return 0;
}
int mt_net_send(mt_net_t *n, int id, const void *data, size_t size) {
    mt_net_socket_t *s;
    if (!valid(n, id))
        return -9;
    s = &n->sockets[id];
    if (s->state != MT_NET_S_CONNECTED)
        return -104;
    if (s->send_active)
        return MT_NET_AGAIN;
    if (size > MT_NET_PACKET)
        return -22;
    if (!size && !s->udp) {
        s->send_result = 0;
        return 0;
    }
    if (size)
        memcpy(s->tx, data, size);
    s->tx_length = (uint16_t)size;
    s->tx_offset = 0;
    s->tx_phase = 0;
    s->send_active = true;
    s->send_result = MT_NET_AGAIN;
    s->send_deadline = n->now + 2 * NET_US;
    return 0;
}
int mt_net_send_result(mt_net_t *n, int id) {
    if (!valid(n, id))
        return -9;
    return n->sockets[id].send_active ? MT_NET_AGAIN : n->sockets[id].send_result;
}
int mt_net_receive(mt_net_t *n, int id, size_t capacity) {
    mt_net_socket_t *s;
    if (!valid(n, id))
        return -9;
    if (capacity > MT_NET_PACKET)
        return -22;
    s = &n->sockets[id];
    if (s->receive_active || s->receive_result != MT_NET_AGAIN)
        return MT_NET_AGAIN;
    s->receive_active = true;
    s->receive_result = MT_NET_AGAIN;
    s->rx_phase = 0;
    s->rx_offset = 0;
    s->rx_capacity = (uint16_t)capacity;
    return 0;
}
int mt_net_receive_result(mt_net_t *n, int id, void *out, size_t cap, size_t *dg) {
    mt_net_socket_t *s;
    int result;
    if (!valid(n, id))
        return -9;
    s = &n->sockets[id];
    result = s->receive_result;
    if (result == MT_NET_AGAIN)
        return result;
    if (result > 0) {
        if ((size_t)result > cap)
            return -22;
        memcpy(out, s->rx, (size_t)result);
    }
    if (dg)
        *dg = s->datagram_length;
    s->receive_result = MT_NET_AGAIN;
    if (!result && !s->udp && s->state == MT_NET_S_CLOSED && id) {
        s->state = MT_NET_S_FREE;
        s->hardware = -1;
    }
    return result;
}
static void ring_read(mt_net_t *n, mt_net_socket_t *s, uint16_t offset, uint8_t *out,
                      unsigned count) {
    unsigned i;
    uint16_t base = (uint16_t)(0x6000 + s->hardware * 0x800);
    for (i = 0; i < count; i++)
        out[i] = rd(n, base + ((offset + i) & 0x7ff));
}
static void poll_receive(mt_net_t *n, mt_net_socket_t *s, uint8_t sr) {
    int count;
    unsigned amount;
    uint16_t b = SBASE(s);
    if (!s->receive_active)
        return;
    if (s->rx_phase == 0) {
        if (!s->rx_capacity) {
            s->receive_result = 0;
            s->receive_active = false;
            return;
        }
        count = stable16(n, b + 0x26);
        if (count < 0)
            return;
        if (!count) {
            if (sr == 0 || sr == 0x1c) {
                s->receive_result = 0;
                s->receive_active = false;
                dead(n, s, 104);
            }
            return;
        }
        s->rx_pointer = rd16(n, b + 0x28);
        s->rx_offset = 0;
        if (s->udp) {
            uint8_t header[8];
            if (count < 8)
                return;
            ring_read(n, s, s->rx_pointer, header, 8);
            s->datagram_length = net_be16(header + 6);
            if (s->datagram_length > 2040) {
                dead(n, s, 5);
                return;
            }
            if (count < 8 + s->datagram_length)
                return;
            memcpy(s->rx_peer, header, 4);
            s->rx_port = net_be16(header + 4);
            s->rx_pointer += 8;
            if (s->owner != MT_NET_OWNER_SYSTEM &&
                (memcmp(s->peer, s->rx_peer, 4) || s->port != s->rx_port)) {
                wr16(n, b + 0x28, (uint16_t)(s->rx_pointer + s->datagram_length));
                command(n, s, 0x40);
                return;
            }
            s->rx_length =
                s->datagram_length < s->rx_capacity ? s->datagram_length : s->rx_capacity;
        } else {
            s->datagram_length = 0;
            s->rx_length = (uint16_t)(count < s->rx_capacity ? count : s->rx_capacity);
        }
        s->rx_phase = 1;
    }
    if (s->rx_phase == 1) {
        amount = s->rx_length - s->rx_offset;
        if (amount > 128)
            amount = 128;
        ring_read(n, s, s->rx_pointer + s->rx_offset, s->rx + s->rx_offset, amount);
        s->rx_offset += (uint16_t)amount;
        if (s->rx_offset == s->rx_length) {
            wr16(n, b + 0x28,
                 (uint16_t)(s->rx_pointer + (s->udp ? s->datagram_length : s->rx_length)));
            command(n, s, 0x40);
            s->rx_phase = 2;
        }
        return;
    }
    if (s->rx_phase == 2) {
        s->receive_result = s->rx_length;
        s->receive_active = false;
        s->rx_phase = 0;
    }
}
static void poll_send(mt_net_t *n, mt_net_socket_t *s) {
    uint16_t b = SBASE(s);
    unsigned amount, i;
    int count;
    if (!s->send_active)
        return;
    if (n->now >= s->send_deadline) {
        s->send_active = false;
        s->send_result = -116;
        if (s->tx_phase == 2)
            dead(n, s, 116);
        return;
    }
    if (s->tx_phase == 0) {
        count = stable16(n, b + 0x20);
        if (count < 0 || count < s->tx_length)
            return;
        s->pointer = rd16(n, b + 0x24);
        s->tx_phase = 1;
    }
    if (s->tx_phase == 1) {
        amount = s->tx_length - s->tx_offset;
        if (amount > 128)
            amount = 128;
        for (i = 0; i < amount; i++)
            wr(n,
               (uint16_t)(0x4000U + (unsigned)s->hardware * 0x800U +
                          ((s->pointer + s->tx_offset + i) & 0x7ff)),
               s->tx[s->tx_offset + i]);
        s->tx_offset += (uint16_t)amount;
        if (s->tx_offset == s->tx_length) {
            if (s->udp) {
                for (i = 0; i < 4; i++)
                    wr(n, (uint16_t)(b + 0x0c + i), s->peer[i]);
                wr16(n, b + 0x10, s->port);
            }
            wr16(n, b + 0x24, (uint16_t)(s->pointer + s->tx_length));
            wr(n, b + 2, 0x18);
            command(n, s, 0x20);
            s->tx_phase = 2;
        }
        return;
    }
    if (s->tx_phase == 2) {
        uint8_t ir = rd(n, b + 2);
        if (ir & 0x18) {
            wr(n, b + 2, ir & 0x18);
            s->send_active = false;
            s->send_result = ir & 8 ? (s->udp ? -118 : -116) : s->tx_length;
            if (ir & 8 && !s->udp)
                dead(n, s, 116);
        }
    }
}
static void poll_socket(mt_net_t *n, int id) {
    mt_net_socket_t *s = &n->sockets[id];
    uint16_t b;
    uint8_t sr, ir;
    unsigned i;
    if (s->hardware < 0) {
        if (s->receive_active && s->state == MT_NET_S_CLOSED) {
            s->receive_result = s->udp ? -104 : 0;
            s->receive_active = false;
        }
        return;
    }
    b = SBASE(s);
    if (s->command_pending) {
        if (rd(n, b + 1)) {
            if (n->now >= s->command_deadline)
                n->fault = true;
            return;
        }
        s->command_pending = false;
    }
    sr = rd(n, b + 3);
    ir = rd(n, b + 2);
    s->ir = ir;
    if (s->state == MT_NET_S_OPENING) {
        if (!s->stage) {
            wr(n, b, s->udp ? 2 : 1);
            wr16(n, b + 4, s->local_port);
            wr(n, b + 2, 255);
            command(n, s, 1);
            s->stage = 1;
            return;
        }
        if (sr != (s->udp ? 0x22 : 0x13)) {
            if (n->now >= s->deadline)
                dead(n, s, 116);
            return;
        }
        if (s->udp) {
            s->state = MT_NET_S_CONNECTED;
            return;
        }
        if (s->listen) {
            command(n, s, 2);
            s->state = MT_NET_S_LISTENING;
            return;
        }
        for (i = 0; i < 4; i++)
            wr(n, (uint16_t)(b + 0x0c + i), s->peer[i]);
        wr16(n, b + 0x10, s->port);
        command(n, s, 4);
        s->state = MT_NET_S_CONNECTING;
        return;
    }
    if (s->state == MT_NET_S_CONNECTING || s->state == MT_NET_S_LISTENING) {
        if (sr == 0x17) {
            s->state = MT_NET_S_CONNECTED;
            if (s->listen) {
                for (i = 0; i < 4; i++)
                    s->peer[i] = rd(n, (uint16_t)(b + 0x0c + i));
                s->port = rd16(n, b + 0x10);
            }
            return;
        }
        if (sr == 0 || (s->state == MT_NET_S_CONNECTING && n->now >= s->deadline))
            dead(n, s, ir & 8 ? 116 : sr == 0 ? 104 : 116);
        return;
    }
    if (s->state == MT_NET_S_CLOSING) {
        if (!s->stage) {
            command(n, s, 8);
            s->stage = 1;
            return;
        }
        if (sr == 0 || n->now >= s->deadline) {
            free_hw(n, s);
            s->state = MT_NET_S_FREE;
        }
        return;
    }
    if (s->state != MT_NET_S_CONNECTED)
        return;
    /* CLOSE_WAIT preserves unread TCP bytes; hardware is retired only after
     * the RX counter reaches zero, so a FIN never discards trailing data. */
    if (!s->udp && sr == 0 && stable16(n, b + 0x26) == 0) {
        dead(n, s, ir & 8 ? 116 : 104);
        return;
    }
    poll_receive(n, s, sr);
    if (!s->command_pending && s->hardware >= 0)
        poll_send(n, s);
}
void mt_net_poll(mt_net_t *n) {
    unsigned i;
    static const uint8_t zero[4] = {0};
    n->now = n->ops.now_us(n->ops.context);
    n->budget = 1024;
    if (!n->enabled)
        return;
    if (n->fault) {
        mt_net_shutdown(n);
        n->enabled = n->config.mode != MT_NET_OFF;
        n->failures++;
        n->retry_at = n->now + NET_US;
        n->fault = false;
        n->init_stage = 0;
        return;
    }
    if (n->retry_at) {
        if (n->failures >= 3 || n->now < n->retry_at)
            return;
        n->retry_at = 0;
        n->ops.run(n->ops.context, true);
    }
    if (!n->ops.ready(n->ops.context))
        return;
    for (i = 0; i < 4; i++)
        if (n->hardware[i] == -2) {
            if (!rd(n, (uint16_t)(0x401 + i * 0x100)))
                n->hardware[i] = -1;
            else if (n->now >= n->close_deadline[i])
                n->fault = true;
        }
    if (!n->ready) {
        if (rd(n, 0x80) != 0x51) {
            n->fault = true;
            return;
        }
        wr(n, 0x1a, 0x55);
        wr(n, 0x1b, 0x55);
        wr16(n, 0x17, 2000);
        wr(n, 0x19, 7);
        for (i = 0; i < 6; i++)
            wr(n, (uint16_t)(9 + i), n->config.mac[i]);
        n->ephemeral = (uint16_t)(49152 + (net_random(n) & 0x3fff));
        n->ready = true;
        n->link_check = 0;
        alloc(n, MT_NET_OWNER_SYSTEM, true, zero, 67, false, true);
        if (n->config.mode == MT_NET_STATIC)
            net_address_apply(n, n->config.ip, n->config.mask, n->config.gateway, false);
        else
            net_protocol_restart(n);
    }
    if (n->now >= n->link_check) {
        bool before = n->link;
        n->link = (rd(n, 0x3c) & 1) != 0;
        n->link_check = n->now + 100000;
        if (!before && n->link && n->lease_expiry > n->now && !n->runtime_address &&
            n->config.mode == MT_NET_DHCP) {
            n->dhcp_state = 4;
            n->dhcp_due = 0;
        }
    }
    net_protocol_poll(n);
    for (i = 0; i < 4; i++) {
        unsigned hw = n->poll_cursor;
        n->poll_cursor = (n->poll_cursor + 1) & 3;
        if (n->budget < 350)
            break;
        if (n->hardware[hw] >= 0)
            poll_socket(n, n->hardware[hw]);
    }
    for (i = 1; i < 16; i++)
        if (n->sockets[i].hardware < 0 && n->sockets[i].state == MT_NET_S_CLOSED)
            poll_socket(n, (int)i);
}
