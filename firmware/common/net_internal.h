/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MT_NET_INTERNAL_H
#define MT_NET_INTERNAL_H
#include "multitini_net.h"
#include <string.h>
#define NET_US UINT64_C(1000000)
static inline uint16_t net_be16(const uint8_t *p){return (uint16_t)((unsigned)p[0]*256+p[1]);}
static inline uint32_t net_be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static inline void net_put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static inline void net_put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static inline bool net_zero(const uint8_t ip[4]){return !(ip[0]|ip[1]|ip[2]|ip[3]);}
uint32_t net_random(mt_net_t *);
void net_protocol_poll(mt_net_t *);
void net_address_apply(mt_net_t *,const uint8_t *,const uint8_t *,const uint8_t *,bool);
void net_protocol_restart(mt_net_t *);
#endif
