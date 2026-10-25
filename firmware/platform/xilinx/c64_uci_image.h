/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_UCI_IMAGE_H
#define MULTITINI_C64_UCI_IMAGE_H
#include "sd_storage.h"

typedef struct c64_uci_image_file c64_uci_image_file_t;
/* These paths have already passed the caller's rooted path policy. The image
 * itself is checked against the UCI mounted-media owner before it is opened. */
bool c64_uci_image_path(const char *path, char image[512], const char **member);
mt_sd_result_t c64_uci_image_open(const char *path, uint8_t flags,
                                  c64_uci_image_file_t **file);
mt_sd_result_t c64_uci_image_read(c64_uci_image_file_t *, void *, size_t);
mt_sd_result_t c64_uci_image_write(c64_uci_image_file_t *, const void *, size_t);
mt_sd_result_t c64_uci_image_seek(c64_uci_image_file_t *, uint32_t);
mt_sd_result_t c64_uci_image_close(c64_uci_image_file_t **);
/* Discard changes before a journal commit starts. An interrupted commit must
 * be completed with close/retry and cannot be rolled back by discarding RAM. */
mt_sd_result_t c64_uci_image_abort(c64_uci_image_file_t **);
mt_sd_result_t c64_uci_image_stat(const char *, mt_sd_info_t *);
mt_sd_result_t c64_uci_image_info(c64_uci_image_file_t *, mt_sd_info_t *);
mt_sd_result_t c64_uci_image_directory(const char *, c64_uci_image_file_t **);
mt_sd_result_t c64_uci_image_next(c64_uci_image_file_t *, mt_sd_info_t *);
mt_sd_result_t c64_uci_image_header(c64_uci_image_file_t *, char name[17], char id[6]);
mt_sd_result_t c64_uci_image_remove(const char *);
mt_sd_result_t c64_uci_image_mkdir(const char *);
mt_sd_result_t c64_uci_image_rename(const char *, const char *);
mt_sd_result_t c64_uci_image_copy(const char *, const char *);
/* Operate on an existing image, retaining its geometry. A missing ID keeps the
 * previous ID. Label updates accept NULL to preserve the existing label.
 * Creating a DNP follows Ultimate N:NAME.DNP,ddd: a three-digit track count
 * 001..255 creates an unformatted image, formatted by a subsequent N: inside
 * the image. Higher tracks cannot fit the on-disk BAM and chain pointers. */
mt_sd_result_t c64_uci_image_format(const char *, const char *label, const char *id);
mt_sd_result_t c64_uci_image_label(const char *, const char *label, const char *id);
mt_sd_result_t c64_uci_image_attributes(const char *, uint8_t attributes, uint8_t mask);
/* CBM DOS codes; operations match U1/U2/B-A/B-F. Allocating an already used
 * block returns 65 and the next free track/sector, without reserving it. */
/* Retry cleanup of failed single-operation image transactions. */
int c64_uci_image_retry(void);
int c64_uci_image_sector(const char *, unsigned operation, unsigned *track,
                         unsigned *sector, uint8_t data[256]);
#endif
