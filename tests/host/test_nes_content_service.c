/* SPDX-License-Identifier: GPL-3.0-only */
#include "nes_content_service.h"

#include "multitini_nes_interface.h"
#include "multitini_regs.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t registers[MT_REGISTER_SPAN / sizeof(uint32_t)];
    uint32_t sequence;
    uint32_t active_size;
    uint32_t active_offset;
    uint8_t active_kind;
    uint8_t completed_kinds[4];
    uint32_t completed_sizes[4];
    unsigned completed_streams;
    unsigned reset_commands;
    unsigned mount_commands;
    unsigned eject_commands;
    unsigned completion_commands;
    uint32_t mount_flags;
    uint32_t mount_size;
    uint32_t mount_generation;
    uint32_t completion_status;
    uint32_t completion_blocks;
    uint8_t media_data[MT_SIZE_NES_SAVE];
    uint8_t loaded_save[MT_SIZE_NES_SAVE];
    uint8_t loaded_header[16];
    size_t media_data_size;
    size_t media_data_offset;
    int raise_load_error_on_end;
    bool stream_open,reset_asserted,cart_committed;
    uint32_t burst_word;
    unsigned burst_count,burst_index,burst_writes,cart_write_polls,save_wait_polls;
    uint8_t stalled_kind;
    uint32_t stalled_offset;
    unsigned abort_commands;
    unsigned commands,snapshot_reads,media_reads;
    bool reject_assertion,reject_release,reject_mount,never_ready;
} shell_t;

static char requested_path[256];
static char requested_save_path[384];
static char requested_journal_path[384];
static char committed_save_path[384];
static char committed_journal_path[384];
static uint8_t committed_save[MT_SIZE_NES_SAVE];
static size_t committed_size;
static bool cartridge_battery=true;
static int fail_read;
static int fail_save_read;
static int fail_save_commit;
static bool save_exists;

static size_t register_index(uint32_t offset)
{
    return (size_t)(offset / (uint32_t)sizeof(uint32_t));
}

static void transport_step(shell_t *shell)
{
    if(shell->cart_write_polls!=0U && --shell->cart_write_polls==0U)
        shell->cart_committed=true;
    if(shell->burst_count!=0U) {
        if(shell->active_kind==shell->stalled_kind && shell->active_offset>=shell->stalled_offset)return;
        if(shell->active_kind==MT_LOAD_NES_SAVE && !shell->cart_committed) {
            ++shell->save_wait_polls;
            return;
        }
        assert(shell->reset_asserted && shell->active_offset<shell->active_size);
        if(shell->active_kind==MT_LOAD_NES_SAVE)
            shell->loaded_save[shell->active_offset]=(uint8_t)(shell->burst_word>>(shell->burst_index*8U));
        if(shell->active_kind==MT_LOAD_NES_CART && shell->active_offset<16U)
            shell->loaded_header[shell->active_offset]=(uint8_t)(shell->burst_word>>(shell->burst_index*8U));
        ++shell->active_offset;++shell->burst_index;
        if(shell->burst_index==shell->burst_count) { shell->burst_count=0U;shell->burst_index=0U; }
        if(shell->active_offset==shell->active_size && shell->active_kind==MT_LOAD_NES_CART)
            shell->cart_write_polls=32U; /* Final accepted byte precedes physical PSRAM completion. */
    }
    if(!shell->reset_asserted && shell->cart_committed && !shell->never_ready)
        shell->registers[register_index(MT_REG_STATUS)]|=MT_STATUS_CORE_READY;
}

static uint32_t read32(void *context, uint32_t offset)
{
    shell_t *const shell = context;
    if(offset==MT_REG_ERROR)++shell->snapshot_reads;
    if(offset==MT_REG_STREAM_STATUS) {
        transport_step(shell);
        if(!shell->stream_open)return 0U;
        return shell->active_offset==shell->active_size && shell->burst_count==0U ?
            MT_STREAM_STATUS_DRAINED : shell->burst_count==0U ? MT_STREAM_STATUS_READY:0U;
    }
    if(offset==MT_REG_STATUS)transport_step(shell);
    if (offset == MT_REG_MEDIA_DATA) {
        ++shell->media_reads;
        uint32_t value;
        bool last;
        if (shell->media_data_offset >= shell->media_data_size) return 0U;
        last = shell->media_data_offset + 1U == shell->media_data_size;
        value = MT_MEDIA_DATA_VALID |
            ((uint32_t)shell->media_data[shell->media_data_offset] <<
             MT_MEDIA_DATA_BYTE_SHIFT) |
            (last ? MT_MEDIA_DATA_LAST : 0U);
        ++shell->media_data_offset;
        if (last)
            shell->registers[register_index(MT_REG_STATUS)] &=
                ~MT_STATUS_MEDIA_DATA;
        return value;
    }
    return shell->registers[register_index(offset)];
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    shell_t *const shell = context;
    if (offset == MT_REG_ERROR && (value & MT_ERROR_CORE_LOAD) != 0U) {
        shell->registers[register_index(MT_REG_STATUS)] &=
            ~MT_STATUS_CORE_LOAD_ERROR;
        return;
    }
    shell->registers[register_index(offset)] = value;
    if(offset==MT_REG_STREAM_DATA_BURST) {
        assert(shell->stream_open && shell->reset_asserted && shell->burst_count==0U);
        const uint32_t remaining=shell->active_size-shell->active_offset;
        assert(remaining!=0U);
        shell->burst_word=value;shell->burst_index=0U;
        shell->burst_count=remaining>=4U ? 4U:(unsigned)remaining;
        ++shell->burst_writes;
    }
}

static void respond(void *context)
{
    shell_t *const shell = context;
    const uint32_t sequence =
        shell->registers[register_index(MT_REG_COMMAND_SEQ)];
    const uint32_t opcode =
        shell->registers[register_index(MT_REG_COMMAND_OPCODE)];
    const uint32_t arg0 =
        shell->registers[register_index(MT_REG_COMMAND_ARG0)];
    const uint32_t arg1 =
        shell->registers[register_index(MT_REG_COMMAND_ARG1)];
    const uint32_t arg2 =
        shell->registers[register_index(MT_REG_COMMAND_ARG2)];
    const uint32_t arg3 =
        shell->registers[register_index(MT_REG_COMMAND_ARG3)];
    uint32_t response_status=MT_RESPONSE_OK;
    if (sequence == shell->sequence) return;
    shell->sequence = sequence;
    ++shell->commands;
    if (opcode == MT_COMMAND_STREAM_BEGIN) {
        shell->active_kind = (uint8_t)arg0;
        shell->active_size = arg1;
        shell->active_offset = 0U;
        shell->stream_open=true;shell->burst_count=0U;
        if(shell->active_kind==MT_LOAD_NES_CART) shell->cart_committed=false;
    } else if (opcode == MT_COMMAND_STREAM_DATA) {
        if (shell->active_kind == MT_LOAD_NES_SAVE) {
            assert(shell->active_offset < sizeof(shell->loaded_save));
            shell->loaded_save[shell->active_offset] = (uint8_t)arg0;
        }
        ++shell->active_offset;
        if(shell->active_offset==shell->active_size && shell->active_kind==MT_LOAD_NES_CART)shell->cart_committed=true;
    } else if (opcode == MT_COMMAND_STREAM_END) {
        assert(shell->active_offset == shell->active_size);
        assert(shell->burst_count==0U);shell->stream_open=false;
        assert(shell->completed_streams < 4U);
        shell->completed_kinds[shell->completed_streams] = shell->active_kind;
        shell->completed_sizes[shell->completed_streams] = shell->active_size;
        ++shell->completed_streams;
        if (shell->raise_load_error_on_end != 0)
            shell->registers[register_index(MT_REG_STATUS)] |=
                MT_STATUS_CORE_LOAD_ERROR;
    } else if (opcode == MT_COMMAND_CORE_RESET) {
        ++shell->reset_commands;
        if((arg0!=0U && shell->reject_assertion) || (arg0==0U && shell->reject_release))
            response_status=MT_RESPONSE_IO_ERROR;
        else {
        shell->reset_asserted=arg0!=0U;
        shell->registers[register_index(MT_REG_CONTROL)]=shell->reset_asserted ? MT_CONTROL_CORE_RESET_REQUEST:0U;
        if (arg0 == 0U && shell->cart_committed && !shell->never_ready)
            shell->registers[register_index(MT_REG_STATUS)] |=
                MT_STATUS_CORE_READY;
        else
            shell->registers[register_index(MT_REG_STATUS)] &=
                ~MT_STATUS_CORE_READY;
        }
    } else if (opcode == MT_COMMAND_MEDIA_MOUNT) {
        ++shell->mount_commands;
        if(shell->reject_mount)response_status=MT_RESPONSE_IO_ERROR;
        else {
        shell->mount_flags = arg0;
        shell->mount_size = arg1;
        shell->mount_generation = arg2;
        shell->registers[register_index(MT_REG_MEDIA_STATE)] =
            MT_MEDIA_STATE_MOUNTED;
        }
    } else if (opcode == MT_COMMAND_MEDIA_EJECT) {
        ++shell->eject_commands;
        if ((shell->registers[register_index(MT_REG_MEDIA_STATE)] &
             MT_MEDIA_STATE_DIRTY) != 0U) {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] |=
                MT_MEDIA_STATE_EJECT_PENDING;
        } else {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] = 0U;
        }
    } else if (opcode == MT_COMMAND_MEDIA_COMPLETE) {
        ++shell->completion_commands;
        shell->completion_status = arg1;
        shell->completion_blocks = arg2;
        assert(arg0 ==
               shell->registers[register_index(MT_REG_MEDIA_REQUEST_ID)]);
        assert(arg3 == shell->registers[
                           register_index(MT_REG_MEDIA_REQUEST_GENERATION)]);
        shell->registers[register_index(MT_REG_MEDIA_REQUEST)] = 0U;
        if (arg1 == MT_RESPONSE_OK) {
            shell->registers[register_index(MT_REG_MEDIA_STATE)] &=
                ~MT_MEDIA_STATE_DIRTY;
            if ((shell->registers[register_index(MT_REG_MEDIA_STATE)] &
                 MT_MEDIA_STATE_EJECT_PENDING) != 0U)
                shell->registers[register_index(MT_REG_MEDIA_STATE)] = 0U;
        } else {
            shell->registers[register_index(MT_REG_STATUS)] |=
                MT_STATUS_CORE_LOAD_ERROR;
        }
    } else {
        assert(opcode == MT_COMMAND_STREAM_ABORT);
        shell->stream_open=false;shell->burst_count=0U;
        ++shell->abort_commands;shell->cart_committed=false;
    }
    shell->registers[register_index(MT_REG_RESPONSE_STATUS)] = response_status;
    shell->registers[register_index(MT_REG_RESPONSE_SEQ)] = sequence;
}

static void finish_header(uint8_t *cart, size_t size)
{
    memset(cart, 0, size);
    assert(size == 16U+32768U);
    memcpy(cart, "NES\032", 4U);
    cart[4] = 2U;
    cart[6] = 0x10U | (cartridge_battery ? 2U : 0U);
    cart[7] = 8U;
    cart[10] = cartridge_battery ? 0x70U : 7U;
    cart[11] = 7U;
    cart[15] = 1U;
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    static uint8_t cart[16U+32768U];
    assert(strlen(path) < sizeof(requested_path));
    strcpy(requested_path, path);
    if (fail_read != 0) {
        result.status = MT_SD_READ_FAILED;
        result.backend_status = 9U;
        return result;
    }
    assert(capacity >= sizeof(cart));
    finish_header(cart, sizeof(cart));
    if (strstr(path, "bad") != NULL) cart[0] ^= 1U;
    if (strstr(path, "unsupported") != NULL) cart[8] = 0x10U;
    memcpy(dst, cart, sizeof(cart));
    result.size = sizeof(cart);
    return result;
}

mt_sd_result_t mt_sd_storage_journal_recover(const char *path,
                                             const char *journal_path,
                                             uint8_t *recovery_buffer,
                                             size_t capacity)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    (void)recovery_buffer;
    (void)capacity;
    strcpy(requested_save_path, path);
    strcpy(requested_journal_path, journal_path);
    return result;
}

mt_sd_result_t mt_sd_storage_read_file_optional_exact(
    const char *path, uint8_t *dst, size_t expected_size, bool *found)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    assert(strcmp(path, requested_save_path) == 0);
    if (fail_save_read != 0) {
        result.status = MT_SD_READ_FAILED;
        result.backend_status = 11U;
        return result;
    }
    *found = save_exists;
    if (save_exists) {
        assert(expected_size == committed_size);
        memcpy(dst, committed_save, expected_size);
        result.size = expected_size;
    }
    return result;
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
                                             const char *journal_path,
                                             const uint8_t *src,
                                             size_t size)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    strcpy(committed_save_path, path);
    strcpy(committed_journal_path, journal_path);
    if (fail_save_commit != 0) {
        result.status = MT_SD_WRITE_FAILED;
        result.backend_status = 12U;
        return result;
    }
    assert(size <= sizeof(committed_save));
    memcpy(committed_save, src, size);
    committed_size = size;
    save_exists = true;
    result.size = size;
    return result;
}

static void queue_save_request(shell_t *shell, uint32_t request_id)
{
    size_t index;
    assert(shell->mount_size <= sizeof(shell->media_data));
    for (index = 0U; index < shell->mount_size; ++index)
        shell->media_data[index] = (uint8_t)(index ^ request_id);
    shell->media_data_size = shell->mount_size;
    shell->media_data_offset = 0U;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST)] =
        MT_MEDIA_REQUEST_VALID | MT_MEDIA_REQUEST_WRITE;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_ID)] = request_id;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_LBA)] = 0U;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_BLOCK_COUNT)] =
        shell->mount_size / MT_MEDIA_BLOCK_BYTES;
    shell->registers[register_index(MT_REG_MEDIA_REQUEST_GENERATION)] =
        shell->mount_generation;
    shell->registers[register_index(MT_REG_MEDIA_STATE)] |=
        MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY;
    shell->registers[register_index(MT_REG_STATUS)] |= MT_STATUS_MEDIA_DATA;
}

int main(void)
{
    shell_t shell = {0};
    mt_core_link_t link;
    shell.registers[register_index(MT_REG_ABI_VERSION)]=MT_ABI_VERSION;
    shell.registers[register_index(MT_REG_CORE_ID)]=MT_NES_CORE_ID;
    mt_core_link_init(&link,&shell,read32,write32);
    nes_content_service_init(&link,"0:/multitini/cores/nes/media",true,respond,&shell);
    mt_nes_content_service_result_t result=nes_content_service_load("demo.nes");
    assert(result.status==MT_NES_CONTENT_SERVICE_OK && result.phase==MT_NES_TRANSFER_READY);
    assert(strcmp(requested_path,"0:/multitini/cores/nes/media/demo.nes")==0);
    assert(strcmp(requested_save_path,"0:/multitini/cores/nes/media/demo.nes.sav")==0);
    assert(strcmp(requested_journal_path,"0:/multitini/cores/nes/media/demo.nes.sav.mtj")==0);
    assert(result.save_active && !result.save_existed && result.save_size==MT_SIZE_NES_SAVE);
    assert(shell.completed_streams==2U && shell.completed_kinds[0]==MT_LOAD_NES_CART &&
        shell.completed_kinds[1]==MT_LOAD_NES_SAVE && !shell.reset_asserted);
    assert(shell.mount_size==MT_SIZE_NES_SAVE && shell.save_wait_polls>0U);
    assert(shell.loaded_header[7]==0U);
    for(size_t i=8U;i<16U;++i)assert(shell.loaded_header[i]==0U);
    for(size_t i=0U;i<MT_SIZE_NES_SAVE;++i)assert(shell.loaded_save[i]==0U);
    const uint32_t generation=shell.mount_generation;

    /* Write success is returned only after the durable journal operation. */
    queue_save_request(&shell,7U);
    assert(nes_content_service_save_dirty());
    nes_content_service_poll();
    assert(shell.completion_status==MT_RESPONSE_OK && shell.completion_blocks==32U);
    assert(committed_size==MT_SIZE_NES_SAVE && !nes_content_service_save_dirty());
    for(size_t i=0U;i<committed_size;++i)assert(committed_save[i]==(uint8_t)(i^7U));

    /* A failed write blocks reset and preserves the cartridge for retry. */
    queue_save_request(&shell,8U);fail_save_commit=1;
    nes_content_service_poll();
    assert(nes_content_service_save_faulted() && shell.completion_status==MT_RESPONSE_IO_ERROR);
    assert(nes_content_service_prepare_reset()!=0 && nes_content_service_cartridge_attached());
    fail_save_commit=0;
    assert(nes_content_service_retry_save()==0);
    queue_save_request(&shell,9U);nes_content_service_poll();
    assert(!nes_content_service_save_faulted());
    assert(nes_content_service_prepare_reset()==0);
    assert(nes_content_service_resume_after_reset()==0 && shell.mount_generation!=generation);

    /* Stale generation never reads or commits the supplied FIFO bytes. */
    queue_save_request(&shell,10U);
    shell.registers[register_index(MT_REG_MEDIA_REQUEST_GENERATION)]=generation;
    const unsigned reads=shell.media_reads;
    nes_content_service_poll();
    assert(shell.completion_status==MT_RESPONSE_INVALID_ARGUMENT && shell.media_reads==reads);
    assert(nes_content_service_retry_save()==0);
    queue_save_request(&shell,11U);nes_content_service_poll();

    /* Rejected content and failed reads leave the mounted cartridge intact. */
    const unsigned resets=shell.reset_commands;
    assert(nes_content_service_load("../demo.nes").status==MT_NES_CONTENT_SERVICE_INVALID_ARGUMENT);
    assert(nes_content_service_load("bad.nes").status==MT_NES_CONTENT_SERVICE_REJECTED);
    result=nes_content_service_load("unsupported.nes");
    assert(result.status==MT_NES_CONTENT_SERVICE_REJECTED &&
        strcmp(result.rejection_reason,"NES cartridge board variant is unsupported")==0);
    fail_read=1;
    assert(nes_content_service_load("new.nes").status==MT_NES_CONTENT_SERVICE_READ_FAILED);
    fail_read=0;
    assert(shell.reset_commands==resets && nes_content_service_cartridge_attached());
    assert(strcmp(nes_content_service_cartridge_name(),"demo.nes")==0);

    result=nes_content_service_detach();
    assert(result.status==MT_NES_CONTENT_SERVICE_OK && shell.reset_asserted);
    assert(!nes_content_service_cartridge_attached() && !nes_content_service_save_active());
    shell.completed_streams=0U;
    result=nes_content_service_load("demo.nes");
    assert(result.status==MT_NES_CONTENT_SERVICE_OK && result.save_existed);
    for(size_t i=0U;i<MT_SIZE_NES_SAVE;++i)assert(shell.loaded_save[i]==(uint8_t)(i^11U));
    assert(nes_content_service_detach().status==MT_NES_CONTENT_SERVICE_OK);

    /* Nonbattery cartridges have volatile RAM and create no save file. */
    cartridge_battery=false;shell.completed_streams=0U;
    const unsigned mounts=shell.mount_commands;
    result=nes_content_service_load("volatile.nes");
    assert(result.status==MT_NES_CONTENT_SERVICE_OK && !result.save_active);
    assert(shell.mount_commands==mounts && shell.completed_streams==1U);
    assert(nes_content_service_detach().status==MT_NES_CONTENT_SERVICE_OK);

    /* A failed mount leaves reset asserted, with a diagnostic snapshot. */
    cartridge_battery=true;shell.completed_streams=0U;shell.reject_mount=true;
    result=nes_content_service_load("mount.nes");
    assert(result.status==MT_NES_CONTENT_SERVICE_SAVE_MOUNT_FAILED && result.snapshot_valid);
    assert(shell.reset_asserted && result.phase==MT_NES_TRANSFER_MOUNT);
    puts("PASS: NES battery load, durable commit, retry barriers, generations and detach");
    return 0;
}

