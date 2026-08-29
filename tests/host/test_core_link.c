/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_link.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t progress_count;
} mock_shell_t;

static uint32_t mock_read(void *context, uint32_t offset)
{
    mock_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    return shell->registers[offset / sizeof(uint32_t)];
}

static void mock_write(void *context, uint32_t offset, uint32_t value)
{
    mock_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    shell->registers[offset / sizeof(uint32_t)] = value;
}

static void respond_on_second_poll(void *context)
{
    mock_shell_t *const shell = context;
    ++shell->progress_count;
    if (shell->progress_count == 2U) {
        const uint32_t sequence = shell->registers[MT_REG_COMMAND_SEQ / 4U];
        shell->registers[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_OK;
        shell->registers[MT_REG_RESPONSE_ARG0 / 4U] = UINT32_C(0x12345678);
        shell->registers[MT_REG_RESPONSE_ARG1 / 4U] = UINT32_C(0xABCDEF01);
        shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
    }
}

int main(void)
{
    mock_shell_t shell = {0};
    mt_core_link_t link;
    mt_command_response_t response = {0};
    const uint32_t args[4] = {1U, 2U, 3U, 4U};

    mt_core_link_init(&link, &shell, mock_read, mock_write);
    assert(mt_core_link_probe(&link) == MT_LINK_NO_SHELL);

    shell.registers[MT_REG_MAGIC / 4U] = MT_MAGIC;
    shell.registers[MT_REG_ABI_VERSION / 4U] = MT_ABI_VERSION;
    assert(mt_core_link_probe(&link) == MT_LINK_OK);

    shell.registers[MT_REG_ABI_VERSION / 4U] = ((MT_ABI_MAJOR + 1U) << 16);
    assert(mt_core_link_probe(&link) == MT_LINK_ABI_MISMATCH);
    shell.registers[MT_REG_ABI_VERSION / 4U] = MT_ABI_VERSION;

    assert(mt_core_link_command(&link, MT_COMMAND_PING, args, 8U,
                                respond_on_second_poll, &shell, &response) == MT_LINK_OK);
    assert(shell.registers[MT_REG_COMMAND_ARG0 / 4U] == 1U);
    assert(shell.registers[MT_REG_COMMAND_ARG3 / 4U] == 4U);
    assert(shell.registers[MT_REG_COMMAND_OPCODE / 4U] == MT_COMMAND_PING);
    assert(shell.registers[MT_REG_COMMAND_DOORBELL / 4U] == response.sequence);
    assert(response.arg0 == UINT32_C(0x12345678));
    assert(response.arg1 == UINT32_C(0xABCDEF01));

    shell.progress_count = 0U;
    shell.registers[MT_REG_RESPONSE_SEQ / 4U] = 0U;
    assert(mt_core_link_command(&link, MT_COMMAND_GET_STATUS, args, 1U,
                                NULL, NULL, &response) == MT_LINK_TIMEOUT);
    assert(mt_core_link_command(NULL, MT_COMMAND_PING, args, 1U,
                                NULL, NULL, &response) == MT_LINK_BAD_ARGUMENT);
    return 0;
}
