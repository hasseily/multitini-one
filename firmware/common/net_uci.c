/* SPDX-License-Identifier: GPL-3.0-only */
/* Ultimate network target03. Wire reference: GideonZ/1541ultimate
 * d43546fe25b26cb332f43f00219d33153dfe44f8 network_target.cc.
 * Listener reply sizes match xlar54 ultimateii-dos-lib 3a38e690eaa5. */
#include "multitini_net_uci.h"
#include <stdio.h>
#include <string.h>
static mt_uci_result_t status(mt_uci_reply_t *r, const char *s) {
    mt_uci_reply_clear(r);
    mt_uci_reply_status(r, s);
    return MT_UCI_REPLY;
}
static bool owns(mt_net_uci_t *u, int id) {
    return id > 0 && id < 16 && u->net->sockets[id].state != MT_NET_S_FREE &&
           u->net->sockets[id].owner == MT_NET_OWNER_UCI;
}
static mt_uci_result_t count_reply(mt_uci_reply_t *r, int count, const char *s) {
    status(r, s);
    r->data[0] = (uint8_t)count;
    r->data[1] = (uint8_t)(count >> 8);
    r->data_length = 2;
    return MT_UCI_REPLY;
}
static mt_uci_result_t chunk(mt_net_uci_t *u, mt_uci_reply_t *r) {
    size_t prefix = u->offset ? 0 : 2, amount = u->length - u->offset;
    mt_uci_reply_clear(r);
    if (amount > 895 - prefix)
        amount = 895 - prefix;
    if (prefix) {
        r->data[0] = (uint8_t)u->length;
        r->data[1] = (uint8_t)(u->length >> 8);
    }
    memcpy(r->data + prefix, u->payload + u->offset, amount);
    u->offset += amount;
    r->data_length = prefix + amount;
    r->more = u->offset < u->length;
    if (!r->more)
        mt_uci_reply_status(r, u->status);
    return MT_UCI_REPLY;
}
static unsigned listener_state(mt_net_uci_t *u) {
    mt_net_socket_state_t s;
    if (!u->listener)
        return u->listener_error;
    s = mt_net_state(u->net, u->listener, NULL);
    return s == MT_NET_S_CONNECTED                            ? 2
           : s == MT_NET_S_LISTENING || s == MT_NET_S_OPENING ? 1
           : s == MT_NET_S_CLOSED                             ? 3
                                                              : 0;
}
static mt_uci_result_t poll(void *context, mt_uci_reply_t *r) {
    mt_net_uci_t *u = context;
    mt_net_t *n = u->net;
    int result, error;
    uint8_t ip[4];
    mt_net_socket_state_t s;
    size_t dg;
    switch (u->command) {
    case 7:
    case 8:
        if (!mt_net_available(n))
            return status(r, "83,INTERFACE NOT AVAILABLE");
        if (!u->phase) {
            result = mt_net_resolve_owned(n, MT_NET_OWNER_UCI, u->host, ip);
            if (result == MT_NET_AGAIN)
                return MT_UCI_WAIT;
            if (result)
                return status(r, "84,UNRESOLVED HOST");
            u->socket = mt_net_open(n, MT_NET_OWNER_UCI, u->command == 8, ip, u->port, false);
            if (u->socket == MT_NET_AGAIN)
                return MT_UCI_WAIT;
            if (u->socket < 0)
                return status(r, u->socket == -100 ? "83,INTERFACE NOT AVAILABLE"
                                                   : "85,ERROR OPENING SOCKET");
            u->phase = 1;
        }
        s = mt_net_state(n, u->socket, &error);
        if (s == MT_NET_S_CONNECTED) {
            status(r, "00,OK");
            r->data[0] = (uint8_t)u->socket;
            r->data_length = 1;
            u->phase = 2;
            return MT_UCI_REPLY;
        }
        if (s == MT_NET_S_CLOSED || s == MT_NET_S_FREE) {
            snprintf(u->status, sizeof(u->status), "11,ERROR ON CONNECT: %d", error ? error : 104);
            (void)mt_net_close(n, u->socket);
            return status(r, u->status);
        }
        return MT_UCI_WAIT;
    case 0x10:
        result = mt_net_receive_result(n, u->socket, u->payload, sizeof(u->payload), &dg);
        if (result == MT_NET_AGAIN) {
            if (n->now < u->deadline)
                return MT_UCI_WAIT;
            /* The deadline bounds waiting for data, not copying a receive
             * already selected from the ring. RECV may have committed RX_RD
             * before the next poll publishes its result; canceling here would
             * discard those bytes. Command completion retains its own bound. */
            if (owns(u, u->socket) && n->sockets[u->socket].receive_active &&
                n->sockets[u->socket].rx_phase != 0)
                return MT_UCI_WAIT;
            if (owns(u, u->socket))
                n->sockets[u->socket].receive_active = false;
            return count_reply(r, -1, "02,NO DATA: 11");
        }
        if (result < 0) {
            snprintf(u->status, sizeof(u->status), "02,NO DATA: %d", -result);
            return count_reply(r, -1, u->status);
        }
        u->length = (size_t)result;
        u->offset = 0;
        if (!result && (!owns(u, u->socket) || !n->sockets[u->socket].udp))
            strcpy(u->status, "01,CONNECTION CLOSED BY HOST");
        else if (dg > (size_t)result)
            snprintf(u->status, sizeof(u->status), "04,DATAGRAM TRUNCATED: %u", (unsigned)dg);
        else
            strcpy(u->status, "00,OK");
        return chunk(u, r);
    case 0x11:
        result = mt_net_send_result(n, u->socket);
        if (result == MT_NET_AGAIN)
            return MT_UCI_WAIT;
        if (result < 0) {
            snprintf(u->status, sizeof(u->status), "12,SEND ERROR: %d", -result);
            return count_reply(r, -1, u->status);
        }
        return count_reply(r, result, "00,OK");
    default:
        return status(r, "21,UNKNOWN COMMAND");
    }
}
static mt_uci_result_t begin(void *context, const uint8_t c[896], size_t length,
                             mt_uci_reply_t *r) {
    mt_net_uci_t *u = context;
    mt_net_t *n = u->net;
    unsigned count = n->enabled ? 1 : 0;
    uint16_t requested;
    size_t len;
    int result;
    static const uint8_t zero[4] = {0};
    u->command = length >= 2 ? c[1] : 0;
    u->phase = 0;
    u->length = u->offset = 0;
    u->socket = 0;
    switch (u->command) {
    case 1:
        mt_uci_reply_text(r, "ULTIMATE-II NETWORK INTERFACE V1.0", "00,OK");
        return MT_UCI_REPLY;
    case 2:
        status(r, "00,OK");
        r->data[0] = (uint8_t)count;
        r->data_length = 1;
        return MT_UCI_REPLY;
    case 4:
    case 5:
    case 6:
        if (length != (u->command == 6 ? 15 : 3))
            return status(r, "81,INVALID PARAMS");
        if (c[2] >= count)
            return status(r, "82,PARAMETER(S) OUT OF RANGE");
        status(r, "00,OK");
        if (u->command == 4) {
            memcpy(r->data, n->config.mac, 6);
            r->data_length = 6;
        } else if (u->command == 5) {
            memcpy(r->data, n->ip, 4);
            memcpy(r->data + 4, n->mask, 4);
            memcpy(r->data + 8, n->gateway, 4);
            r->data_length = 12;
        } else
            mt_net_set_address(n, c + 3);
        return MT_UCI_REPLY;
    case 7:
    case 8:
        if (length < 5 || length > 896)
            return status(r, "81,INVALID PARAMS");
        u->port = (uint16_t)(c[2] | ((unsigned)c[3] << 8));
        len = length - 4;
        while (len && c[4 + len - 1] == 0)
            len--;
        if (!len || len >= sizeof(u->host) || memchr(c + 4, 0, len))
            return status(r, "81,INVALID PARAMS");
        memcpy(u->host, c + 4, len);
        u->host[len] = 0;
        return poll(u, r);
    case 9:
        if (length != 3)
            return status(r, "81,INVALID PARAMS");
        if (!owns(u, c[2]))
            return status(r, "12,ERROR ON CLOSE: 9");
        (void)mt_net_close(n, c[2]);
        if (u->listener == c[2])
            u->listener = 0;
        return status(r, "00,OK");
    case 0x10:
        if (length != 5)
            return status(r, "81,INVALID PARAMS");
        requested = (uint16_t)(c[3] | ((unsigned)c[4] << 8));
        if (requested > 1472)
            return status(r, "82,PARAMETER(S) OUT OF RANGE");
        if (!owns(u, c[2]))
            return count_reply(r, -1, "02,NO DATA: 9");
        u->socket = c[2];
        u->requested = requested;
        u->deadline = n->now + 40000;
        result = mt_net_receive(n, u->socket, requested);
        if (result && result != MT_NET_AGAIN)
            return count_reply(r, -1, "02,NO DATA: 9");
        return poll(u, r);
    case 0x11:
        if (length < 3 || length > 896)
            return status(r, "81,INVALID PARAMS");
        if (!owns(u, c[2]))
            return count_reply(r, -1, "12,SEND ERROR: 9");
        u->socket = c[2];
        result = mt_net_send(n, u->socket, c + 3, length - 3);
        if (result) {
            snprintf(u->status, sizeof(u->status), "12,SEND ERROR: %d", -result);
            return count_reply(r, -1, u->status);
        }
        return poll(u, r);
    case 0x12:
        if (length != 4)
            return status(r, "81,INVALID PARAMS");
        if (u->listener)
            (void)mt_net_close(n, u->listener);
        u->port = (uint16_t)(c[2] | ((unsigned)c[3] << 8));
        u->listener = 0;
        u->listener_error = 0;
        {
            unsigned i;
            for (i = 1; i < 16; i++)
                if (n->sockets[i].state != MT_NET_S_FREE &&
                    n->sockets[i].state != MT_NET_S_CLOSED && n->sockets[i].local_port == u->port) {
                    u->listener_error = 4;
                    return count_reply(r, 4, "00,OK");
                }
        }
        result = mt_net_open(n, MT_NET_OWNER_UCI, false, zero, u->port, true);
        if (result < 0)
            u->listener_error = 3;
        else
            u->listener = result;
        return count_reply(r, (int)listener_state(u), "00,OK");
    case 0x13:
        if (length != 2)
            return status(r, "81,INVALID PARAMS");
        if (u->listener)
            (void)mt_net_close(n, u->listener);
        u->listener = 0;
        u->listener_error = 0;
        return count_reply(r, 0, "00,OK");
    case 0x14:
        if (length != 2)
            return status(r, "81,INVALID PARAMS");
        return count_reply(r, (int)listener_state(u), "00,OK");
    case 0x15:
        if (length != 2)
            return status(r, "81,INVALID PARAMS");
        status(r, "00,OK");
        r->data[0] = listener_state(u) == 2 ? (uint8_t)u->listener : 0;
        r->data[1] = 0;
        r->data_length = 2;
        return MT_UCI_REPLY;
    default:
        return status(r, "21,UNKNOWN COMMAND");
    }
}
static mt_uci_result_t next(void *context, size_t consumed, mt_uci_reply_t *r) {
    mt_net_uci_t *u = context;
    (void)consumed;
    if (u->command == 0x10 && u->offset < u->length)
        return chunk(u, r);
    return status(r, "00,OK");
}
static mt_uci_result_t abort_cmd(void *context, size_t consumed) {
    mt_net_uci_t *u = context;
    (void)consumed;
    if ((u->command == 7 || u->command == 8) && u->phase < 2) {
        if (u->socket > 0)
            (void)mt_net_close(u->net, u->socket);
        mt_net_resolve_cancel_owner(u->net, MT_NET_OWNER_UCI);
    }
    if (u->command == 0x11 && owns(u, u->socket) && u->net->sockets[u->socket].send_active)
        (void)mt_net_close(u->net, u->socket);
    if (u->command == 0x10 && owns(u, u->socket)) {
        u->net->sockets[u->socket].receive_active = false;
        u->net->sockets[u->socket].receive_result = MT_NET_AGAIN;
    }
    u->command = 0;
    u->length = u->offset = 0;
    return MT_UCI_REPLY;
}
static void reset(void *context) {
    mt_net_uci_t *u = context;
    abort_cmd(u, 0);
    mt_net_close_owner(u->net, MT_NET_OWNER_UCI);
    u->listener = 0;
    u->listener_error = 0;
}
void mt_net_uci_init(mt_net_uci_t *u, mt_net_t *n) {
    memset(u, 0, sizeof(*u));
    u->net = n;
}
mt_uci_target_t mt_net_uci_target(mt_net_uci_t *u) {
    mt_uci_target_t t = {u, begin, poll, next, abort_cmd, reset};
    return t;
}
