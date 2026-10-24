/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_interface.h"
#include "net_model.h"
#include "net_service.h"
#include "sd_storage.h"
#include "xiltimer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static model_t model;
static uint32_t abi = 26, caps = MT_NET_CAPS_W5100S, status;
static unsigned extended_reads, writes, saves, printed;
static bool external, save_failure, found;
static char persisted[1024], last_line[400];
static size_t persisted_size;
void XTime_GetTime(XTime *value) { *value = model.time * (COUNTS_PER_SECOND / 1000000); }
static uint32_t read32(void *context, uint32_t address) {
    (void)context;
    if (address == MT_REG_ABI_VERSION)
        return abi;
    if (address == MT_REG_CORE_ID)
        return MT_C64_CORE_ID;
    if (address == MT_REG_NET_CAPS) {
        extended_reads++;
        return caps;
    }
    if (address == MT_REG_NET_STATUS) {
        extended_reads++;
        return status;
    }
    assert(0);
    return 0;
}
static void write32(void *context, uint32_t address, uint32_t value) {
    (void)context;
    writes++;
    if (address == MT_REG_NET_CONTROL) {
        run(&model, (value & 1) != 0);
        status = value & 1 ? MT_NET_STATUS_VALID | MT_NET_STATUS_READY | MT_NET_STATUS_RESET_DONE
                           : MT_NET_STATUS_VALID;
        return;
    }
    if (address == MT_REG_NET_ACCESS) {
        uint8_t result = (uint8_t)(value >> 16);
        assert(!access_reg(&model, (uint16_t)(value & 0x7fff), (value & MT_NET_ACCESS_WRITE) != 0,
                           result, &result));
        status = (status & 0xff) | ((((status >> 16) + 1) & 255) << 16) | ((uint32_t)result << 8) |
                 MT_NET_STATUS_DONE;
        return;
    }
    assert(0);
}
static mt_sd_result_t sd_result(mt_sd_status_t s, size_t size) {
    mt_sd_result_t r = {s, 0, size};
    return r;
}
bool mt_sd_storage_external_active(void) { return external; }
mt_sd_result_t mt_sd_storage_ensure_directory(const char *path) {
    assert(!strcmp(path, "0:/multitini"));
    return sd_result(external ? MT_SD_EXTERNAL_ACCESS : MT_SD_OK, 0);
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *path, const char *journal, uint8_t *data,
                                             size_t size) {
    (void)data;
    (void)size;
    assert(!strcmp(path, "0:/multitini/network.ini") &&
           !strcmp(journal, "0:/multitini/network.ini.mtj"));
    return sd_result(MT_SD_OK, 0);
}
mt_sd_result_t mt_sd_storage_read_file_optional_max(const char *path, uint8_t *data,
                                                    size_t capacity, bool *exists) {
    (void)path;
    assert(capacity >= persisted_size);
    memcpy(data, persisted, persisted_size);
    *exists = found;
    return sd_result(MT_SD_OK, persisted_size);
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *path, const char *journal,
                                             const uint8_t *data, size_t size) {
    (void)path;
    (void)journal;
    saves++;
    if (save_failure || external)
        return sd_result(MT_SD_WRITE_FAILED, 0);
    assert(size < sizeof(persisted));
    memcpy(persisted, data, size);
    persisted[size] = 0;
    persisted_size = size;
    found = true;
    return sd_result(MT_SD_OK, size);
}
static void output(void *context, const char *line) {
    (void)context;
    printed++;
    snprintf(last_line, sizeof(last_line), "%s", line);
}
int main(void) {
    mt_core_link_t link = {NULL, read32, write32, 0};
    uint8_t seed[16] = {1, 2, 3, 4};
    char saved_mac[80];
    unsigned before;
    char info[160];
    /* Old images must never see reads of the new register range. */
    abi = 25;
    mt_net_service_seed(seed, sizeof(seed));
    mt_net_service_init(&link);
    mt_net_service_poll();
    assert(!extended_reads && !writes);
    mt_net_service_shutdown();
    abi = 26;
    mt_net_service_init(&link);
    assert(mt_net_service_mode() == MT_NET_OFF);
    before = saves;
    assert(mt_net_service_set_mode(MT_NET_DHCP));
    assert(saves == before + 1);
    assert(strstr(persisted, "mac_auto=") && strstr(persisted, "mac_card="));
    snprintf(saved_mac, sizeof(saved_mac), "%.26s", strstr(persisted, "mac_auto="));
    model.time += 1000;
    mt_net_service_poll();
    mt_net_service_status(info, sizeof(info));
    assert(strstr(info, "DHCP"));
    external = true;
    before = saves;
    assert(!mt_net_service_set_mode(MT_NET_OFF) && saves == before &&
           mt_net_service_mode() == MT_NET_DHCP);
    external = false;
    save_failure = true;
    assert(!mt_net_service_set_mode(MT_NET_OFF) && mt_net_service_mode() == MT_NET_DHCP);
    save_failure = false;
    mt_net_service_shutdown();
    model.time += 900000;
    before = saves;
    mt_net_service_init(&link);
    assert(saves == before && strstr(persisted, saved_mac));
    seed[0]++;
    mt_net_service_seed(seed, sizeof(seed));
    mt_net_service_shutdown();
    mt_net_service_init(&link);
    assert(saves == before + 1 && !strstr(persisted, saved_mac));
    assert(mt_net_service_command(":net set mac 02:12:34:56:78:9a", output, NULL));
    assert(strstr(persisted, "mac=02:12:34:56:78:9a"));
    before = saves;
    seed[0]++;
    mt_net_service_seed(seed, sizeof(seed));
    mt_net_service_shutdown();
    mt_net_service_init(&link);
    assert(saves == before && strstr(persisted, "mac=02:12:34:56:78:9a"));
    assert(mt_net_service_command(":net", output, NULL) && printed > 3);
    assert(!mt_net_service_command(":network", output, NULL));
    assert(mt_net_service_command(":net set ip 999.1.2.3", output, NULL) &&
           !strcmp(last_line, "Invalid network key or value"));
    mt_net_service_shutdown();
    puts("PASS platform network ABI gates, atomic settings, USB-export refusal, stable CID/MAC "
         "persistence/copied-card regeneration, explicitMAC, UART");
    return 0;
}
