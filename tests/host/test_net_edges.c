/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_net_uci.h"
#include "net_model.h"
#include <stdio.h>
static model_t model;
static mt_net_t net;
static mt_net_uci_t uci;
static mt_uci_reply_t reply;
static mt_uci_target_t target;
static const uint8_t peer[4] = {10, 0, 0, 2};
static void setup(void) {
    mt_net_config_t cfg;
    mt_net_ops_t ops = {&model, access_reg, now, run, ready};
    memset(&model, 0, sizeof(model));
    mt_net_config_default(&cfg);
    assert(mt_net_config_set(&cfg, "mode", "static"));
    assert(mt_net_config_set(&cfg, "ip", "10.0.0.1"));
    assert(mt_net_config_set(&cfg, "mac", "02:00:01:02:03:04"));
    mt_net_init(&net, &ops, &cfg);
    poll(&net, &model, 5);
    mt_net_uci_init(&uci, &net);
    target = mt_net_uci_target(&uci);
}
static mt_uci_result_t issue(const uint8_t *data, size_t length) {
    uint8_t command[896] = {0};
    memcpy(command, data, length);
    return target.begin(target.context, command, length, &reply);
}
static bool status_is(const char *text) {
    return reply.status_length == strlen(text) && !memcmp(reply.status, text, strlen(text));
}
static mt_uci_result_t step(void) {
    poll(&net, &model, 1);
    return target.poll(target.context, &reply);
}
static void read_deadline(void) {
    /* Exercise both the partially copied ring and committed RECV seams.
     * The last foreground copy occurs at the 40ms no-data deadline. */
    const unsigned lengths[] = {1, 128, 129, 1472};
    uint8_t data[1472], output[1472];
    for (unsigned i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i ^ 0xa7);
    for (unsigned trial = 0; trial < sizeof(lengths) / sizeof(lengths[0]); trial++) {
        setup();
        int id = mt_net_open(&net, MT_NET_OWNER_UCI, false, peer, 80, false);
        assert(id > 0);
        poll(&net, &model, 5);
        unsigned hw = (unsigned)net.sockets[id].hardware;
        uint8_t cmd[] = {3, 0x10, (uint8_t)id, (uint8_t)lengths[trial],
                         (uint8_t)(lengths[trial] >> 8)};
        assert(issue(cmd, sizeof(cmd)) == MT_UCI_WAIT);
        for (unsigned i = 0; i < 39; i++) assert(step() == MT_UCI_WAIT);
        inject(&model, hw, data, lengths[trial], false, peer, 80);
        mt_uci_result_t state = step();
        assert(state == MT_UCI_WAIT); /* data is copying/awaiting RECV completion */
        for (unsigned i = 0; state == MT_UCI_WAIT; i++) {
            assert(i < 20);
            state = step();
        }
        assert(state == MT_UCI_REPLY && reply.data_length >= 2);
        assert(((unsigned)reply.data[0] | ((unsigned)reply.data[1] << 8)) == lengths[trial]);
        size_t size = reply.data_length - 2;
        memcpy(output, reply.data + 2, size);
        while (reply.more) {
            assert(target.next(target.context, reply.data_length, &reply) == MT_UCI_REPLY);
            assert(reply.data_length <= sizeof(output) - size);
            memcpy(output + size, reply.data, reply.data_length);
            size += reply.data_length;
        }
        assert(status_is("00,OK") && size == lengths[trial] && !memcmp(output, data, size));
        assert(get16(model.mem + 0x426 + hw * 256) == 0);
        /* Nothing received still times out; later data belongs to the next READ. */
        assert(issue(cmd, sizeof(cmd)) == MT_UCI_WAIT);
        for (unsigned i = 0; i < 39; i++) assert(step() == MT_UCI_WAIT);
        assert(step() == MT_UCI_REPLY && status_is("02,NO DATA: 11"));
        assert(!net.sockets[id].receive_active);
        inject(&model, hw, data, 1, false, peer, 80);
        assert(issue(cmd, sizeof(cmd)) == MT_UCI_WAIT);
        assert(step() == MT_UCI_WAIT);
        assert(step() == MT_UCI_REPLY && status_is("00,OK") && reply.data[0] == 1 &&
               reply.data[2] == data[0]);
    }
    puts("PASS READ deadline preserves partial and committed receives; empty timeout/next READ");
}
static void connect_fin(void) {
    setup();
    const uint8_t cmd[] = {3, 7, 80, 0, '1', '0', '.', '0', '.', '0', '.', '2'};
    assert(issue(cmd, sizeof(cmd)) == MT_UCI_WAIT);
    /* CONNECT has reached hardware but software has not sampled ESTABLISHED. */
    assert(step() == MT_UCI_WAIT);
    assert(step() == MT_UCI_WAIT);
    int id = uci.socket;
    assert(net.sockets[id].state == MT_NET_S_CONNECTING);
    unsigned hw = (unsigned)net.sockets[id].hardware, b = 0x400 + hw * 256;
    inject(&model, hw, (const uint8_t *)"banner", 6, false, peer, 80);
    model.mem[b + 3] = 0x1c;
    assert(step() == MT_UCI_REPLY && status_is("00,OK") && reply.data[0] == id);
    uint8_t read[] = {3, 0x10, (uint8_t)id, 16, 0};
    assert(issue(read, sizeof(read)) == MT_UCI_WAIT);
    assert(step() == MT_UCI_WAIT);
    assert(step() == MT_UCI_REPLY && status_is("00,OK") && reply.data[0] == 6 &&
           !memcmp(reply.data + 2, "banner", 6));
    assert(issue(read, sizeof(read)) == MT_UCI_WAIT);
    assert(step() == MT_UCI_REPLY && status_is("01,CONNECTION CLOSED BY HOST"));
    assert(mt_net_state(&net, id, NULL) == MT_NET_S_FREE);
    puts("PASS outgoing connect/banner/FIN between polls returns data then EOF");
}
static void listen_fin(void) {
    setup();
    for (unsigned trial = 0; trial < 5; trial++) {
        const uint8_t listen[] = {3, 0x12, 0x34, 0x12}, state[] = {3, 0x14}, accept[] = {3, 0x15};
        assert(issue(listen, sizeof(listen)) == MT_UCI_REPLY && reply.data[0] == 1);
        poll(&net, &model, 3);
        int id = uci.listener;
        assert(net.sockets[id].state == MT_NET_S_LISTENING);
        unsigned hw = (unsigned)net.sockets[id].hardware, b = 0x400 + hw * 256;
        memcpy(model.mem + b + 0x0c, peer, 4);
        put16(model.mem + b + 0x10, 5555);
        inject(&model, hw, (const uint8_t *)"ok", 2, false, peer, 5555);
        model.mem[b + 3] = 0x1c; /* connect/send/FIN all between software polls */
        poll(&net, &model, 1);
        assert(issue(state, sizeof(state)) == MT_UCI_REPLY && reply.data[0] == 2);
        assert(issue(accept, sizeof(accept)) == MT_UCI_REPLY && reply.data[0] == id);
        assert(net.sockets[id].port == 5555 && !memcmp(net.sockets[id].peer, peer, 4));
        uint8_t read[] = {3, 0x10, (uint8_t)id, 8, 0};
        assert(issue(read, sizeof(read)) == MT_UCI_WAIT);
        assert(step() == MT_UCI_WAIT);
        assert(step() == MT_UCI_REPLY && status_is("00,OK") && reply.data[0] == 2 &&
               !memcmp(reply.data + 2, "ok", 2));
        assert(issue(read, sizeof(read)) == MT_UCI_WAIT);
        assert(step() == MT_UCI_REPLY && status_is("01,CONNECTION CLOSED BY HOST"));
        poll(&net, &model, 2); /* retire the physical close before explicit rearm */
    }
    puts("PASS listener quick connect/send/FIN and explicit rearm reuse after EOF");
}
int main(int argc, char **argv) {
    if (argc == 1 || !strcmp(argv[1], "read")) read_deadline();
    if (argc == 1 || !strcmp(argv[1], "connect")) connect_fin();
    if (argc == 1 || !strcmp(argv[1], "listen")) listen_fin();
    return 0;
}
