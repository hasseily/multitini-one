/* SPDX-License-Identifier: GPL-3.0-only */
#include "atari_xt_content_service.h"
#include "multitini_atari_xt_interface.h"
#include "multitini_content.h"
#include "multitini_media_browser.h"
#include "sd_storage.h"
#include "sleep.h"

#include <string.h>

#define POLLS 100000U

static uint8_t os[MT_ATARI_XT_OS_BYTES];
static uint8_t basic[MT_ATARI_XT_BASIC_BYTES];
static uint8_t cart[MT_ATARI_XT_CART_MAX_FILE_BYTES];
static char cart_name[MT_MEDIA_BROWSER_PATH_MAX + 1U];
static bool roms_loaded;
static bool cart_uncertain;

void atari_xt_content_reset_selection(void)
{
    cart_name[0] = '\0';
    roms_loaded = false;
    cart_uncertain = false;
}

bool atari_xt_content_roms_loaded(void) { return roms_loaded; }
bool atari_xt_content_cart_uncertain(void) { return cart_uncertain; }
const char *atari_xt_content_cart_name(void) { return cart_name[0] != '\0' ? cart_name : NULL; }

static bool active(const mt_core_link_t *link)
{
    return link != NULL && link->read32 != NULL && link->write32 != NULL &&
           link->read32(link->context, MT_REG_CORE_ID) == MT_ATARI_XT_CORE_ID;
}

static const char *read_rom(const char *core_directory, const char *relative,
                            const char *missing, uint8_t *data, size_t size)
{
    char path[MT_MEDIA_BROWSER_PATH_MAX + 32U];
    if (!mt_media_browser_join(core_directory, relative, path, sizeof(path)))
        return "Invalid Atari ROM path";
    const mt_sd_result_t result = mt_sd_storage_read_file_exact(path, data, size);
    if (result.status == MT_SD_OPEN_FAILED) return missing;
    if (result.status == MT_SD_SIZE_MISMATCH) return "Atari ROM has the wrong size";
    if (result.status != MT_SD_OK) return mt_sd_status_name(result.status);
    for (size_t i = 1U; i < size; ++i)
        if (data[i] != data[0]) return NULL;
    return "Atari ROM contains a single repeated byte";
}

static const char *request_reset(mt_core_link_t *link, bool asserted, mt_link_progress_fn progress)
{
    const uint32_t args[4] = {asserted ? 1U : 0U, 0U, 0U, 0U};
    mt_command_response_t response;
    const mt_link_result_t result = mt_core_link_command(link, MT_COMMAND_CORE_RESET,
        args, POLLS, progress, NULL, &response);
    return result == MT_LINK_OK ? NULL : mt_link_result_name(result);
}

static const char *transfer(mt_core_link_t *link, uint8_t kind, const uint8_t *data,
                            size_t size, mt_link_progress_fn progress)
{
    link->write32(link->context, MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    const mt_link_result_t result = mt_core_link_stream(link, kind, data, size, POLLS,
        progress, NULL);
    if (result != MT_LINK_OK) return mt_link_result_name(result);
    /* The receiver's sticky result crosses to the shell after the final byte. */
    usleep(1U);
    return (link->read32(link->context, MT_REG_STATUS) & MT_STATUS_CORE_LOAD_ERROR) != 0U ?
        "Atari load rejected" : NULL;
}

static const char *run_guest(mt_core_link_t *link, mt_link_progress_fn progress)
{
    const char *const error = request_reset(link, false, progress);
    if (error == NULL) {
        for (unsigned poll = 0U; poll < POLLS; ++poll) {
            const uint32_t status = link->read32(link->context, MT_REG_STATUS);
            if ((status & MT_STATUS_CORE_LOAD_ERROR) != 0U) break;
            if ((status & MT_STATUS_CORE_READY) != 0U &&
                (link->read32(link->context, MT_REG_CONTROL) & MT_CONTROL_CORE_RESET_REQUEST) == 0U)
                return NULL;
            if (progress != NULL) progress(NULL);
        }
    }
    (void)request_reset(link, true, progress);
    return error != NULL ? error : "Atari did not leave reset";
}

const char *atari_xt_content_boot(mt_core_link_t *link, const char *core_directory,
                                  mt_link_progress_fn progress)
{
    if (!active(link)) return "Atari 800XL core is unavailable";
    const char *error = read_rom(core_directory, "roms/atarixl.rom",
        "Supply roms/atarixl.rom (16 KiB)", os, sizeof(os));
    if (error == NULL) error = read_rom(core_directory, "roms/ataribas.rom",
        "Supply roms/ataribas.rom (8 KiB)", basic, sizeof(basic));
    if (error != NULL) return error;

    error = request_reset(link, true, progress);
    if (error != NULL) return error;
    roms_loaded = false;
    error = transfer(link, MT_LOAD_ATARI_XT_OS, os, sizeof(os), progress);
    if (error == NULL) error = transfer(link, MT_LOAD_ATARI_XT_BASIC, basic, sizeof(basic), progress);
    if (error != NULL) return error;
    roms_loaded = true;
    return cart_uncertain ? "Eject or replace the cartridge to restart" : run_guest(link, progress);
}

static const char *change_cart(mt_core_link_t *link, uint8_t kind, const uint8_t *data,
                               size_t size, const char *relative, mt_link_progress_fn progress)
{
    const char *error = request_reset(link, true, progress);
    if (error != NULL) return error;
    cart_name[0] = '\0';
    cart_uncertain = true;
    error = transfer(link, kind, data, size, progress);
    if (error != NULL) return error;
    cart_uncertain = false;
    if (relative != NULL) memcpy(cart_name, relative, strlen(relative) + 1U);
    return run_guest(link, progress);
}

const char *atari_xt_content_restart(mt_core_link_t *link, mt_link_progress_fn progress)
{
    if (!active(link) || !roms_loaded) return "Load the Atari system ROMs first";
    if (cart_uncertain) return "Eject or replace the cartridge to restart";
    const char *const error = request_reset(link, true, progress);
    if (error != NULL) return error;
    usleep(1U);
    return run_guest(link, progress);
}

const char *atari_xt_content_insert(mt_core_link_t *link, const char *media_root,
                                    const char *relative, mt_link_progress_fn progress)
{
    if (!active(link) || !roms_loaded) return "Load the Atari system ROMs first";
    char path[2U * MT_MEDIA_BROWSER_PATH_MAX + 2U];
    const mt_content_type_t type = mt_content_classify_for_core(MT_ATARI_XT_CORE_ID, relative);
    if ((type != MT_CONTENT_ATARI_XT_CART && type != MT_CONTENT_ATARI_XT_RAW_CART) ||
        !mt_media_browser_join(media_root, relative, path, sizeof(path)))
        return "Choose an Atari 8 KiB or 16 KiB cartridge";
    const mt_sd_result_t file = mt_sd_storage_read_file_max(path, cart, sizeof(cart));
    if (file.status != MT_SD_OK) return mt_sd_status_name(file.status);
    mt_content_info_t info;
    const mt_content_validation_t status = mt_content_inspect(type, cart, file.size, &info);
    if (status != MT_CONTENT_VALID) return mt_content_validation_name(status);
    return change_cart(link, MT_LOAD_ATARI_XT_CART, cart + info.payload_offset,
        info.payload_size, relative, progress);
}

const char *atari_xt_content_eject(mt_core_link_t *link, mt_link_progress_fn progress)
{
    static const uint8_t eject = 0U;
    if (!active(link) || !roms_loaded) return "Load the Atari system ROMs first";
    return change_cart(link, MT_LOAD_ATARI_XT_CART_EJECT, &eject, sizeof(eject), NULL, progress);
}
