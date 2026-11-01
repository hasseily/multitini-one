/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_DISPLAY_H
#define MULTITINI_AMIGA_DISPLAY_H
#include "multitini_amiga_boot.h"
#include <stdbool.h>
#include <stdint.h>

#define MT_AMIGA_DISPLAY_MODES 4U
#define MT_AMIGA_DISPLAY_SIDECAR_BYTES 16U
typedef bool (*mt_amiga_display_apply_fn)(void *context,uint8_t mode);
typedef bool (*mt_amiga_display_save_fn)(void *context,uint8_t mode);
typedef struct {
    uint16_t width,height,depth;
    uint32_t mode_id;
    int16_t min_x,min_y,max_x,max_y,view_x,view_y;
    uint16_t flags;
} mt_amiga_display_info_t;
typedef enum {
    MT_AMIGA_DISPLAY_UNAVAILABLE=0,MT_AMIGA_DISPLAY_NO_HELPER,
    MT_AMIGA_DISPLAY_READY,MT_AMIGA_DISPLAY_PENDING,MT_AMIGA_DISPLAY_BUSY,
    MT_AMIGA_DISPLAY_APPLIED,MT_AMIGA_DISPLAY_FAILED,MT_AMIGA_DISPLAY_SAVE_FAILED
} mt_amiga_display_status_t;
typedef struct {
    mt_core_link_t *link;
    mt_amiga_display_apply_fn apply;
    mt_amiga_display_save_fn save;
    void *context;
    mt_amiga_display_info_t info;
    mt_amiga_display_status_t status;
    uint32_t next_poll_ms,heartbeat_ms,request_ms,pause_ms;
    uint16_t heartbeat,request_word,sequence;
    uint8_t requested,applied,saved;
    uint16_t guest_result,helper_flags;
    bool capable,poll_started,heartbeat_seen,helper_healthy,pending,saved_valid,paused;
} mt_amiga_display_t;

void mt_amiga_display_init(mt_amiga_display_t *,mt_core_link_t *,
                           mt_amiga_display_apply_fn,mt_amiga_display_save_fn,void *);
void mt_amiga_display_restore(mt_amiga_display_t *,uint8_t mode);
void mt_amiga_display_poll(mt_amiga_display_t *,uint32_t now_ms);
void mt_amiga_display_set_paused(mt_amiga_display_t *,bool paused,uint32_t now_ms);
bool mt_amiga_display_request(mt_amiga_display_t *,uint8_t mode,uint32_t now_ms);
bool mt_amiga_display_available(const mt_amiga_display_t *);
uint8_t mt_amiga_display_mode_count(const mt_amiga_display_t *);
bool mt_amiga_display_mode_available(const mt_amiga_display_t *,uint8_t mode);
const char *mt_amiga_display_status_name(const mt_amiga_display_t *);
const char *mt_amiga_display_mode_name(uint8_t mode);
void mt_amiga_display_sidecar_encode(uint8_t mode,uint8_t out[MT_AMIGA_DISPLAY_SIDECAR_BYTES]);
bool mt_amiga_display_sidecar_decode(const uint8_t in[MT_AMIGA_DISPLAY_SIDECAR_BYTES],uint8_t *mode);
#endif
