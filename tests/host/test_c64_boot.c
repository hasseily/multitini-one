/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_boot.h"
#include "multitini_c64_interface.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_kind;
    uint32_t active_size;
    uint32_t active_offset;
    uint32_t checksum;
    uint32_t completed_kinds;
    uint32_t reset_commands;
    uint32_t fail_data_at;
    int fail_data_enabled;
    int failure_injected;
    int core_reset;
} boot_shell_t;

static uint32_t read32(void *context, uint32_t offset)
{
    boot_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    return shell->registers[offset / sizeof(uint32_t)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    boot_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    shell->registers[offset / sizeof(uint32_t)] = value;
}

static void respond(void *context)
{
    boot_shell_t *const shell = context;
    const uint32_t sequence = shell->registers[MT_REG_COMMAND_SEQ / 4U];
    const uint32_t opcode = shell->registers[MT_REG_COMMAND_OPCODE / 4U];
    const uint32_t arg0 = shell->registers[MT_REG_COMMAND_ARG0 / 4U];
    const uint32_t arg1 = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
    uint32_t response_status = MT_RESPONSE_OK;

    if (sequence == shell->sequence) {
        return;
    }
    shell->sequence = sequence;

    switch (opcode) {
    case MT_COMMAND_CORE_RESET:
        shell->core_reset = arg0 != 0U;
        ++shell->reset_commands;
        break;
    case MT_COMMAND_STREAM_BEGIN:
        assert(shell->core_reset != 0);
        assert(shell->active_size == 0U);
        shell->active_kind = arg0;
        shell->active_size = arg1;
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_DATA:
        assert(shell->active_size != 0U);
        assert(shell->active_offset < shell->active_size);
        if (shell->fail_data_enabled != 0 && shell->failure_injected == 0 &&
            shell->active_offset == shell->fail_data_at) {
            response_status = MT_RESPONSE_INVALID_ARGUMENT;
            shell->failure_injected = 1;
        } else {
            shell->checksum += arg0;
            ++shell->active_offset;
        }
        break;
    case MT_COMMAND_STREAM_END:
        assert(shell->active_offset == shell->active_size);
        shell->completed_kinds |= UINT32_C(1) << shell->active_kind;
        shell->active_kind = 0U;
        shell->active_size = 0U;
        shell->active_offset = 0U;
        break;
    case MT_COMMAND_STREAM_ABORT:
        shell->active_kind = 0U;
        shell->active_size = 0U;
        shell->active_offset = 0U;
        break;
    default:
        assert(0);
    }

    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = response_status;
    shell->registers[MT_REG_RESPONSE_ARG0 / 4U] = 0U;
    shell->registers[MT_REG_RESPONSE_ARG1 / 4U] = 0U;
    shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

int main(void)
{
    uint8_t basic[MT_SIZE_C64_BASIC];
    uint8_t kernal[MT_SIZE_C64_KERNAL];
    uint8_t character[MT_SIZE_C64_CHARACTER];
    boot_shell_t shell = {0};
    mt_core_link_t link;
    mt_c64_system_roms_t roms;
    uint32_t expected_checksum = 0U;
    size_t index;

    for (index = 0U; index < sizeof(basic); ++index) {
        basic[index] = (uint8_t)index;
        expected_checksum += basic[index];
    }
    for (index = 0U; index < sizeof(kernal); ++index) {
        kernal[index] = (uint8_t)(index ^ 0xA5U);
        expected_checksum += kernal[index];
    }
    for (index = 0U; index < sizeof(character); ++index) {
        character[index] = (uint8_t)(index ^ 0x5AU);
        expected_checksum += character[index];
    }

    roms.basic.data = basic;
    roms.basic.size = sizeof(basic);
    roms.kernal.data = kernal;
    roms.kernal.size = sizeof(kernal);
    roms.character.data = character;
    roms.character.size = sizeof(character);

    mt_core_link_init(&link, &shell, read32, write32);
    assert(mt_c64_load_system_roms(&link, &roms, 2U, respond, &shell) ==
           MT_LINK_OK);
    assert(shell.core_reset == 0);
    assert(shell.reset_commands == 2U);
    assert(shell.completed_kinds ==
           ((UINT32_C(1) << MT_LOAD_C64_BASIC) |
            (UINT32_C(1) << MT_LOAD_C64_KERNAL) |
            (UINT32_C(1) << MT_LOAD_C64_CHARACTER)));
    assert(shell.checksum == expected_checksum);

    roms.basic.size = sizeof(basic) - 1U;
    assert(mt_c64_load_system_roms(&link, &roms, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);
    assert(mt_c64_load_system_roms(&link, NULL, 2U, respond, &shell) ==
           MT_LINK_BAD_ARGUMENT);

    {
        boot_shell_t failing_shell = {0};
        mt_core_link_t failing_link;
        failing_shell.fail_data_at = 17U;
        failing_shell.fail_data_enabled = 1;
        mt_core_link_init(&failing_link, &failing_shell, read32, write32);
        roms.basic.size = sizeof(basic);
        assert(mt_c64_load_system_roms(&failing_link, &roms, 2U, respond,
                                       &failing_shell) ==
               MT_LINK_REMOTE_ERROR);
        assert(failing_shell.failure_injected != 0);
        assert(failing_shell.core_reset != 0);
        assert(failing_shell.reset_commands == 2U);
        assert(failing_shell.completed_kinds == 0U);
    }
    return 0;
}
