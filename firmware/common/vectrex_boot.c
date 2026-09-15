/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_vectrex_boot.h"
#include "multitini_vectrex_interface.h"

mt_link_result_t mt_vectrex_set_mode(mt_core_link_t *link, uint8_t mode,
    uint32_t max_polls, mt_link_progress_fn progress, void *context)
{
    if (mode > 2U) return MT_LINK_BAD_ARGUMENT;
    return mt_core_link_stream(link, MT_LOAD_VECTREX_MODE, &mode, 1U,
                               max_polls, progress, context);
}

mt_link_result_t mt_vectrex_boot(mt_core_link_t *link,
    const uint8_t *bios, size_t bios_size, const uint8_t *cart, size_t cart_size,
    uint32_t max_polls, mt_link_progress_fn progress, void *context)
{
    uint32_t args[4] = {1U, 0U, 0U, 0U};
    mt_command_response_t response;
    mt_link_result_t result;
    if (bios == NULL || bios_size != MT_SIZE_VECTREX_BIOS || max_polls == 0U ||
        ((cart == NULL) != (cart_size == 0U)) ||
        cart_size > MT_SIZE_VECTREX_CART_MAX) return MT_LINK_BAD_ARGUMENT;
    result = mt_core_link_command(link, MT_COMMAND_CORE_RESET, args, max_polls,
                                  progress, context, &response);
    if (result != MT_LINK_OK) return result;
    result = mt_vectrex_set_mode(link, 0U, max_polls, progress, context);
    if (result != MT_LINK_OK) return result;
    result = mt_core_link_stream(link, MT_LOAD_VECTREX_BIOS, bios, bios_size,
                                 max_polls, progress, context);
    if (result != MT_LINK_OK) return result;
    if (cart != NULL) {
        result = mt_core_link_stream(link, MT_LOAD_VECTREX_CART, cart, cart_size,
                                     max_polls, progress, context);
        if (result != MT_LINK_OK) return result;
    }
    args[0] = 0U;
    return mt_core_link_command(link, MT_COMMAND_CORE_RESET, args, max_polls,
                                progress, context, &response);
}
