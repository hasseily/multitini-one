/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_genesis_boot.h"
#include "multitini_genesis_content.h"
#include <string.h>

bool mt_genesis_config_valid(const mt_genesis_config_t *config, bool allow_auto)
{
    return config != NULL && config->region >= (allow_auto ?
        MT_GENESIS_REGION_AUTO : MT_GENESIS_REGION_US) &&
        config->region <= MT_GENESIS_REGION_JP && config->six_button_mask <= 3U &&
        config->fm_chip <= 1U && config->lowpass <= 3U;
}

static mt_link_result_t reset_core(mt_core_link_t *link, bool reset,
    uint32_t polls, mt_link_progress_fn progress, void *context)
{
    const uint32_t args[4] = {reset ? 1U : 0U, 0U, 0U, 0U};
    mt_command_response_t response;
    return mt_core_link_command(link, MT_COMMAND_CORE_RESET, args, polls,
                                progress, context, &response);
}

mt_link_result_t mt_genesis_stage(mt_core_link_t *link,
    const mt_genesis_boot_images_t *images, uint32_t polls,
    mt_link_progress_fn progress, void *context)
{
    mt_genesis_content_info_t info;
    mt_link_result_t result;
    uint8_t config[MT_SIZE_GENESIS_CONFIG] = {0};
    if (images == NULL || polls == 0U || !mt_genesis_config_valid(&images->config, false) ||
        mt_genesis_content_inspect(images->cartridge.data, images->cartridge.size,
            MT_GENESIS_FORMAT_RAW, &info) != MT_CONTENT_VALID || info.byte_swapped ||
        !((images->save.data == NULL && images->save.size == 0U) ||
          (images->save.data != NULL && images->save.size == MT_SIZE_GENESIS_SAVE)))
        return MT_LINK_BAD_ARGUMENT;
    memcpy(config, "MD01", 4U);
    config[4] = (uint8_t)images->config.region;
    config[5] = images->config.six_button_mask;
    config[6] = images->config.fm_chip;
    config[7] = images->config.lowpass;
    result = reset_core(link, true, polls, progress, context);
    if (result != MT_LINK_OK) return result;
    result = mt_core_link_stream(link, MT_LOAD_GENESIS_CONFIG, config, sizeof(config),
                                polls, progress, context);
    if (result != MT_LINK_OK) return result;
    result = mt_core_link_stream(link, MT_LOAD_GENESIS_CART, images->cartridge.data,
                                images->cartridge.size, polls, progress, context);
    if (result != MT_LINK_OK) return result;
    if (images->save.data != NULL)
        result = mt_core_link_stream(link, MT_LOAD_GENESIS_SAVE, images->save.data,
                                    images->save.size, polls, progress, context);
    return result;
}

mt_link_result_t mt_genesis_release(mt_core_link_t *link, uint32_t polls,
    mt_link_progress_fn progress, void *context)
{
    if (polls == 0U) return MT_LINK_BAD_ARGUMENT;
    return reset_core(link, false, polls, progress, context);
}

mt_link_result_t mt_genesis_boot(mt_core_link_t *link,
    const mt_genesis_boot_images_t *images, uint32_t polls,
    mt_link_progress_fn progress, void *context)
{
    const mt_link_result_t result = mt_genesis_stage(link, images, polls, progress, context);
    return result == MT_LINK_OK ? mt_genesis_release(link, polls, progress, context) : result;
}
