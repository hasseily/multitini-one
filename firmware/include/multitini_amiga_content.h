/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_CONTENT_H
#define MULTITINI_AMIGA_CONTENT_H
#include "multitini_amiga_interface.h"
#include "multitini_content.h"
#define MT_AMIGA_MFM_SECTOR_WORDS 544U
#define MT_AMIGA_MFM_GAP_WORDS 350U
#define MT_AMIGA_MAX_HDF_BYTES (UINT64_C(512)*UINT32_C(0x0fffffff))
bool mt_amiga_config_valid(const mt_amiga_config_t *);
bool mt_amiga_fast_ide_enabled(const mt_amiga_config_t *,uint32_t host_status);
void mt_amiga_config_defaults(mt_amiga_config_t *);
void mt_amiga_config_encode(const mt_amiga_config_t *,uint8_t wire[MT_AMIGA_CONFIG_BYTES]);
bool mt_amiga_config_decode(const uint8_t wire[MT_AMIGA_CONFIG_BYTES],mt_amiga_config_t *);
bool mt_amiga_media_path_valid(const char *);
bool mt_amiga_rom_size_valid(size_t);
mt_content_validation_t mt_amiga_media_validate(unsigned drive,uint64_t bytes);
/* ADF sectors contain 512 bytes. The MFM sector includes preamble, two
 * syncs, header/label/checksums and odd/even data. Clock state crosses sectors. */
bool mt_amiga_mfm_encode(unsigned track,unsigned sector,uint16_t sync,
    const uint8_t data[512],uint16_t words[MT_AMIGA_MFM_SECTOR_WORDS],uint16_t *previous);
bool mt_amiga_mfm_decode(const uint16_t words[MT_AMIGA_MFM_SECTOR_WORDS],
    unsigned expected_track,unsigned *sector,uint8_t data[512]);
uint16_t mt_amiga_mfm_clock(uint16_t data,uint16_t *previous);
#endif
