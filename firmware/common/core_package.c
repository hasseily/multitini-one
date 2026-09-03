/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_package.h"

#include <stdbool.h>
#include <string.h>

#define MT_CORE_LINE_MAX 256U

enum {
    SEEN_SCHEMA = 1U << 0,
    SEEN_ID = 1U << 1,
    SEEN_NAME = 1U << 2,
    SEEN_CORE_ID = 1U << 3,
    SEEN_ABI = 1U << 4,
    SEEN_CONTENT_ROOT = 1U << 5
};

static char *trim_left(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r') ++text;
    return text;
}

static void trim_right(char *text)
{
    size_t size = strlen(text);
    while (size != 0U &&
           (text[size - 1U] == ' ' || text[size - 1U] == '\t' ||
            text[size - 1U] == '\r')) {
        text[--size] = '\0';
    }
}

static bool copy_checked(char *destination, size_t capacity,
                         const char *source)
{
    const size_t size = strlen(source);
    if (size == 0U || size >= capacity) return false;
    memcpy(destination, source, size + 1U);
    return true;
}

static bool is_slug(const char *text)
{
    size_t index;
    if (text[0] == '\0') return false;
    for (index = 0U; text[index] != '\0'; ++index) {
        const char ch = text[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              ch == '-' || ch == '_')) {
            return false;
        }
    }
    return true;
}

static bool is_filename(const char *text)
{
    size_t index;
    bool saw_dot = false;
    if (text[0] == '\0' || text[0] == '.') return false;
    for (index = 0U; text[index] != '\0'; ++index) {
        const char ch = text[index];
        if (ch == '.') saw_dot = true;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
              ch == '.')) {
            return false;
        }
    }
    return saw_dot && strstr(text, "..") == NULL;
}

static bool is_content_root(const char *text)
{
    size_t index;
    if (strncmp(text, "0:/multitini/cores/", 19U) != 0 ||
        strstr(text, "..") != NULL)
        return false;
    for (index = 0U; text[index] != '\0'; ++index) {
        const char ch = text[index];
        if (ch == '\\' || (ch == ':' && index != 1U) ||
            (unsigned char)ch < 0x20U) {
            return false;
        }
    }
    return text[index == 0U ? 0U : index - 1U] != '/';
}

static bool parse_u32_decimal(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    size_t index;
    if (text[0] == '\0') return false;
    for (index = 0U; text[index] != '\0'; ++index) {
        const uint32_t digit = (uint32_t)(unsigned char)text[index] -
                               (uint32_t)'0';
        if (digit > 9U || result > (UINT32_MAX - digit) / 10U) return false;
        result = (result * 10U) + digit;
    }
    *value = result;
    return true;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool parse_u32_hex(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    size_t index;
    if (text[0] != '0' || (text[1] != 'x' && text[1] != 'X') ||
        strlen(text + 2U) != 8U) {
        return false;
    }
    for (index = 2U; text[index] != '\0'; ++index) {
        const int digit = hex_value(text[index]);
        if (digit < 0) return false;
        result = (result << 4U) | (uint32_t)digit;
    }
    *value = result;
    return true;
}

static bool parse_abi(char *text, uint16_t *major, uint16_t *minor)
{
    uint32_t major_value;
    uint32_t minor_value;
    char *const dot = strchr(text, '.');
    if (dot == NULL || strchr(dot + 1, '.') != NULL) return false;
    *dot = '\0';
    if (!parse_u32_decimal(text, &major_value) ||
        !parse_u32_decimal(dot + 1, &minor_value) ||
        major_value > UINT16_MAX || minor_value > UINT16_MAX) {
        return false;
    }
    *major = (uint16_t)major_value;
    *minor = (uint16_t)minor_value;
    return true;
}

static bool variant_id_exists(const mt_core_package_t *package,
                              const char *id)
{
    size_t index;
    for (index = 0U; index < package->variant_count; ++index) {
        if (strcmp(package->variants[index].id, id) == 0) return true;
    }
    return false;
}

static mt_core_package_status_t parse_variant(
    char *value, mt_core_package_t *package)
{
    char *fields[5];
    char *cursor = value;
    size_t field;
    uint32_t size;
    uint32_t crc;
    mt_core_variant_t *variant;

    if (package->variant_count >= MT_CORE_PACKAGE_MAX_VARIANTS)
        return MT_CORE_PACKAGE_TOO_MANY_VARIANTS;
    for (field = 0U; field < 5U; ++field) {
        char *separator;
        fields[field] = trim_left(cursor);
        separator = strchr(cursor, '|');
        if (field == 4U) {
            if (separator != NULL) return MT_CORE_PACKAGE_SYNTAX;
        } else {
            if (separator == NULL) return MT_CORE_PACKAGE_SYNTAX;
            *separator = '\0';
            cursor = separator + 1;
        }
        trim_right(fields[field]);
    }
    if (!is_slug(fields[0]) || variant_id_exists(package, fields[0]) ||
        !is_filename(fields[2]) || !parse_u32_decimal(fields[3], &size) ||
        size < MT_CORE_BITSTREAM_MIN_BYTES ||
        size > MT_CORE_BITSTREAM_MAX_BYTES || (size & 3U) != 0U ||
        !parse_u32_hex(fields[4], &crc)) {
        return variant_id_exists(package, fields[0]) ?
               MT_CORE_PACKAGE_DUPLICATE_VARIANT :
               MT_CORE_PACKAGE_INVALID_VALUE;
    }
    variant = &package->variants[package->variant_count];
    if (!copy_checked(variant->id, sizeof(variant->id), fields[0]) ||
        !copy_checked(variant->label, sizeof(variant->label), fields[1]) ||
        !copy_checked(variant->filename, sizeof(variant->filename), fields[2])) {
        return MT_CORE_PACKAGE_INVALID_VALUE;
    }
    variant->bitstream_size = size;
    variant->bitstream_crc32 = crc;
    ++package->variant_count;
    return MT_CORE_PACKAGE_OK;
}

static mt_core_package_status_t parse_assignment(
    char *line, mt_core_package_t *package, uint32_t *seen)
{
    char *const equals = strchr(line, '=');
    char *key;
    char *value;
    uint32_t bit;

    if (equals == NULL) return MT_CORE_PACKAGE_SYNTAX;
    *equals = '\0';
    key = trim_left(line);
    value = trim_left(equals + 1);
    trim_right(key);
    trim_right(value);
    if (key[0] == '\0' || value[0] == '\0') return MT_CORE_PACKAGE_SYNTAX;

    if (strcmp(key, "variant") == 0) return parse_variant(value, package);
    if (strcmp(key, "schema") == 0) bit = SEEN_SCHEMA;
    else if (strcmp(key, "id") == 0) bit = SEEN_ID;
    else if (strcmp(key, "name") == 0) bit = SEEN_NAME;
    else if (strcmp(key, "core_id") == 0) bit = SEEN_CORE_ID;
    else if (strcmp(key, "abi") == 0) bit = SEEN_ABI;
    else if (strcmp(key, "content_root") == 0) bit = SEEN_CONTENT_ROOT;
    else return MT_CORE_PACKAGE_UNKNOWN_KEY;

    if ((*seen & bit) != 0U) return MT_CORE_PACKAGE_DUPLICATE_KEY;
    *seen |= bit;
    if (bit == SEEN_SCHEMA) {
        if (!parse_u32_decimal(value, &package->schema) ||
            package->schema != MT_CORE_PACKAGE_SCHEMA)
            return MT_CORE_PACKAGE_INVALID_VALUE;
    } else if (bit == SEEN_ID) {
        if (!is_slug(value) ||
            !copy_checked(package->id, sizeof(package->id), value))
            return MT_CORE_PACKAGE_INVALID_VALUE;
    } else if (bit == SEEN_NAME) {
        if (!copy_checked(package->name, sizeof(package->name), value))
            return MT_CORE_PACKAGE_INVALID_VALUE;
    } else if (bit == SEEN_CORE_ID) {
        if (!parse_u32_hex(value, &package->core_id) || package->core_id == 0U)
            return MT_CORE_PACKAGE_INVALID_VALUE;
    } else if (bit == SEEN_ABI) {
        if (!parse_abi(value, &package->abi_major, &package->abi_minor))
            return MT_CORE_PACKAGE_INVALID_VALUE;
    } else if (!is_content_root(value) ||
               !copy_checked(package->content_root,
                             sizeof(package->content_root), value)) {
        return MT_CORE_PACKAGE_INVALID_VALUE;
    }
    return MT_CORE_PACKAGE_OK;
}

mt_core_package_status_t mt_core_package_parse(
    const char *text, size_t size, mt_core_package_t *package)
{
    size_t offset = 0U;
    uint32_t seen = 0U;
    const uint32_t required = SEEN_SCHEMA | SEEN_ID | SEEN_NAME |
                              SEEN_CORE_ID | SEEN_ABI | SEEN_CONTENT_ROOT;

    if (text == NULL || package == NULL) return MT_CORE_PACKAGE_INVALID_ARGUMENT;
    if (size > MT_CORE_PACKAGE_TEXT_MAX) return MT_CORE_PACKAGE_TOO_LARGE;
    memset(package, 0, sizeof(*package));
    while (offset < size) {
        char line[MT_CORE_LINE_MAX];
        size_t length = 0U;
        char *content;
        mt_core_package_status_t status;
        while (offset < size && text[offset] != '\n') {
            if (text[offset] == '\0') return MT_CORE_PACKAGE_SYNTAX;
            if (length + 1U >= sizeof(line)) return MT_CORE_PACKAGE_LINE_TOO_LONG;
            line[length++] = text[offset++];
        }
        if (offset < size && text[offset] == '\n') ++offset;
        line[length] = '\0';
        content = trim_left(line);
        trim_right(content);
        if (content[0] == '\0' || content[0] == '#') continue;
        status = parse_assignment(content, package, &seen);
        if (status != MT_CORE_PACKAGE_OK) return status;
    }
    if ((seen & required) != required || package->variant_count == 0U)
        return MT_CORE_PACKAGE_MISSING_KEY;
    return MT_CORE_PACKAGE_OK;
}

mt_core_package_status_t mt_core_package_bitstream_path(
    const char *directory, const mt_core_variant_t *variant,
    char *path, size_t capacity)
{
    size_t directory_size;
    size_t filename_size;
    bool needs_separator;
    if (directory == NULL || variant == NULL || path == NULL || capacity == 0U ||
        directory[0] == '\0' || !is_filename(variant->filename)) {
        return MT_CORE_PACKAGE_INVALID_ARGUMENT;
    }
    directory_size = strlen(directory);
    filename_size = strlen(variant->filename);
    needs_separator = directory[directory_size - 1U] != '/';
    if (directory_size + (needs_separator ? 1U : 0U) + filename_size + 1U >
        capacity) {
        return MT_CORE_PACKAGE_PATH_TOO_LONG;
    }
    memcpy(path, directory, directory_size);
    if (needs_separator) path[directory_size++] = '/';
    memcpy(path + directory_size, variant->filename, filename_size + 1U);
    return MT_CORE_PACKAGE_OK;
}

uint32_t mt_core_package_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;
    if (data == NULL && size != 0U) return 0U;
    for (index = 0U; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

const char *mt_core_package_status_name(mt_core_package_status_t status)
{
    switch (status) {
    case MT_CORE_PACKAGE_OK: return "ok";
    case MT_CORE_PACKAGE_INVALID_ARGUMENT: return "invalid argument";
    case MT_CORE_PACKAGE_TOO_LARGE: return "manifest too large";
    case MT_CORE_PACKAGE_LINE_TOO_LONG: return "manifest line too long";
    case MT_CORE_PACKAGE_SYNTAX: return "manifest syntax";
    case MT_CORE_PACKAGE_UNKNOWN_KEY: return "unknown manifest key";
    case MT_CORE_PACKAGE_DUPLICATE_KEY: return "duplicate manifest key";
    case MT_CORE_PACKAGE_MISSING_KEY: return "missing manifest key";
    case MT_CORE_PACKAGE_INVALID_VALUE: return "invalid manifest value";
    case MT_CORE_PACKAGE_TOO_MANY_VARIANTS: return "too many variants";
    case MT_CORE_PACKAGE_DUPLICATE_VARIANT: return "duplicate variant";
    case MT_CORE_PACKAGE_PATH_TOO_LONG: return "path too long";
    default: return "unknown";
    }
}
