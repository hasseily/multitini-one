/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_IIGS_CONTENT_H
#define MULTITINI_IIGS_CONTENT_H
#include "multitini_iigs_interface.h"
#include "multitini_content.h"
enum { MT_IIGS_RAW_NONE, MT_IIGS_RAW_DOS525, MT_IIGS_RAW_PRODOS35 };
typedef struct {
    uint64_t offset, size;
    uint8_t kind;
    bool writable;
    uint8_t raw_format;
} mt_iigs_media_info_t;
#define MT_IIGS_FLOPPY_HEADER_BYTES 1536U
#define MT_IIGS_FLOPPY_TRACK_BYTES 10240U
#define MT_IIGS_FLOPPY_RAW_TRACK_BYTES 6144U
typedef struct {
    uint32_t raw_offset, bit_count;
    uint16_t first_block, block_count, raw_bytes;
} mt_iigs_floppy_track_t;
/* Raw images are exposed to the existing IWM as a generated WOZ2 bitstream.
 * The 3.5-inch track number includes side (cylinder*2+side). */
bool mt_iigs_floppy_track(uint8_t format, unsigned track, mt_iigs_floppy_track_t *info);
uint32_t mt_iigs_floppy_woz_size(uint8_t format);
bool mt_iigs_floppy_header(uint8_t format, bool writable,
    uint8_t header[MT_IIGS_FLOPPY_HEADER_BYTES]);
bool mt_iigs_floppy_encode(uint8_t format, unsigned track, const uint8_t *raw,
    uint8_t bits[MT_IIGS_FLOPPY_TRACK_BYTES]);
/* Reject incomplete or corrupt tracks. Never synthesize missing sector data. */
bool mt_iigs_floppy_decode(uint8_t format, unsigned track, const uint8_t *bits,
    uint8_t raw[MT_IIGS_FLOPPY_RAW_TRACK_BYTES]);
#define MT_IIGS_MEDIA_NAME_MAX 95U
/* Relative to the media root; FAT-safe components, no traversal or volume. */
bool mt_iigs_media_path_valid(const char *path);
bool mt_iigs_config_valid(const mt_iigs_config_t *config);
uint8_t mt_iigs_next_speed(uint8_t current, uint32_t supported_mask);
void mt_iigs_config_encode(const mt_iigs_config_t *config, uint8_t wire[MT_IIGS_CONFIG_BYTES]);
/* Inspect a prefix without loading a disk into ARM RAM. offset/size describe
 * the original payload, including when raw_format requests a virtual WOZ. */
mt_content_validation_t mt_iigs_media_inspect(unsigned drive, const char *name,
    const uint8_t *prefix, size_t prefix_size, uint64_t file_size,
    mt_iigs_media_info_t *info);
#endif
