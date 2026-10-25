/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_HTTP_H
#define MULTITINI_HTTP_H
#include "multitini_net.h"
#include "multitini_c64_uci.h"
/* Slot and exchange storage lives in ARM DDR, allocated only while used. */
typedef struct {
    mt_net_t *net;void *headers[16],*bodies[16];
    int socket;uint8_t command,phase,request_header,request_body;
    uint8_t *request,*object;size_t request_size,request_sent,object_size;
    uint8_t input[1472],output[895];size_t input_size,input_at,output_size;
    char response_header[4096],response_status[255],chunk_line[128];
    size_t response_header_size,chunk_line_size,reply_offset,reply_size;
    uint8_t *reply_data;uint64_t deadline,remaining,chunk_remaining;
    bool raw,chunked,length_known,body_done,first_block,receive_wait,send_wait;
    unsigned chunk_state,trailers_size;
} mt_http_t;
void mt_http_init(mt_http_t *,mt_net_t *);
/* End the HTTP session and release all object handles. A UCI target reset only
 * cancels its active exchange: Ultimate keeps these handles across C64 reset. */
void mt_http_shutdown(mt_http_t *);
mt_uci_target_t mt_http_uci_target(mt_http_t *);
#endif
