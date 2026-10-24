/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MT_C64_UCI_INTERNAL_H
#define MT_C64_UCI_INTERNAL_H
#include "c64_uci_service.h"
#include "sd_storage.h"
#define C64_UCI_PATH 512
typedef struct {
    mt_core_link_t *link;
    c64_uci_callbacks_t callbacks;
    char root[C64_UCI_PATH],home[C64_UCI_PATH],temporary[C64_UCI_PATH],reu_image[C64_UCI_PATH];
    bool available,fault;
} c64_uci_environment_t;
extern c64_uci_environment_t c64_uci_environment;
uint32_t c64_uci_read(unsigned offset);
void c64_uci_write(unsigned offset,uint32_t value);
uint64_t c64_uci_time(void);
bool c64_uci_protected(const char *path,bool writing);
/* Lexical normalization plus U64 aliases. Existing wildcard components are
 * resolved in FatFs directory order; non-existing last component is allowed. */
bool c64_uci_path(const char *cwd,const uint8_t *name,size_t length,char out[C64_UCI_PATH]);
const char *c64_uci_sd_status(mt_sd_result_t);
void c64_uci_reu_read(uint32_t address,void *data,size_t bytes);
void c64_uci_reu_write(uint32_t address,const void *data,size_t bytes);
void c64_uci_reu_drain(void);
/* Return 0 done,1 waiting,-1 fault. A failed fence deliberately retains HOLD. */
int c64_uci_reu_fence(void);
int c64_uci_ram_read(uint16_t address,uint8_t *data,size_t bytes);
/* Drain an accepted native RAM read before releasing HOLD or reusing the
 * reply buffer. There is no unsafe asynchronous cancellation. */
int c64_uci_ram_cancel(void);
int c64_uci_ram_write(uint16_t address,const uint8_t *data,size_t bytes);
bool c64_uci_post(c64_uci_action_t,const char *path,unsigned device);
int c64_uci_action_result(void); /* 1 pending,0 successful,-1 failed */
bool c64_uci_cancel_action(void);
mt_uci_target_t c64_uci_dos_target(unsigned instance);
mt_uci_target_t c64_uci_control_target(void);
mt_uci_target_t c64_uci_softiec_target(void);
mt_uci_result_t c64_uci_named_reu_begin(bool save,mt_uci_reply_t *);
mt_uci_result_t c64_uci_named_reu_poll(mt_uci_reply_t *);
mt_uci_result_t c64_uci_named_reu_abort(void);
#endif
