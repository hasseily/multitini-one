/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_net.h"
#include <assert.h>
#include <string.h>
#ifndef TEST_NET_MODEL_H
#define TEST_NET_MODEL_H
typedef struct {
    uint8_t mem[32768];
    uint16_t rx_ack[4], tx_ack[4];
    uint64_t time;
    unsigned accesses, packets;
    bool run, stuck, fail;
    uint8_t sent[2048];
    size_t sent_size;
} model_t;
static uint16_t get16(uint8_t *p) { return (uint16_t)((unsigned)p[0] * 256 + p[1]); }
static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static inline void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static int access_reg(void *context, uint16_t a, bool write, uint8_t d, uint8_t *out) {
    model_t *m = context;
    unsigned s, b;
    m->accesses++;
    assert(a < 32768);
    if (m->fail)
        return -1;
    if (!write) {
        *out = m->mem[a];
        return 0;
    }
    if (a >= 0x400 && a < 0x800 && (a & 255) == 2) {
        m->mem[a] &= (uint8_t)~d;
        return 0;
    }
    m->mem[a] = d;
    if (a >= 0x400 && a < 0x800 && (a & 255) == 1 && !m->stuck) {
        s = (a - 0x400) / 256;
        b = 0x400 + s * 256;
        m->mem[a] = 0;
        switch (d) {
        case 1:
            m->mem[b + 3] = m->mem[b] == 2 ? 0x22 : 0x13;
            put16(m->mem + b + 0x20, 2048);
            break;
        case 2:
            m->mem[b + 3] = 0x14;
            break;
        case 4:
            m->mem[b + 3] = 0x17;
            break;
        case 8:
        case 0x10:
            m->mem[b + 3] = 0;
            break;
        case 0x20: {
            unsigned ptr = get16(m->mem + b + 0x24), len = (uint16_t)(ptr - m->tx_ack[s]), i;
            assert(len <= 2048);
            m->sent_size = len;
            for (i = 0; i < len; i++)
                m->sent[i] = m->mem[0x4000 + s * 0x800 + ((m->tx_ack[s] + i) & 2047)];
            m->tx_ack[s] = (uint16_t)ptr;
            m->packets++;
            m->mem[b + 2] |= 16;
            break;
        }
        case 0x40: {
            unsigned ptr = get16(m->mem + b + 0x28), used = (uint16_t)(ptr - m->rx_ack[s]),
                     count = get16(m->mem + b + 0x26);
            assert(used <= count);
            put16(m->mem + b + 0x26, count - used);
            m->rx_ack[s] = (uint16_t)ptr;
            break;
        }
        default:
            assert(0);
        }
    }
    return 0;
}
static inline uint64_t now(void *context) { return ((model_t *)context)->time; }
static void run(void *context, bool enabled) {
    model_t *m = context;
    m->run = enabled;
    if (!enabled) {
        memset(m->mem, 0, sizeof(m->mem));
        memset(m->rx_ack, 0, sizeof(m->rx_ack));
        memset(m->tx_ack, 0, sizeof(m->tx_ack));
        m->mem[0x80] = 0x51;
        m->mem[0x3c] = 1;
    }
}
static inline bool ready(void *context) { return ((model_t *)context)->run; }
static inline void poll(mt_net_t *n, model_t *m, unsigned cycles) {
    while (cycles--) {
        unsigned before = m->accesses;
        m->time += 1000;
        mt_net_poll(n);
        assert(m->accesses - before <= 1024);
    }
}
static inline void inject(model_t *m, unsigned s, const uint8_t *p, size_t len, bool udp,
                          const uint8_t ip[4], unsigned port) {
    unsigned b = 0x400 + s * 256, ptr = m->rx_ack[s], i;
    uint8_t hdr[8];
    assert(!get16(m->mem + b + 0x26));
    if (udp) {
        memcpy(hdr, ip, 4);
        put16(hdr + 4, port);
        put16(hdr + 6, (unsigned)len);
        for (i = 0; i < 8; i++)
            m->mem[0x6000 + s * 0x800 + ((ptr + i) & 2047)] = hdr[i];
        ptr += 8;
    }
    for (i = 0; i < len; i++)
        m->mem[0x6000 + s * 0x800 + ((ptr + i) & 2047)] = p[i];
    put16(m->mem + b + 0x26, (unsigned)len + (udp ? 8 : 0));
}
#endif
