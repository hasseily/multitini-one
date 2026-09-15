/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX32_SERVICE_H
#define MULTITINI_VECTREX32_SERVICE_H
#include "multitini_core_link.h"
typedef struct {
    void *context;
    int (*service)(void *); /* false requests an intentional interpreter stop */
    uint64_t (*milliseconds)(void *);
    void (*controls)(void *, int [2][6]);
    void (*text)(void *, const char *);
    /* Optional wait hook: service USB/cancel, but never issue core commands. */
    int (*stream_service)(void *);
    int (*console_read_byte)(void *); /* 0..255, -1 not ready, -2 EOF, -3 error */
    int (*console_write)(void *, const void *, size_t); /* 0 success, -1 error, -2 stop */
} mt_vectrex32_platform_ops_t;
void vectrex32_service_init(mt_core_link_t *, const mt_vectrex32_platform_ops_t *);
int vectrex32_service_run(void *, const char *root, const char *name, const char *source);
const char *vectrex32_service_error(void);
#endif
