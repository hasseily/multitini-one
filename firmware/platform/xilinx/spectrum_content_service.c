/* SPDX-License-Identifier: GPL-3.0-only */
#include "spectrum_content_service.h"
#include "multitini_content.h"
#include "multitini_media_browser.h"
#include "multitini_spectrum_interface.h"
#include "sd_storage.h"

#include "sleep.h"
#include <string.h>

#define POLLS 100000U

static uint8_t image[MT_SPECTRUM_TAP_MAX_BYTES];
static char tape_name[MT_MEDIA_BROWSER_PATH_MAX + 1U];

void spectrum_content_reset_selection(void)
{
    tape_name[0] = '\0';
}

const char *spectrum_content_tape_name(void)
{
    return tape_name[0] == '\0' ? NULL : tape_name;
}

const char *spectrum_content_boot(mt_core_link_t *link, const char *core_directory,
                                 mt_link_progress_fn progress)
{
    char path[MT_MEDIA_BROWSER_PATH_MAX + 32U];
    if (link->read32(link->context, MT_REG_CORE_ID) != MT_SPECTRUM_CORE_ID)
        return "ZX Spectrum core is unavailable";
    if (!mt_media_browser_join(core_directory, "roms/spectrum.rom", path, sizeof(path)))
        return "Invalid Spectrum ROM path";
    const mt_sd_result_t rom = mt_sd_storage_read_file_max(path, image, MT_SPECTRUM_ROM_128_BYTES);
    if (rom.status == MT_SD_OPEN_FAILED)
        return "Supply roms/spectrum.rom";
    if (rom.status == MT_SD_TOO_LARGE ||
        (rom.status == MT_SD_OK && rom.size != MT_SPECTRUM_ROM_48_BYTES &&
         rom.size != MT_SPECTRUM_ROM_128_BYTES))
        return "Spectrum ROM must be 16 or 32 KiB";
    if (rom.status != MT_SD_OK) return mt_sd_status_name(rom.status);
    size_t different = 1U;
    while (different < rom.size && image[different] == image[0]) ++different;
    if (different == rom.size) return "Spectrum ROM contains a single repeated byte";

    const uint32_t reset_args[4] = {1U, 0U, 0U, 0U};
    const uint32_t run_args[4] = {0U, 0U, 0U, 0U};
    mt_link_result_t result = mt_core_link_command(link, MT_COMMAND_CORE_RESET, reset_args,
        POLLS, progress, NULL, NULL);
    if (result != MT_LINK_OK) return mt_link_result_name(result);
    link->write32(link->context, MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    result = mt_core_link_stream(link, MT_LOAD_SPECTRUM_ROM, image, rom.size, POLLS, progress, NULL);
    if (result != MT_LINK_OK) return mt_link_result_name(result);
    result = mt_core_link_command(link, MT_COMMAND_CORE_RESET, run_args, POLLS, progress, NULL, NULL);
    if (result != MT_LINK_OK) return mt_link_result_name(result);

    for (unsigned poll = 0; poll < POLLS; ++poll) {
        if (progress != NULL) progress(NULL);
        const uint32_t status = link->read32(link->context, MT_REG_STATUS);
        if ((status & MT_STATUS_CORE_LOAD_ERROR) != 0U) return "Spectrum ROM load rejected";
        if ((status & MT_STATUS_CORE_READY) != 0U) return NULL;
    }
    return "Spectrum did not leave reset";
}

const char *spectrum_content_insert_tape(mt_core_link_t *link, const char *media_root,
                                        const char *relative, mt_link_progress_fn progress)
{
    char path[2U * MT_MEDIA_BROWSER_PATH_MAX + 2U];
    mt_content_info_t info;
    if (link->read32(link->context, MT_REG_CORE_ID) != MT_SPECTRUM_CORE_ID ||
        (link->read32(link->context, MT_REG_STATUS) & MT_STATUS_CORE_READY) == 0U)
        return "Load the Spectrum ROM first";
    if (!mt_media_browser_join(media_root, relative, path, sizeof(path)) ||
        mt_content_classify_for_core(MT_SPECTRUM_CORE_ID, relative) != MT_CONTENT_SPECTRUM_TAP)
        return "Choose a Spectrum TAP image";
    const mt_sd_result_t tap = mt_sd_storage_read_file_max(path, image, sizeof(image));
    if (tap.status != MT_SD_OK) return mt_sd_status_name(tap.status);
    const mt_content_validation_t validation =
        mt_content_inspect(MT_CONTENT_SPECTRUM_TAP, image, tap.size, &info);
    if (validation != MT_CONTENT_VALID) return mt_content_validation_name(validation);

    link->write32(link->context, MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    const mt_link_result_t result = mt_core_link_stream(link, MT_LOAD_SPECTRUM_TAP, image,
        info.payload_size, POLLS, progress, NULL);
    if (result != MT_LINK_OK) {
        spectrum_content_reset_selection();
        return mt_link_result_name(result);
    }
    /* STREAM_END drains the data FIFO. The adapter decides on the final byte,
     * then the sticky result crosses back to the control clock. */
    usleep(1U);
    const uint32_t status = link->read32(link->context, MT_REG_STATUS);
    if ((status & (MT_STATUS_CORE_LOAD_ERROR | MT_STATUS_CORE_READY)) != MT_STATUS_CORE_READY) {
        spectrum_content_reset_selection();
        return "Spectrum tape load rejected";
    }
    memcpy(tape_name, relative, strlen(relative) + 1U);
    return NULL;
}
