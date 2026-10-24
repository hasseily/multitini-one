/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MT_HTTP_INTERNAL_H
#define MT_HTTP_INTERNAL_H
#include "multitini_http.h"
#define HTTP_BODY_LIMIT 65536U
#define HTTP_NODE_LIMIT 1024U
typedef struct http_json {
    struct http_json *parent,*child,*next;
    char *key,*text;int32_t integer;uint8_t type;
} http_json_t;
typedef struct {uint8_t format;http_json_t *root,*current;uint8_t *binary;size_t length;unsigned nodes;} http_body_t;
typedef struct {uint8_t verb;uint16_t port;bool secure;char host[254],path[896],lines[4096];size_t length;} http_header_t;
http_json_t *http_json_new(uint8_t);
void http_json_free(http_json_t *);
unsigned http_json_count(http_json_t *);
http_json_t *http_json_path(http_json_t *,const char *);
bool http_json_attach(http_body_t *,http_json_t *,const char *,http_json_t *);
http_json_t *http_json_decode(const uint8_t *,size_t,size_t *,unsigned,unsigned *);
http_json_t *http_json_parse(const char *,size_t);
uint8_t *http_json_render(http_json_t *,bool,size_t *);
uint8_t *http_json_encode(http_json_t *,size_t *);
http_body_t *http_body_new(uint8_t);
void http_body_free(http_body_t *);
bool http_body_clear(http_body_t *);
bool http_body_command(http_body_t *,uint8_t,const uint8_t *,size_t,uint8_t **,size_t *);
#endif
