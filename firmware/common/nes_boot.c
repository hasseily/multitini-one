/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_nes_boot.h"
#include "multitini_nes_content.h"
static mt_link_result_t set_reset(mt_core_link_t *link, bool reset, uint32_t polls,
                                  mt_link_progress_fn progress, void *context)
{
    const uint32_t args[4] = {reset ? 1U : 0U, 0U, 0U, 0U};
    mt_command_response_t response;
    return mt_core_link_command(link, MT_COMMAND_CORE_RESET, args, polls,
                                 progress, context, &response);
}
mt_link_result_t mt_nes_stage_traced(mt_core_link_t *link,
    const mt_nes_boot_images_t *images, uint32_t polls,
    mt_link_progress_fn progress, void *context, mt_nes_transfer_phase_t *phase)
{
    mt_nes_content_info_t info;
    mt_link_result_t result;
    if (phase != NULL) *phase = MT_NES_TRANSFER_NONE;
    if (images == NULL || polls == 0U ||
        mt_nes_content_inspect(images->cartridge.data, images->cartridge.size,
                                &info) != MT_CONTENT_VALID ||
        !((images->save.data == NULL && images->save.size == 0U) ||
          (images->save.data != NULL && images->save.size == MT_SIZE_NES_SAVE && info.battery)))
        return MT_LINK_BAD_ARGUMENT;
    if (phase != NULL) *phase = MT_NES_TRANSFER_RESET;
    result = set_reset(link, true, polls, progress, context);
    if (result != MT_LINK_OK) return result;
    if (phase != NULL) *phase = MT_NES_TRANSFER_CARTRIDGE;
    result = mt_core_link_stream(link, MT_LOAD_NES_CART, images->cartridge.data,
                                 images->cartridge.size, polls, progress, context);
    if (result != MT_LINK_OK) return result;
    if (images->save.data != NULL) {
        if (phase != NULL) *phase = MT_NES_TRANSFER_SAVE;
        result = mt_core_link_stream(link, MT_LOAD_NES_SAVE, images->save.data,
                                     images->save.size, polls, progress, context);
    }
    return result;
}
mt_link_result_t mt_nes_stage(mt_core_link_t *link,
    const mt_nes_boot_images_t *images, uint32_t polls,
    mt_link_progress_fn progress, void *context)
{ return mt_nes_stage_traced(link, images, polls, progress, context, NULL); }
mt_link_result_t mt_nes_release(mt_core_link_t *link, uint32_t polls,
    mt_link_progress_fn progress, void *context)
{ return polls == 0U ? MT_LINK_BAD_ARGUMENT : set_reset(link, false, polls, progress, context); }
mt_link_result_t mt_nes_boot(mt_core_link_t *link,
    const mt_nes_boot_images_t *images, uint32_t polls,
    mt_link_progress_fn progress, void *context)
{
    const mt_link_result_t result = mt_nes_stage(link, images, polls, progress, context);
    return result == MT_LINK_OK ? mt_nes_release(link, polls, progress, context) : result;
}
