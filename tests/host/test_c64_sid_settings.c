/* SPDX-License-Identifier: GPL-3.0-only */
#include "supervisor_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t image[32 + MT_SETTINGS_MAX_PROFILES * MT_C64_SID_RECORD_BYTES];
static size_t image_size;
static bool found, busy, fail_recovery, fail_write;
static unsigned writes, recoveries;
bool mt_sd_file_path_busy(const char *path, bool exclusive)
{
    assert(strstr(path, "c64_sid.bin") && exclusive);
    return busy;
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *p, const char *j, uint8_t *out, size_t cap)
{
    (void)p;
    (void)j;
    (void)out;
    (void)cap;
    ++recoveries;
    return (mt_sd_result_t){fail_recovery ? MT_SD_SYNC_FAILED : MT_SD_OK, 0, 0};
}
mt_sd_result_t mt_sd_storage_read_file_optional_max(const char *p, uint8_t *out, size_t cap,
                                                    bool *exists)
{
    (void)p;
    *exists = found;
    assert(image_size <= cap);
    memcpy(out, image, image_size);
    return (mt_sd_result_t){MT_SD_OK, 0, image_size};
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *p, const char *j, const uint8_t *in,
                                             size_t size)
{
    (void)p;
    (void)j;
    ++writes;
    if (fail_write)
        return (mt_sd_result_t){MT_SD_SYNC_FAILED, 0, 0};
    assert(size <= sizeof image);
    memcpy(image, in, size);
    image_size = size;
    found = true;
    return (mt_sd_result_t){MT_SD_OK, 0, size};
}
int main(void)
{
    mt_c64_sid_model_t m, loaded;
    assert(mt_supervisor_sid_map_load("c64", 3, &loaded));
    assert(mt_c64_sid_preset_id(&loaded) == MT_SID_DUAL500 && writes == 0);
    assert(mt_c64_sid_preset(&m, MT_SID_EIGHT));
    m.sid_volume[3] = 7;
    m.sid_pan[5] = 2;
    assert(mt_supervisor_sid_map_save("c64", &m));
    uint8_t legacy = image[32 + 24];
    assert(image_size == 160 && mt_supervisor_sid_map_load("c64", legacy, &loaded));
    assert(memcmp(&m, &loaded, sizeof m) == 0);
    assert(mt_supervisor_sid_map_load("c64", legacy == 2 ? 3 : 2, &loaded));
    assert(mt_c64_sid_preset_id(&loaded) == (legacy == 2 ? MT_SID_DUAL500 : MT_SID_DUAL420));
    assert(writes == 1); /* Legacy fallback does not silently repair persistent data. */
    unsigned before = recoveries;
    busy = true;
    assert(!mt_supervisor_sid_map_save("c64", &m));
    assert(!mt_supervisor_sid_map_load("c64", legacy, &loaded));
    assert(recoveries == before && writes == 1);
    busy = false;
    fail_recovery = true;
    assert(!mt_supervisor_sid_map_load("c64", legacy, &loaded));
    fail_recovery = false;
    image[50] ^= 1;
    assert(!mt_supervisor_sid_map_load("c64", legacy, &loaded));
    assert(!mt_supervisor_sid_map_save("c64", &m) && writes == 1);
    image[50] ^= 1;
    uint8_t previous[sizeof image];
    memcpy(previous, image, sizeof image);
    fail_write = true;
    assert(!mt_supervisor_sid_map_save("second", &m));
    assert(memcmp(previous, image, sizeof image) == 0);
    fail_write = false;
    for (unsigned i = 1; i < MT_SETTINGS_MAX_PROFILES; ++i) {
        char id[24];
        (void)snprintf(id, sizeof id, "c64-%u", i);
        assert(mt_supervisor_sid_map_save(id, &m));
    }
    before = writes;
    assert(!mt_supervisor_sid_map_save("overflow", &m) && writes == before);
    assert(mt_supervisor_sid_map_save("c64", &m));
    assert(image_size == sizeof image);
    for (unsigned byte = 0; byte < 32; ++byte) {
        image[byte] ^= 0x80;
        assert(!mt_supervisor_sid_map_load("c64", legacy, &loaded));
        image[byte] ^= 0x80;
    }
    puts("SID companion: legacy fallback, exact roundtrip, all header bytes, CRC, "
         "busy/recovery/write "
         "failure and capacity PASS");
    return 0;
}
