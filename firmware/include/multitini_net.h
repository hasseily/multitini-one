/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_NET_H
#define MULTITINI_NET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MT_NET_PACKET 1472U
#define MT_NET_HANDLES 16U
#define MT_NET_NAME 254U
#define MT_NET_AGAIN (-11)
typedef enum { MT_NET_OFF, MT_NET_DHCP, MT_NET_STATIC } mt_net_mode_t;
typedef enum { MT_NET_OWNER_SYSTEM, MT_NET_OWNER_UCI, MT_NET_OWNER_HTTP } mt_net_owner_t;
typedef struct {
    mt_net_mode_t mode;
    uint8_t ip[4],mask[4],gateway[4],dns[2][4],mac[6];
    char hostname[64];
    bool mac_explicit,mac_valid;
    uint32_t mac_card;
} mt_net_config_t;
typedef struct {
    void *context;
    /* Bounded single-byte transaction, zero on success. No sleeps. */
    int (*access)(void *,uint16_t,bool,uint8_t,uint8_t *);
    uint64_t (*now_us)(void *);
    void (*run)(void *,bool);
    bool (*ready)(void *);
} mt_net_ops_t;
typedef enum { MT_NET_S_FREE,MT_NET_S_OPENING,MT_NET_S_CONNECTING,
    MT_NET_S_LISTENING,MT_NET_S_CONNECTED,MT_NET_S_CLOSING,MT_NET_S_CLOSED } mt_net_socket_state_t;
typedef struct {
    mt_net_socket_state_t state;
    mt_net_owner_t owner;
    int8_t hardware;
    bool udp,listen,command_pending,send_active,receive_active;
    uint8_t stage,command,ir,peer[4],rx_peer[4];
    uint16_t local_port,port,rx_port,pointer,rx_pointer,tx_length,tx_offset,rx_length,rx_offset,rx_capacity,datagram_length;
    int error,send_result,receive_result;
    uint64_t deadline,command_deadline,send_deadline;
    uint8_t tx_phase,rx_phase;
    uint8_t tx[MT_NET_PACKET],rx[MT_NET_PACKET+8];
} mt_net_socket_t;
typedef struct { char name[MT_NET_NAME];uint8_t ip[4];uint64_t expires,used; } mt_net_dns_cache_t;
typedef struct {
    mt_net_ops_t ops;mt_net_config_t config;
    mt_net_socket_t sockets[MT_NET_HANDLES];int8_t hardware[4];
    uint64_t close_deadline[4];
    uint8_t ip[4],mask[4],gateway[4],dns[2][4],next_id,init_stage,failures,poll_cursor;
    bool enabled,ready,link,runtime_address,fault;
    uint16_t ephemeral,budget;uint32_t transactions;
    uint64_t random,now,retry_at,link_check;
    uint8_t dhcp_state,dhcp_attempt,server[4],offered[4];uint32_t xid;
    uint64_t lease_expiry,t1,t2,dhcp_due;
    bool dhcp_tx_wait;
    mt_net_dns_cache_t cache[8];char query[MT_NET_NAME];uint8_t query_ip[4];
    uint8_t query_server,query_attempt;uint16_t query_id;
    int query_result;bool query_pending,query_sent;uint64_t query_due;
    mt_net_owner_t query_owner;
    uint8_t packet[MT_NET_PACKET];
} mt_net_t;
void mt_net_config_default(mt_net_config_t *);
bool mt_net_config_set(mt_net_config_t *,const char *,const char *);
bool mt_net_config_parse(mt_net_config_t *,const char *,size_t,bool *unknown);
size_t mt_net_config_format(const mt_net_config_t *,char *,size_t);
bool mt_net_ipv4(const char *,uint8_t[4]);
bool mt_net_mac_valid(const uint8_t[6]);
void mt_net_generate_mac(mt_net_config_t *,const void *,size_t,uint64_t);
void mt_net_init(mt_net_t *,const mt_net_ops_t *,const mt_net_config_t *);
void mt_net_configure(mt_net_t *,const mt_net_config_t *);
void mt_net_poll(mt_net_t *);
void mt_net_shutdown(mt_net_t *);
void mt_net_close_owner(mt_net_t *,mt_net_owner_t);
bool mt_net_available(const mt_net_t *);
void mt_net_renew(mt_net_t *);
void mt_net_set_address(mt_net_t *,const uint8_t[12]);
int mt_net_open(mt_net_t *,mt_net_owner_t,bool udp,const uint8_t[4],uint16_t,bool listener);
mt_net_socket_state_t mt_net_state(mt_net_t *,int id,int *error);
int mt_net_close(mt_net_t *,int id);
int mt_net_send(mt_net_t *,int,const void *,size_t);
int mt_net_send_result(mt_net_t *,int);
int mt_net_receive(mt_net_t *,int,size_t);
/* Result consumes exactly one completed receive; zero TCP means EOF. */
int mt_net_receive_result(mt_net_t *,int,void *,size_t,size_t *datagram_length);
int mt_net_resolve(mt_net_t *,const char *,uint8_t[4]);
void mt_net_resolve_cancel(mt_net_t *);
/* One DNS transaction is shared by the console and guest targets. A target
 * reset must cancel only its own transaction, including a completed result. */
int mt_net_resolve_owned(mt_net_t *,mt_net_owner_t,const char *,uint8_t[4]);
void mt_net_resolve_cancel_owner(mt_net_t *,mt_net_owner_t);
/* Pure wire parsers are exposed for adversarial fixtures. */
int mt_net_dns_parse(const uint8_t *,size_t,uint16_t,const char *,uint8_t[4],uint32_t *ttl);
void mt_net_dhcp_packet(mt_net_t *,const uint8_t *,size_t,const uint8_t[4],uint16_t);
#endif
