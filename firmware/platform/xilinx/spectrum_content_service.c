/* SPDX-License-Identifier: GPL-3.0-only */
#include "spectrum_content_service.h"
#include "multitini_content.h"
#include "multitini_core_package.h"
#include "multitini_media_browser.h"
#include "multitini_spectrum_interface.h"
#include "multitini_spectrum_tzx.h"
#include "sd_storage.h"

#include "sleep.h"
#include <string.h>

#define POLLS 100000U
#define TAPE_MODE_PATH "0:/multitini/spectrum_tape.cfg"
#define TAPE_MODE_JOURNAL TAPE_MODE_PATH ".mtj"

static uint8_t image[MT_SPECTRUM_TAP_MAX_BYTES];
static uint8_t tzx_stream[MT_SPECTRUM_TZX_MAX_BYTES];
static char tape_name[MT_MEDIA_BROWSER_PATH_MAX + 1U];
static const char *const tape_modes[] = {
    [MT_SPECTRUM_TAPE_NORMAL] = "normal\n",
    [MT_SPECTRUM_TAPE_FAST] = "fast\n",
    [MT_SPECTRUM_TAPE_INSTANT] = "instant\n",
};

static uint32_t load_tape_mode(void)
{
    uint8_t wire[sizeof("instant\n") - 1U];
    bool found = false;
    const mt_sd_result_t recovered = mt_sd_storage_journal_recover(
        TAPE_MODE_PATH, TAPE_MODE_JOURNAL, wire, sizeof(wire));
    if (recovered.status != MT_SD_OK) return MT_SPECTRUM_TAPE_NORMAL;
    const mt_sd_result_t read = mt_sd_storage_read_file_optional_max(
        TAPE_MODE_PATH, wire, sizeof(wire), &found);
    if (read.status == MT_SD_OK && found) {
        for (uint32_t mode = MT_SPECTRUM_TAPE_NORMAL; mode <= MT_SPECTRUM_TAPE_INSTANT; ++mode)
            if (read.size == strlen(tape_modes[mode]) &&
                memcmp(wire, tape_modes[mode], read.size) == 0)
                return mode;
    }
    return MT_SPECTRUM_TAPE_NORMAL;
}

uint32_t spectrum_content_tape_config(const mt_core_link_t *link)
{
    const uint32_t value = link->read32(link->context, MT_REG_SPECTRUM_TAPE_CONFIG);
    return (value & MT_SPECTRUM_TAPE_SIGNATURE_MASK) == MT_SPECTRUM_TAPE_SIGNATURE ? value : 0U;
}

const char *spectrum_content_next_tape_mode(mt_core_link_t *link)
{
    const uint32_t config = spectrum_content_tape_config(link);
    if (config == 0U) return "Update the Spectrum FPGA image to change tape speed";
    const uint32_t current = config & MT_SPECTRUM_TAPE_MODE_MASK;
    const uint32_t stock_rom = config & MT_SPECTRUM_TAPE_STOCK_ROM;
    uint32_t next = MT_SPECTRUM_TAPE_NORMAL;
    if (current == MT_SPECTRUM_TAPE_NORMAL ||
        (current == MT_SPECTRUM_TAPE_INSTANT && stock_rom == 0U))
        next = MT_SPECTRUM_TAPE_FAST;
    else if (current == MT_SPECTRUM_TAPE_FAST && stock_rom != 0U) next = MT_SPECTRUM_TAPE_INSTANT;

    link->write32(link->context, MT_REG_SPECTRUM_TAPE_CONFIG, next | stock_rom);
    const uint32_t readback_mask = MT_SPECTRUM_TAPE_SIGNATURE_MASK |
        MT_SPECTRUM_TAPE_MODE_MASK | MT_SPECTRUM_TAPE_STOCK_ROM;
    if ((spectrum_content_tape_config(link) & readback_mask) != (MT_SPECTRUM_TAPE_SIGNATURE | next | stock_rom))
        return "Spectrum FPGA did not accept the tape speed";

    const char *const setting = tape_modes[next];
    const mt_sd_result_t saved = mt_sd_storage_journal_replace(
        TAPE_MODE_PATH, TAPE_MODE_JOURNAL, (const uint8_t *)setting, strlen(setting));
    if (saved.status == MT_SD_OK) return NULL;
    link->write32(link->context, MT_REG_SPECTRUM_TAPE_CONFIG, current | stock_rom);
    return mt_sd_status_name(saved.status);
}

static bool stock_tape_rom(const uint8_t *rom, size_t size)
{
    /* English retail ROM checksums. On 128K only BASIC ROM bank 1 uses
     * the stock tape entry points. The adapter checks which bank is mapped. */
    if (size == MT_SPECTRUM_ROM_48_BYTES)
        return mt_core_package_crc32(rom, size) == UINT32_C(0xddee531f);
    return mt_core_package_crc32(rom + MT_SPECTRUM_ROM_48_BYTES,
                                 MT_SPECTRUM_ROM_48_BYTES) == UINT32_C(0xb96a36be);
}

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
    mt_command_response_t response;
    mt_link_result_t result = mt_core_link_command(link, MT_COMMAND_CORE_RESET, reset_args,
        POLLS, progress, NULL, &response);
    if (result != MT_LINK_OK) return mt_link_result_name(result);
    const uint32_t tape_config = spectrum_content_tape_config(link);
    if (tape_config != 0U)
        link->write32(link->context, MT_REG_SPECTRUM_TAPE_CONFIG, tape_config & MT_SPECTRUM_TAPE_MODE_MASK);
    link->write32(link->context, MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    result = mt_core_link_stream(link, MT_LOAD_SPECTRUM_ROM, image, rom.size, POLLS, progress, NULL);
    if (result != MT_LINK_OK) return mt_link_result_name(result);
    if (tape_config != 0U) {
        const uint32_t stock = stock_tape_rom(image, rom.size) ? MT_SPECTRUM_TAPE_STOCK_ROM : 0U;
        link->write32(link->context, MT_REG_SPECTRUM_TAPE_CONFIG, load_tape_mode() | stock);
    }
    result = mt_core_link_command(link, MT_COMMAND_CORE_RESET, run_args, POLLS, progress, NULL,
        &response);
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
    if (link->read32(link->context, MT_REG_CORE_ID) != MT_SPECTRUM_CORE_ID ||
        (link->read32(link->context, MT_REG_STATUS) & MT_STATUS_CORE_READY) == 0U)
        return "Load the Spectrum ROM first";
    const mt_content_type_t type = mt_content_classify_for_core(MT_SPECTRUM_CORE_ID, relative);
    if (!mt_media_browser_join(media_root, relative, path, sizeof(path)) ||
        (type != MT_CONTENT_SPECTRUM_TAP && type != MT_CONTENT_SPECTRUM_TZX))
        return "Choose a Spectrum TAP or TZX image";
    const bool tzx = type == MT_CONTENT_SPECTRUM_TZX;
    if (tzx && (spectrum_content_tape_config(link) & MT_SPECTRUM_TAPE_TZX) == 0U)
        return "Update the Spectrum FPGA image to load TZX";
    const mt_sd_result_t tape = mt_sd_storage_read_file_max(path, image, sizeof(image));
    if (tape.status != MT_SD_OK) return mt_sd_status_name(tape.status);

    size_t stream_size;
    if (tzx) {
        const mt_spectrum_tzx_status_t status = mt_spectrum_tzx_compile(image, tape.size,
            tzx_stream, sizeof(tzx_stream), &stream_size);
        if (status != MT_SPECTRUM_TZX_OK) return mt_spectrum_tzx_status_name(status);
    } else {
        mt_content_info_t info;
        const mt_content_validation_t status =
            mt_content_inspect(MT_CONTENT_SPECTRUM_TAP, image, tape.size, &info);
        if (status != MT_CONTENT_VALID) return mt_content_validation_name(status);
        stream_size = info.payload_size;
    }

    link->write32(link->context, MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    const mt_link_result_t result = mt_core_link_stream(link,
        tzx ? MT_LOAD_SPECTRUM_TZX : MT_LOAD_SPECTRUM_TAP,
        tzx ? tzx_stream : image, stream_size, POLLS, progress, NULL);
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
