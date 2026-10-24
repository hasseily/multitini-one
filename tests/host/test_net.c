/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_net.h"
#include "multitini_net_uci.h"
#include "net_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static mt_net_t net;
static model_t model;
int main(void) {
    mt_net_config_t c, parsed;
    mt_net_ops_t ops = {&model, access_reg, now, run, ready};
    int id, i;
    uint8_t peer[4] = {10, 0, 0, 2}, data[1472], received[1472];
    size_t dg;
    char text[1024];
    bool unknown;
    uint32_t ttl;
    mt_net_config_default(&c);
    assert(mt_net_config_set(&c, "mode", "static"));
    assert(mt_net_config_set(&c, "ip", "10.0.0.1"));
    assert(mt_net_config_set(&c, "netmask", "255.255.255.0"));
    assert(mt_net_config_set(&c, "mac", "02:00:01:02:03:04"));
    assert(!mt_net_config_set(&c, "ip", "256.0.0.1"));
    assert(!mt_net_config_set(&c, "mac", "ff:00:00:00:00:00"));
    i = (int)mt_net_config_format(&c, text, sizeof(text));
    assert(i > 0 && mt_net_config_parse(&parsed, text, (size_t)i, &unknown) && !unknown);
    assert(!memcmp(&c, &parsed, sizeof(c)));
    mt_net_init(&net, &ops, &c);
    poll(&net, &model, 4);
    assert(mt_net_available(&net));
    assert(net.sockets[0].udp && net.sockets[0].local_port == 68);
    id = mt_net_open(&net, MT_NET_OWNER_UCI, false, peer, 80, false);
    assert(id == 1);
    poll(&net, &model, 5);
    assert(mt_net_state(&net, id, NULL) == MT_NET_S_CONNECTED);
    for (i = 0; i < 1472; i++)
        data[i] = (uint8_t)(i ^ 0xa5);
    /* Real ring crossing, with SEND_OK observed on a later foreground pass. */
    put16(model.mem + 0x524, 2000);
    model.tx_ack[1] = 2000;
    assert(!mt_net_send(&net, id, data, sizeof(data)));
    poll(&net, &model, 20);
    assert(mt_net_send_result(&net, id) == 1472 && model.sent_size == 1472 &&
           !memcmp(data, model.sent, 1472));
    put16(model.mem + 0x528, 2000);
    model.rx_ack[1] = 2000;
    inject(&model, 1, data, 1472, false, peer, 80);
    assert(!mt_net_receive(&net, id, 1472));
    poll(&net, &model, 20);
    assert(mt_net_receive_result(&net, id, received, sizeof(received), &dg) == 1472 &&
           !memcmp(data, received, 1472));
    /* FIN preserves received bytes, then reports EOF exactly once. */
    inject(&model, 1, data, 4, false, peer, 80);
    model.mem[0x503] = 0x1c;
    assert(!mt_net_receive(&net, id, 4));
    poll(&net, &model, 3);
    assert(mt_net_receive_result(&net, id, received, sizeof(received), &dg) == 4);
    assert(!mt_net_receive(&net, id, 4));
    poll(&net, &model, 2);
    assert(mt_net_receive_result(&net, id, received, sizeof(received), &dg) == 0);
    assert(mt_net_state(&net, id, NULL) == MT_NET_S_FREE);
    id = mt_net_open(&net, MT_NET_OWNER_UCI, true, peer, 99, false);
    poll(&net, &model, 4);
    inject(&model, 1, data, 20, true, peer, 99);
    assert(!mt_net_receive(&net, id, 7));
    poll(&net, &model, 3);
    assert(mt_net_receive_result(&net, id, received, sizeof(received), &dg) == 7 && dg == 20 &&
           get16(model.mem + 0x526) == 0);
    {
        uint8_t wrong[4] = {10, 0, 0, 3};
        inject(&model, 1, data, 2, true, wrong, 99);
        assert(!mt_net_receive(&net, id, 7));
        poll(&net, &model, 3);
        assert(mt_net_receive_result(&net, id, received, sizeof(received), &dg) == MT_NET_AGAIN &&
               get16(model.mem + 0x526) == 0);
    }
    mt_net_close_owner(&net, MT_NET_OWNER_UCI);
    poll(&net, &model, 3);
    assert(mt_net_state(&net, id, NULL) == MT_NET_S_FREE);
    /* UDP empty datagrams still issue SEND, and live local ports cannot collide. */
    id = mt_net_open(&net, MT_NET_OWNER_UCI, true, peer, 99, false);
    poll(&net, &model, 5);
    model.sent_size = 123;
    assert(!mt_net_send(&net, id, NULL, 0));
    poll(&net, &model, 5);
    assert(mt_net_send_result(&net, id) == 0 && model.sent_size == 0);
    net.ephemeral = net.sockets[id].local_port;
    int second = mt_net_open(&net, MT_NET_OWNER_HTTP, false, peer, 80, false);
    assert(second > 0 && net.sockets[second].local_port != net.sockets[id].local_port);
    int third = mt_net_open(&net, MT_NET_OWNER_HTTP, false, peer, 80, false);
    assert(third > 0);
    poll(&net, &model, 5);
    assert(!mt_net_close(&net, second));
    assert(mt_net_open(&net, MT_NET_OWNER_UCI, false, peer, 80, false) == MT_NET_AGAIN);
    poll(&net, &model, 4);
    assert(mt_net_open(&net, MT_NET_OWNER_UCI, false, peer, 80, false) > 0);
    mt_net_close_owner(&net, MT_NET_OWNER_UCI);
    mt_net_close_owner(&net, MT_NET_OWNER_HTTP);
    poll(&net, &model, 5);
    /* DNS uses the real system UDP socket and validates its peer. A TTL0
     * result is consumed once and a second lookup must send a new query. */
    memcpy(net.dns[0], peer, 4);
    assert(mt_net_resolve(&net, "a.com", received) == MT_NET_AGAIN);
    poll(&net, &model, 10);
    {
        uint8_t p[100] = {0};
        size_t q = model.sent_size;
        assert(q == 23 && model.sent[12] == 1 && model.sent[13] == 'a');
        memcpy(p, model.sent, q);
        p[2] = 0x81;
        p[3] = 0x80;
        p[7] = 1;
        p[q++] = 0xc0;
        p[q++] = 12;
        p[q++] = 0;
        p[q++] = 1;
        p[q++] = 0;
        p[q++] = 1;
        put32(p + q, 0);
        q += 4;
        p[q++] = 0;
        p[q++] = 4;
        memcpy(p + q, peer, 4);
        q += 4;
        inject(&model, 0, p, q, true, (const uint8_t *)"\x0a\x00\x00\x03", 53);
        poll(&net, &model, 8);
        assert(net.query_pending);
        inject(&model, 0, p, q, true, peer, 53);
        poll(&net, &model, 8);
        assert(mt_net_resolve(&net, "a.com", received) == 0 && !memcmp(received, peer, 4));
        assert(mt_net_resolve(&net, "a.com", received) == MT_NET_AGAIN);
        mt_net_resolve_cancel(&net);
    }
    /* DNS compression and zero TTL, plus a compression pointer loop. */
    {
        uint8_t p[] = {0x12, 0x34, 0x81, 0x80, 0,   1, 0, 1, 0, 0, 0,    0,  1,
                       'a',  3,    'c',  'o',  'm', 0, 0, 1, 0, 1, 0xc0, 12, 0,
                       1,    0,    1,    0,    0,   0, 0, 0, 4, 1, 2,    3,  4};
        uint8_t ip[4];
        assert(!mt_net_dns_parse(p, sizeof(p), 0x1234, "A.COM", ip, &ttl) && ttl == 0 &&
               ip[3] == 4);
        p[12] = 0xc0;
        p[13] = 12;
        assert(mt_net_dns_parse(p, sizeof(p), 0x1234, "a.com", ip, &ttl) < 0);
    }
    /* Two-second DHCP lease is not extended to60 seconds; expiry closes
     * clients and clears the address, without resetting the controller. */
    c.mode = MT_NET_DHCP;
    mt_net_configure(&net, &c);
    poll(&net, &model, 15);
    {
        uint8_t p[280] = {0};
        size_t at = 240;
        p[0] = 2;
        p[1] = 1;
        p[2] = 6;
        put32(p + 4, net.xid);
        memcpy(p + 28, c.mac, 6);
        put32(p + 236, 0x63825363);
        memcpy(p + 16, peer, 4);
        p[at++] = 53;
        p[at++] = 1;
        p[at++] = 2;
        p[at++] = 54;
        p[at++] = 4;
        memcpy(p + at, peer, 4);
        at += 4;
        p[at++] = 255;
        assert(model.sent[0] == 1 && get16(model.sent + 10) == 0x8000 && model.sent[242] == 1);
        inject(&model, 0, p, at, true, peer, 67);
        poll(&net, &model, 8);
        assert(net.dhcp_state == 3);
        p[242] = 5;
        at--;
        p[at++] = 51;
        p[at++] = 4;
        put32(p + at, 2);
        at += 4;
        p[at++] = 255;
        mt_net_dhcp_packet(&net, p, at, peer, 67);
        assert(net.lease_expiry == net.now + 2000000 && net.t1 == net.now + 1000000);
        id = mt_net_open(&net, MT_NET_OWNER_UCI, false, peer, 80, false);
        poll(&net, &model, 5);
        model.time = net.lease_expiry;
        poll(&net, &model, 1);
        assert(net.ip[0] == 0 && net.sockets[id].state == MT_NET_S_CLOSED && model.run);
    }
    /* Stuck command becomes a chip fault/restart, never an unbounded loop. */
    mt_net_configure(&net, &parsed);
    model.stuck = true;
    poll(&net, &model, 30);
    assert(net.failures > 0);
    model.stuck = false;
    puts("PASS network config, ringIO, UDP filtering/truncation, FIN/EOF, DNS bounds, shortlease, "
         "command timeout, per-pass budget");
    return 0;
}
