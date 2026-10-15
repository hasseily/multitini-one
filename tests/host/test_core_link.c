/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_link.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t progress_count;
    uint32_t observed_sequence;
    uint32_t observed_commands;
    uint32_t stream_kind;
    uint32_t stream_size;
    uint8_t stream_data[8];
    size_t stream_data_size;
    uint32_t burst_writes;
    uint32_t abort_commands;
    int stream_open;
    int stream_blocked;
    int inject_busy;
    uint32_t input_code;
    uint32_t input_value;
} mock_shell_t;

static uint32_t mock_read(void *context, uint32_t offset)
{
    mock_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    if (offset == MT_REG_STREAM_STATUS && shell->stream_open) {
        if (shell->stream_blocked != 0) return 0U;
        return shell->stream_data_size == shell->stream_size ?
               MT_STREAM_STATUS_DRAINED : MT_STREAM_STATUS_READY;
    }
    return shell->registers[offset / sizeof(uint32_t)];
}

static void mock_write(void *context, uint32_t offset, uint32_t value)
{
    mock_shell_t *const shell = context;
    assert(offset < MT_REGISTER_SPAN && (offset & 3U) == 0U);
    shell->registers[offset / sizeof(uint32_t)] = value;
    if (offset == MT_REG_STREAM_DATA_BURST) {
        unsigned lane;
        assert(shell->stream_open);
        ++shell->burst_writes;
        for (lane = 0U; lane < MT_STREAM_BURST_BYTES &&
             shell->stream_data_size < shell->stream_size; ++lane) {
            assert(shell->stream_data_size < sizeof(shell->stream_data));
            shell->stream_data[shell->stream_data_size++] =
                (uint8_t)(value >> (lane * 8U));
        }
    }
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

static void respond_to_stream(void *context)
{
    mock_shell_t *const shell = context;
    const uint32_t sequence = shell->registers[MT_REG_COMMAND_SEQ / 4U];
    const uint32_t opcode = shell->registers[MT_REG_COMMAND_OPCODE / 4U];
    uint32_t status = MT_RESPONSE_OK;

    if (sequence == shell->observed_sequence) {
        return;
    }
    shell->observed_sequence = sequence;
    ++shell->observed_commands;

    if (opcode == MT_COMMAND_STREAM_BEGIN) {
        shell->stream_kind = shell->registers[MT_REG_COMMAND_ARG0 / 4U];
        shell->stream_size = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
        shell->stream_open = 1;
    } else if (opcode == MT_COMMAND_STREAM_DATA) {
        if (shell->inject_busy != 0 && shell->stream_data_size == 1U) {
            status = MT_RESPONSE_BUSY;
            shell->inject_busy = 0;
        } else {
            assert(shell->stream_data_size < sizeof(shell->stream_data));
            shell->stream_data[shell->stream_data_size++] =
                (uint8_t)shell->registers[MT_REG_COMMAND_ARG0 / 4U];
        }
    } else if (opcode == MT_COMMAND_INPUT_EVENT) {
        shell->input_code = shell->registers[MT_REG_COMMAND_ARG0 / 4U];
        shell->input_value = shell->registers[MT_REG_COMMAND_ARG1 / 4U];
    } else if (opcode == MT_COMMAND_STREAM_END ||
               opcode == MT_COMMAND_STREAM_ABORT) {
        if (opcode == MT_COMMAND_STREAM_ABORT) ++shell->abort_commands;
        shell->stream_open = 0;
    }

    shell->registers[MT_REG_RESPONSE_STATUS / 4U] = status;
    shell->registers[MT_REG_RESPONSE_ARG0 / 4U] = 0U;
    shell->registers[MT_REG_RESPONSE_ARG1 / 4U] = 0U;
    shell->registers[MT_REG_RESPONSE_SEQ / 4U] = sequence;
}

static int continue_twice(void *context)
{
    uint32_t *const remaining = context;
    if (*remaining == 0U) return 0;
    --*remaining;
    return 1;
}

int main(void)
{
    mock_shell_t shell = {0};
    mt_core_link_t link;
    mt_command_response_t response = {0};
    const uint32_t args[4] = {1U, 2U, 3U, 4U};
    const uint8_t stream_data[3] = {0x11U, 0x22U, 0x33U};
    uint32_t remaining;

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

    shell.observed_sequence = shell.registers[MT_REG_RESPONSE_SEQ / 4U];
    shell.inject_busy = 1;
    shell.registers[MT_REG_ABI_VERSION / 4U] =
        (MT_ABI_MAJOR << 16) | 7U;
    assert(mt_core_link_stream(&link, 7U, stream_data, sizeof(stream_data), 8U,
                               respond_to_stream, &shell) == MT_LINK_OK);
    assert(shell.stream_kind == 7U);
    assert(shell.stream_size == sizeof(stream_data));
    assert(shell.stream_data_size == sizeof(stream_data));
    assert(shell.stream_data[0] == 0x11U);
    assert(shell.stream_data[1] == 0x22U);
    assert(shell.stream_data[2] == 0x33U);
    assert(shell.observed_commands == 6U);

    shell.stream_data_size = 0U;
    shell.observed_commands = 0U;
    shell.burst_writes = 0U;
    shell.registers[MT_REG_ABI_VERSION / 4U] = MT_ABI_VERSION;
    assert(mt_core_link_stream(&link, 8U, stream_data, sizeof(stream_data), 8U,
                               respond_to_stream, &shell) == MT_LINK_OK);
    assert(shell.stream_kind == 8U);
    assert(shell.stream_data_size == sizeof(stream_data));
    assert(memcmp(shell.stream_data, stream_data, sizeof(stream_data)) == 0);
    assert(shell.burst_writes == 1U);
    assert(shell.observed_commands == 2U);

    /* Prefix overlays can cross a burst lane boundary. Neither source buffer
     * changes, and the command-only compatibility path sends identical bytes. */
    const uint8_t original[7]={1U,2U,3U,4U,5U,6U,7U};
    const uint8_t prefix[3]={11U,12U,13U};
    const uint8_t expected[7]={11U,12U,13U,4U,5U,6U,7U};
    for(unsigned burst=0U;burst<2U;++burst) {
        shell.stream_data_size=0U;
        shell.registers[MT_REG_ABI_VERSION/4U]=(MT_ABI_MAJOR<<16U)|(burst ? MT_ABI_MINOR:7U);
        assert(mt_core_link_stream_with_prefix(&link,8U,original,sizeof(original),
            prefix,sizeof(prefix),8U,respond_to_stream,&shell)==MT_LINK_OK);
        assert(shell.stream_data_size==sizeof(original) &&
            memcmp(shell.stream_data,expected,sizeof(expected))==0);
        assert(original[0]==1U && original[6]==7U && prefix[0]==11U);
    }
    assert(mt_core_link_stream_with_prefix(&link,8U,original,sizeof(original),
        NULL,1U,8U,respond_to_stream,&shell)==MT_LINK_BAD_ARGUMENT);
    assert(mt_core_link_stream_with_prefix(&link,8U,original,sizeof(original),
        prefix,sizeof(original)+1U,8U,respond_to_stream,&shell)==MT_LINK_BAD_ARGUMENT);
    shell.registers[MT_REG_ABI_VERSION/4U]=MT_ABI_VERSION;
    shell.stream_data_size=0U;shell.stream_blocked=1;
    assert(mt_core_link_stream_with_prefix(&link,8U,original,sizeof(original),
        prefix,sizeof(prefix),2U,respond_to_stream,&shell)==MT_LINK_TIMEOUT);
    assert(shell.stream_open==0 && shell.abort_commands==1U);
    shell.stream_blocked=0;

    shell.stream_data_size = 0U;
    shell.observed_commands = 0U;
    shell.stream_blocked = 1;
    remaining = 2U;
    assert(mt_core_link_stream_until(
               &link, 9U, stream_data, sizeof(stream_data), 8U,
               respond_to_stream, &shell, continue_twice, &remaining) ==
           MT_LINK_TIMEOUT);
    assert(shell.abort_commands == 2U);
    assert(shell.stream_open == 0);
    shell.stream_blocked = 0;

    assert(mt_core_link_input_event(&link, UINT16_C(0x001C),
                                    UINT16_C(0x0001), 8U,
                                    respond_to_stream, &shell) == MT_LINK_OK);
    assert(shell.input_code == UINT32_C(0x001C));
    assert(shell.input_value == UINT32_C(0x0001));
    assert(mt_core_link_input_event(&link, 0U, 0U, 0U,
                                    NULL, NULL) == MT_LINK_BAD_ARGUMENT);

    assert(mt_core_link_stream(&link, 0U, NULL, 1U, 1U,
                               NULL, NULL) == MT_LINK_BAD_ARGUMENT);
    assert(mt_core_link_stream(&link, 0U, stream_data, 0U, 1U,
                               NULL, NULL) == MT_LINK_BAD_ARGUMENT);
#if SIZE_MAX > UINT32_MAX
    assert(mt_core_link_stream(&link, 0U, stream_data,
                               (size_t)UINT32_MAX + 1U, 1U,
                               NULL, NULL) == MT_LINK_BAD_ARGUMENT);
#endif
    return 0;
}
