/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_IIGS_CONTENT_H
#define MULTITINI_IIGS_CONTENT_H
#include "multitini_iigs_interface.h"
#include "multitini_content.h"
typedef struct { uint64_t offset, size; uint8_t kind; bool writable; } mt_iigs_media_info_t;
#define MT_IIGS_MEDIA_NAME_MAX 95U
/* Relative to the media root; FAT-safe components, no traversal or volume. */
bool mt_iigs_media_path_valid(const char *path);
bool mt_iigs_config_valid(const mt_iigs_config_t *config);
uint8_t mt_iigs_next_speed(uint8_t current, uint32_t supported_mask);
void mt_iigs_config_encode(const mt_iigs_config_t *config, uint8_t wire[MT_IIGS_CONFIG_BYTES]);
/* Inspect a prefix without loading a hard disk into ARM RAM. WOZ is mounted
 * with its header intact; HDD 2MG headers are removed from the logical view. */
mt_content_validation_t mt_iigs_media_inspect(unsigned drive, const char *name,
    const uint8_t *prefix, size_t prefix_size, uint64_t file_size,
    mt_iigs_media_info_t *info);
#endif
