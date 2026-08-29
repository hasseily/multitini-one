/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_boot.h"

#include "multitini_c64_interface.h"

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

static int roms_valid(const mt_c64_system_roms_t *roms)
{
    return roms != NULL &&
           roms->basic.data != NULL &&
           roms->basic.size == MT_SIZE_C64_BASIC &&
           roms->kernal.data != NULL &&
           roms->kernal.size == MT_SIZE_C64_KERNAL &&
           roms->character.data != NULL &&
           roms->character.size == MT_SIZE_C64_CHARACTER;
}

mt_link_result_t mt_c64_load_system_roms(
    mt_core_link_t *link, const mt_c64_system_roms_t *roms,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context)
{
    mt_link_result_t result;

    if (!roms_valid(roms) || max_polls == 0U) {
        return MT_LINK_BAD_ARGUMENT;
    }

    result = set_core_reset(link, 1, max_polls, progress, progress_context);
    if (result != MT_LINK_OK) {
        return result;
    }

    result = mt_core_link_stream(link, MT_LOAD_C64_BASIC,
                                 roms->basic.data, roms->basic.size,
                                 max_polls, progress, progress_context);
    if (result == MT_LINK_OK) {
        result = mt_core_link_stream(link, MT_LOAD_C64_KERNAL,
                                     roms->kernal.data, roms->kernal.size,
                                     max_polls, progress, progress_context);
    }
    if (result == MT_LINK_OK) {
        result = mt_core_link_stream(link, MT_LOAD_C64_CHARACTER,
                                     roms->character.data,
                                     roms->character.size, max_polls,
                                     progress, progress_context);
    }
    if (result != MT_LINK_OK) {
        (void)set_core_reset(link, 1, max_polls, progress, progress_context);
        return result;
    }

    return set_core_reset(link, 0, max_polls, progress, progress_context);
}
