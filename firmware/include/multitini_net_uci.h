/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_NET_UCI_H
#define MULTITINI_NET_UCI_H
#include "multitini_net.h"
#include "multitini_c64_uci.h"
typedef struct {
    mt_net_t *net;uint8_t command,phase;int socket,listener;uint8_t listener_error;
    uint16_t port,requested;uint64_t deadline;char host[MT_NET_NAME];
    uint8_t payload[MT_NET_PACKET];size_t length,offset;char status[80];
} mt_net_uci_t;
void mt_net_uci_init(mt_net_uci_t *,mt_net_t *);
mt_uci_target_t mt_net_uci_target(mt_net_uci_t *);
#endif
