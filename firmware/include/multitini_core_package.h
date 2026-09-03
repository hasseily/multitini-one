/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_CORE_PACKAGE_H
#define MULTITINI_CORE_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#define MT_CORE_PACKAGE_SCHEMA 1U
#define MT_CORE_PACKAGE_TEXT_MAX 4096U
#define MT_CORE_PACKAGE_MAX_VARIANTS 8U
#define MT_CORE_PACKAGE_ID_MAX 24U
#define MT_CORE_PACKAGE_NAME_MAX 48U
#define MT_CORE_VARIANT_ID_MAX 24U
#define MT_CORE_VARIANT_LABEL_MAX 40U
#define MT_CORE_FILENAME_MAX 64U
#define MT_CORE_CONTENT_ROOT_MAX 96U
#define MT_CORE_DIRECTORY_MAX 128U
#define MT_CORE_BITSTREAM_MIN_BYTES (1024U * 1024U)
#define MT_CORE_BITSTREAM_MAX_BYTES (8U * 1024U * 1024U)

typedef enum {
    MT_CORE_PACKAGE_OK = 0,
    MT_CORE_PACKAGE_INVALID_ARGUMENT,
    MT_CORE_PACKAGE_TOO_LARGE,
    MT_CORE_PACKAGE_LINE_TOO_LONG,
    MT_CORE_PACKAGE_SYNTAX,
    MT_CORE_PACKAGE_UNKNOWN_KEY,
    MT_CORE_PACKAGE_DUPLICATE_KEY,
    MT_CORE_PACKAGE_MISSING_KEY,
    MT_CORE_PACKAGE_INVALID_VALUE,
    MT_CORE_PACKAGE_TOO_MANY_VARIANTS,
    MT_CORE_PACKAGE_DUPLICATE_VARIANT,
    MT_CORE_PACKAGE_PATH_TOO_LONG
} mt_core_package_status_t;

typedef struct {
    char id[MT_CORE_VARIANT_ID_MAX];
    char label[MT_CORE_VARIANT_LABEL_MAX];
    char filename[MT_CORE_FILENAME_MAX];
    uint32_t bitstream_size;
    uint32_t bitstream_crc32;
} mt_core_variant_t;

typedef struct {
    uint32_t schema;
    uint32_t core_id;
    uint16_t abi_major;
    uint16_t abi_minor;
    char id[MT_CORE_PACKAGE_ID_MAX];
    char name[MT_CORE_PACKAGE_NAME_MAX];
    char content_root[MT_CORE_CONTENT_ROOT_MAX];
    size_t variant_count;
    mt_core_variant_t variants[MT_CORE_PACKAGE_MAX_VARIANTS];
} mt_core_package_t;

mt_core_package_status_t mt_core_package_parse(
    const char *text, size_t size, mt_core_package_t *package);
mt_core_package_status_t mt_core_package_bitstream_path(
    const char *directory, const mt_core_variant_t *variant,
    char *path, size_t capacity);
uint32_t mt_core_package_crc32(const uint8_t *data, size_t size);
const char *mt_core_package_status_name(mt_core_package_status_t status);

#endif
