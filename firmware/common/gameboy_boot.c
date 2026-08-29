/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_gameboy_boot.h"

#include "multitini_content.h"
#include "multitini_gameboy_interface.h"

static mt_link_result_t set_core_reset(mt_core_link_t *link, int asserted,
                                       uint32_t max_polls,
                                       mt_link_progress_fn progress,
                                       void *progress_context)
{
    const uint32_t args[4] = {asserted != 0 ? 1U : 0U, 0U, 0U, 0U};
    mt_command_response_t response;
    return mt_core_link_command(link, MT_COMMAND_CORE_RESET, args, max_polls,
                                progress, progress_context, &response);
}

static int images_valid(const mt_gameboy_boot_images_t *images)
{
    mt_content_info_t cart_info;
    const int no_override = images != NULL &&
                            images->bootstrap_override.data == NULL &&
                            images->bootstrap_override.size == 0U;
    const int valid_override = images != NULL &&
                               images->bootstrap_override.data != NULL &&
                               images->bootstrap_override.size ==
                                   MT_SIZE_GBC_BOOT;
    if (images == NULL || images->cartridge.data == NULL ||
        (!no_override && !valid_override)) {
        return 0;
    }
    if (mt_content_inspect(MT_CONTENT_GB_CART, images->cartridge.data,
                           images->cartridge.size, &cart_info) !=
        MT_CONTENT_VALID) {
        return 0;
    }
    return no_override ||
           mt_gbc_boot_rom_validate(images->bootstrap_override.data,
                                    images->bootstrap_override.size) ==
               MT_CONTENT_VALID;
}

mt_link_result_t mt_gameboy_boot(
    mt_core_link_t *link, const mt_gameboy_boot_images_t *images,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context)
{
    mt_link_result_t result;

    if (!images_valid(images) || max_polls == 0U) {
        return MT_LINK_BAD_ARGUMENT;
    }
    result = set_core_reset(link, 1, max_polls, progress, progress_context);
    if (result != MT_LINK_OK) return result;

    if (images->bootstrap_override.data != NULL) {
        result = mt_core_link_stream(link, MT_LOAD_GBC_BOOT,
                                     images->bootstrap_override.data,
                                     images->bootstrap_override.size,
                                     max_polls, progress, progress_context);
        if (result != MT_LINK_OK) return result;
    }

    result = mt_core_link_stream(link, MT_LOAD_GB_CART,
                                 images->cartridge.data,
                                 images->cartridge.size,
                                 max_polls, progress, progress_context);
    if (result != MT_LINK_OK) return result;

    return set_core_reset(link, 0, max_polls, progress, progress_context);
}
