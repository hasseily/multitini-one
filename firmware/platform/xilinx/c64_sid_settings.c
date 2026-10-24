/* SPDX-License-Identifier: GPL-3.0-only */
#include "supervisor_settings.h"
#include <string.h>
#define SID_PATH "0:/multitini/c64_sid.bin"
#define SID_JOURNAL "0:/multitini/c64_sid.bin.mtj"
static uint8_t wire[32U + MT_SETTINGS_MAX_PROFILES * MT_C64_SID_RECORD_BYTES];
static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (unsigned b = 0; b < 8; ++b)
            c = (c >> 1) ^ (UINT32_C(0xedb88320) & (0U - (c & 1)));
    }
    return ~c;
}
static bool read_maps(unsigned *count)
{
    *count = 0;
    if (mt_sd_file_path_busy(SID_PATH, true) || mt_sd_file_path_busy(SID_JOURNAL, true))
        return false;
    if (mt_sd_storage_journal_recover(SID_PATH, SID_JOURNAL, wire, sizeof(wire)).status != MT_SD_OK)
        return false;
    bool found = false;
    mt_sd_result_t r = mt_sd_storage_read_file_optional_max(SID_PATH, wire, sizeof(wire), &found);
    if (r.status != MT_SD_OK)
        return false;
    if (!found) {
        memset(wire, 0, sizeof(wire));
        memcpy(wire, "MTSIDMP1", 8);
        put32(wire + 8, 1);
        put32(wire + 12, 128);
        return true;
    }
    if (r.size < 32 || memcmp(wire, "MTSIDMP1", 8) || get32(wire + 8) != 1 ||
        get32(wire + 12) != 128 || get32(wire + 28))
        return false;
    unsigned n = get32(wire + 16);
    uint32_t crc = get32(wire + 20);
    if (n > MT_SETTINGS_MAX_PROFILES || r.size != 32U + 128U * n || get32(wire + 24) != ~crc ||
        crc32(wire + 32, 128U * n) != crc)
        return false;
    for (unsigned i = 0; i < n; ++i) {
        char id[24];
        mt_c64_sid_model_t model;
        if (!mt_c64_sid_decode(&model, id, wire + 32 + 128 * i))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(id, (const char *)wire + 32 + 128 * j))
                return false;
    }
    *count = n;
    return true;
}
bool mt_supervisor_sid_map_load(const char *id, uint8_t legacy, mt_c64_sid_model_t *model)
{
    if (!id || !model)
        return false;
    mt_c64_sid_preset(model, legacy == 2   ? MT_SID_DUAL420
                             : legacy == 3 ? MT_SID_DUAL500
                                           : MT_SID_SINGLE);
    unsigned count;
    if (!read_maps(&count))
        return false;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *r = wire + 32 + 128 * i;
        if (!strcmp(id, (const char *)r) && r[24] == legacy) {
            char decoded[24];
            return mt_c64_sid_decode(model, decoded, r);
        }
    }
    return true;
}
bool mt_supervisor_sid_map_save(const char *id, const mt_c64_sid_model_t *model)
{
    uint8_t record[128];
    if (!mt_c64_sid_encode(model, id, record))
        return false;
    unsigned count;
    if (!read_maps(&count))
        return false;
    unsigned index = 0;
    while (index < count && strcmp(id, (const char *)wire + 32 + 128 * index))
        ++index;
    if (index == count) {
        if (count == MT_SETTINGS_MAX_PROFILES)
            return false;
        ++count;
    }
    memcpy(wire + 32 + 128 * index, record, 128);
    put32(wire + 16, count);
    uint32_t crc = crc32(wire + 32, 128U * count);
    put32(wire + 20, crc);
    put32(wire + 24, ~crc);
    return mt_sd_storage_journal_replace(SID_PATH, SID_JOURNAL, wire, 32U + 128U * count).status ==
           MT_SD_OK;
}
