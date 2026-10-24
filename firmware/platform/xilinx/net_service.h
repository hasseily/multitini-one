/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MT_PLATFORM_NET_SERVICE_H
#define MT_PLATFORM_NET_SERVICE_H
#include "multitini_core_link.h"
#include "multitini_c64_uci.h"
#include "multitini_net.h"
typedef void (*mt_net_output_fn)(void *,const char *);
void mt_net_service_init(mt_core_link_t *);
void mt_net_service_seed(const void *,size_t);
void mt_net_service_poll(void);
void mt_net_service_shutdown(void);
void mt_net_service_guest_reset(void);
bool mt_net_service_set_mode(mt_net_mode_t);
bool mt_net_service_cycle_mode(void);
void mt_net_service_renew(void);
mt_net_mode_t mt_net_service_mode(void);
void mt_net_service_status(char *,size_t);
bool mt_net_service_command(const char *,mt_net_output_fn,void *);
mt_uci_target_t mt_net_service_network_target(void);
mt_uci_target_t mt_net_service_http_target(void);
#endif
