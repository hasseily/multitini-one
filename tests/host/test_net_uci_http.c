/* SPDX-License-Identifier: GPL-3.0-only */
#include "../../firmware/common/http_internal.h"
#include "multitini_http.h"
#include "multitini_net_uci.h"
#include "net_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static model_t model;
static mt_net_t net;
static mt_net_uci_t uci;
static mt_http_t http;
static mt_uci_reply_t reply;
static uint8_t command[896];
static bool status_is(const char *text) {
    return reply.status_length == strlen(text) && !memcmp(reply.status, text, strlen(text));
}
static mt_uci_result_t issue(mt_uci_target_t t, const uint8_t *c, size_t size) {
    memset(command, 0xcc, sizeof(command));
    memcpy(command, c, size);
    return t.begin(t.context, command, size, &reply);
}
static unsigned await_reply(mt_uci_target_t t, mt_uci_result_t state) {
    unsigned steps = 0;
    while (state == MT_UCI_WAIT) {
        assert(++steps < 1000);
        poll(&net, &model, 1);
        state = t.poll(t.context, &reply);
    }
    assert(state == MT_UCI_REPLY);
    return steps;
}
static size_t exchange(mt_uci_target_t t, unsigned header, bool raw, const char *response,
                       uint8_t *output, size_t cap) {
    uint8_t c[] = {6, raw ? 0x32 : 0x31, (uint8_t)header, 255};
    mt_uci_result_t state = issue(t, c, 4);
    size_t sent = 0, size = strlen(response), total = 0;
    unsigned steps = 0;
    for (;;) {
        assert(++steps < 4000);
        if (state == MT_UCI_REPLY) {
            assert(reply.data_length <= cap - total);
            memcpy(output + total, reply.data, reply.data_length);
            total += reply.data_length;
            if (!reply.more)
                break;
            state = t.next(t.context, reply.data_length, &reply);
            continue;
        }
        assert(state == MT_UCI_WAIT);
        poll(&net, &model, 1);
        if (http.phase >= 3 && http.socket > 0 && net.sockets[http.socket].hardware >= 0) {
            unsigned hw = (unsigned)net.sockets[http.socket].hardware, b = 0x400 + hw * 256;
            if (sent < size && !get16(model.mem + b + 0x26)) {
                size_t n = size - sent;
                if (n > 1400)
                    n = 1400;
                inject(&model, hw, (const uint8_t *)response + sent, n, false,
                       (const uint8_t *)"\x0a\x00\x00\x02", 80);
                sent += n;
            }
            if (sent == size && strstr(response, "Content-Length:") == NULL &&
                strstr(response, "Transfer-Encoding:") == NULL)
                model.mem[b + 3] = 0x1c;
        }
        state = t.poll(t.context, &reply);
    }
    return total;
}
int main(void) {
    mt_net_config_t cfg;
    mt_net_ops_t ops = {&model, access_reg, now, run, ready};
    mt_uci_target_t nt, ht;
    int id, other;
    uint8_t output[4000], payload[1472];
    unsigned i, header, body;
    mt_net_config_default(&cfg);
    assert(mt_net_config_set(&cfg, "mode", "static"));
    assert(mt_net_config_set(&cfg, "ip", "10.0.0.1"));
    assert(mt_net_config_set(&cfg, "mac", "02:00:01:02:03:04"));
    mt_net_init(&net, &ops, &cfg);
    poll(&net, &model, 5);
    mt_net_uci_init(&uci, &net);
    mt_http_init(&http, &net);
    nt = mt_net_uci_target(&uci);
    ht = mt_http_uci_target(&http);
    {
        const uint8_t c[] = {3, 1};
        assert(issue(nt, c, 2) == MT_UCI_REPLY && status_is("00,OK") &&
               reply.data_length == strlen("ULTIMATE-II NETWORK INTERFACE V1.0"));
    }
    {
        const uint8_t c[] = {3, 7, 80, 0, '1', '0', '.', '0', '.', '0', '.', '2'};
        await_reply(nt, issue(nt, c, sizeof(c)));
        assert(status_is("00,OK") && reply.data[0]);
        id = reply.data[0];
    }
    /* READ1472 really spans893+579 bytes, then exactly one final status. */
    for (i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)i;
    inject(&model, (unsigned)net.sockets[id].hardware, payload, sizeof(payload), false, cfg.ip, 80);
    {
        uint8_t c[] = {3, 0x10, (uint8_t)id, 0xc0, 5};
        await_reply(nt, issue(nt, c, 5));
        assert(reply.more && reply.data_length == 895 && reply.status_length == 0 &&
               reply.data[0] == 0xc0 && reply.data[1] == 5);
        assert(!memcmp(reply.data + 2, payload, 893));
        assert(nt.next(nt.context, 895, &reply) == MT_UCI_REPLY && !reply.more &&
               reply.data_length == 579 && status_is("00,OK"));
        assert(!memcmp(reply.data, payload + 893, 579));
    }
    {
        uint8_t c[] = {3, 0x10, (uint8_t)id, 1, 0};
        unsigned steps = await_reply(nt, issue(nt, c, 5));
        assert(steps >= 40 && status_is("02,NO DATA: 11") && reply.data[0] == 255);
    }
    {
        const uint8_t c[] = {3, 0x11};
        assert(issue(nt, c, 2) == MT_UCI_REPLY && status_is("81,INVALID PARAMS"));
    }
    other =
        mt_net_open(&net, MT_NET_OWNER_HTTP, false, (const uint8_t *)"\x0a\x00\x00\x02", 80, false);
    poll(&net, &model, 5);
    {
        uint8_t c[] = {3, 9, (uint8_t)other};
        assert(issue(nt, c, 3) == MT_UCI_REPLY && status_is("12,ERROR ON CLOSE: 9"));
    }
    mt_net_close_owner(&net, MT_NET_OWNER_HTTP);
    poll(&net, &model, 4);
    {
        const uint8_t c[] = {3, 0x12, 0x34, 0x12};
        assert(issue(nt, c, 4) == MT_UCI_REPLY && reply.data_length == 2 && reply.data[0] == 1);
        poll(&net, &model, 5);
        assert(net.sockets[uci.listener].local_port == 0x1234);
        model.mem[0x403 + net.sockets[uci.listener].hardware * 256] = 0x17;
        poll(&net, &model, 1);
    }
    {
        const uint8_t c[] = {3, 0x14};
        assert(issue(nt, c, 2) == MT_UCI_REPLY && reply.data[0] == 2);
    }
    {
        const uint8_t c[] = {3, 0x15};
        assert(issue(nt, c, 2) == MT_UCI_REPLY && reply.data_length == 2 &&
               reply.data[0] == uci.listener && reply.data[1] == 0);
    }
    nt.reset(nt.context);
    poll(&net, &model, 4);
    assert(!uci.listener);
    /* TLS refusal must issue no socket open and no request bytes. */
    {
        const uint8_t c[] = {6,   0x11, 1,   'h', 't', 't', 'p', 's', ':', '/',
                             '/', '1',  '0', '.', '0', '.', '0', '.', '2', '/'};
        unsigned before = model.packets;
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && status_is("501 HTTPS NOT SUPPORTED") &&
               model.packets == before);
    }
    {
        const uint8_t c[] = {6,   0x11, 1,   'h', 't', 't', 'p', ':', '/', '/',
                             '1', '0',  '.', '0', '.', '0', '.', '2', '/', 'a'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && status_is("000 OK"));
        header = reply.data[0];
    }
    {
        uint8_t c[] = {6, 0x13, (uint8_t)header, 'X', '-', 'T', ':', ' ', 'o', 'n', 'e'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && status_is("000 OK"));
        c[8] = 't';
        c[9] = 'w';
        c[10] = 'o';
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY);
    }
    {
        uint8_t c[] = {6, 0x14, (uint8_t)header, 'x', '-', 't'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && reply.data_length == 3 &&
               !memcmp(reply.data, "two", 3));
    }
    {
        const uint8_t c[] = {6, 0x21, 2};
        assert(issue(ht, c, 3) == MT_UCI_REPLY);
        body = reply.data[0];
    }
    {
        uint8_t c[] = {6, 0x23, (uint8_t)body, 1, 'n', 0xfe};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && status_is("000 OK"));
    }
    {
        uint8_t c[] = {6, 0x2a, (uint8_t)body, 'n'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && reply.data_length == 5 &&
               reply.data[0] == 1 && reply.data[1] == 254 && reply.data[4] == 255);
    }
    {
        uint8_t c[] = {6, 0x26, (uint8_t)body, 1, 'o'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY);
    }
    {
        uint8_t c[] = {6, 0x25, (uint8_t)body, 1, 's', 3, 'a', '"', 'b'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY);
    }
    {
        uint8_t c[] = {6, 0x2a, (uint8_t)body, 'o', '/', 's'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && reply.data_length == 5 &&
               reply.data[0] == 3 && reply.data[1] == 3);
    }
    {
        uint8_t c[] = {6, 0x29, (uint8_t)body, 'o'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY);
        assert(((http_body_t *)http.bodies[body])->current ==
               ((http_body_t *)http.bodies[body])->root);
    }
    {
        uint8_t c[] = {6, 0x2d, (uint8_t)body, 1, 'a', 5, 2, 1, 1, 0, 0, 0, 2, 1};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && status_is("000 OK"));
    }
    {
        uint8_t c[] = {6, 0x2a, (uint8_t)body, 'a', '%', '1'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && reply.data_length == 2 &&
               reply.data[0] == 2 && reply.data[1] == 1);
    }
    {
        uint8_t c[] = {6, 0x2d, (uint8_t)body, 20, 'x'};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && status_is("400 BAD FORMAT"));
    }
    /* Real async TCP path, response header split across arbitrary reads. */
    assert(exchange(ht, header, true, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", output,
                    sizeof(output)) == 5 &&
           !memcmp(output, "hello", 5));
    poll(&net, &model, 4);
    {
        char response[2100];
        size_t prefix = (size_t)snprintf(response, sizeof(response),
                                         "HTTP/1.1 200 OK\r\nContent-Length: 2000\r\n\r\n");
        for (i = 0; i < 2000; i++)
            response[prefix + i] = (char)('a' + i % 26);
        response[prefix + 2000] = 0;
        assert(exchange(ht, header, true, response, output, sizeof(output)) == 2000 &&
               !memcmp(output, response + prefix, 2000));
        poll(&net, &model, 4);
    }
    assert(exchange(ht, header, true,
                    "HTTP/1.1 200 OK\r\nTransfer-Encoding: "
                    "chunked\r\n\r\n3\r\nabc\r\n2;ext=x\r\nde\r\n0\r\nX-T: z\r\n\r\n",
                    output, sizeof(output)) == 5 &&
           !memcmp(output, "abcde", 5));
    poll(&net, &model, 4);
    assert(exchange(ht, header, false, "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\n{\"x\":3}",
                    output, sizeof(output)) == 2 &&
           status_is("200 OK"));
    poll(&net, &model, 4);
    {
        uint8_t c[] = {6, 0x2a, output[1], 'x'};
        assert(issue(ht, c, 4) == MT_UCI_REPLY && reply.data[0] == 1 && reply.data[1] == 3);
    }
    {
        uint8_t c[] = {6, 0x24, output[1], 1, 'y', 1};
        assert(issue(ht, c, sizeof(c)) == MT_UCI_REPLY && status_is("000 OK"));
    }
    assert(exchange(ht, header, true,
                    "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello",
                    output, sizeof(output)) == 0 &&
           status_is("502 BAD RESPONSE HEADER"));
    poll(&net, &model, 4);
    {
        uint8_t c[] = {6, 0x32, (uint8_t)header, 255};
        mt_uci_result_t state = issue(ht, c, 4);
        assert(state == MT_UCI_WAIT);
        assert(ht.abort(ht.context, 0) == MT_UCI_REPLY);
        poll(&net, &model, 4);
        assert(http.socket == 0 && http.request == NULL);
    }
    /* Decoder bounds against truncated/adversarial typed values and JSON.
     * Deterministic seed keeps failures reproducible under ASan/UBSan. */
    {
        uint32_t seed=0x510064;uint8_t bytes[128];
        for(unsigned trial=0;trial<4000;trial++){
            size_t count=trial%sizeof(bytes),pos=0;unsigned nodes=0;
            for(size_t j=0;j<count;j++){seed=seed*1664525U+1013904223U;bytes[j]=(uint8_t)(seed>>24);}
            http_json_t *node=http_json_decode(bytes,count,&pos,0,&nodes);assert(pos<=count);http_json_free(node);
            node=http_json_parse((const char*)bytes,count);http_json_free(node);
        }
        http_body_t *body=http_body_new(2);assert(body);http_json_t *parent=body->root;
        for(unsigned j=0;j<13;j++){http_json_t *node=http_json_new(4);assert(node&&http_json_attach(body,parent,"a",node));parent=node;}
        http_json_t *sub=http_json_parse("{\"a\":{\"b\":1}}",13);assert(sub);assert(!http_json_attach(body,parent,"too-deep",sub));http_json_free(sub);http_body_free(body);
    }
    ht.reset(ht.context);
    for (i = 0; i < 16; i++)
        assert(!http.headers[i] && !http.bodies[i]);
    puts("PASS network UCI ownership/1472bytes/no-data/listener; HTTP "
         "slots/JSON/TLS-refusal/asyncTCP/content-length/chunks/abort/reset");
    return 0;
}
