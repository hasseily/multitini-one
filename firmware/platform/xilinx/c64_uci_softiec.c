/* SPDX-License-Identifier: GPL-3.0-only
 * Protocol: pinned Ultimate SoftIECTarget, retained in
 * third_party/ultimate_firmware_reference/software/io/command_interface/.
 * This implementation shares the rooted foreground device9 DOS instance.
 */
#include "c64_iec_service.h"
#include "c64_uci_internal.h"
#include <stdio.h>
#include <string.h>

enum
{
    IDLE,
    INPUT,
    HOLD_LOAD,
    LOAD,
    HOLD_SAVE,
    SAVE,
    READ_FILE,
    WRITE_FILE
};
static unsigned state, channel;
static bool leased, load_ready, verify, last_window;
static uint16_t start_address, address, end_address;
static size_t prepared;
static uint32_t status_generation;
static uint8_t block[256], memory[256];
static uint8_t snapshot[65536];
static size_t snapshot_size, snapshot_cursor;

static mt_uci_result_t binary(mt_uci_reply_t *r, unsigned code)
{
    mt_uci_reply_clear(r);
    r->status[0] = (uint8_t)code;
    r->status_length = 1;
    return MT_UCI_REPLY;
}
static void release_channel(void)
{
    if (leased)
        c64_iec_service_channel_release(channel);
    leased = false;
}
static bool close_owned_channel(mt_iec_dos_t *d, unsigned ch)
{
    int rc = mt_iec_dos_abort_channel(d, ch);
    if (!d->channels[ch].kind) {
        /* close may return a previously latched error after successfully
         * releasing the handle. Preserve that error, but not a stale lease. */
        release_channel();
        load_ready = false;
        state = IDLE;
        prepared = 0;
    }
    if (rc) {
        c64_iec_service_write_failure(rc);
        c64_uci_environment.fault = true;
        return false;
    }
    return true;
}
static bool acquire(unsigned ch)
{
    if (leased)
        return channel == ch;
    if (!c64_iec_service_channel_acquire(ch))
        return false;
    channel = ch;
    leased = true;
    return true;
}
static bool text(const uint8_t *in, size_t length, char *out, size_t capacity)
{
    if (length && in[length - 1] == 0)
        --length;
    if (length >= capacity || memchr(in, 0, length))
        return false;
    memcpy(out, in, length);
    out[length] = 0;
    return true;
}
static int open_name(mt_iec_dos_t *d, unsigned ch, const uint8_t *name, size_t length)
{
    if (length && !name[length - 1]) --length;
    return mt_iec_dos_open(d, ch, name, length);
}
static int commit_input(mt_iec_dos_t *d, size_t consumed)
{
    if (consumed > prepared)
        return 70;
    /* A native operation on another channel can replace the DOS status.
     * Never consume bytes from that newer status using an older UCI ACK. */
    int rc = channel == 15 && status_generation != d->status_generation
                 ? 0
                 : mt_iec_dos_consume_many(d, channel, consumed);
    prepared = 0;
    return rc;
}
static mt_uci_result_t input_reply(mt_iec_dos_t *d, mt_uci_reply_t *r, size_t count, bool initial)
{
    mt_uci_reply_clear(r);
    int rc = mt_iec_dos_peek_many(d, channel, r->data, count, &prepared, &last_window);
    if (rc) {
        release_channel();
        state = IDLE;
        return binary(r, 3);
    }
    status_generation = d->status_generation;
    r->data_length = prepared;
    r->more = !last_window;
    if (!initial) {
        r->status[0] = 0;
        r->status_length = 1;
    }
    return MT_UCI_REPLY;
}
static mt_uci_result_t finish_memory(mt_iec_dos_t *d, mt_uci_reply_t *r, unsigned result)
{
    bool saving = state == SAVE || state == HOLD_SAVE || state == WRITE_FILE;
    c64_uci_service_hold_end();
    if (!close_owned_channel(d, channel))
        return MT_UCI_FAILED;
    release_channel();
    load_ready = false;
    state = IDLE;
    prepared = 0;
    c64_uci_service_hold_end();
    binary(r, result);
    if (!saving && !verify) {
        r->status[1] = (uint8_t)address;
        r->status[2] = (uint8_t)(address >> 8);
        r->status_length = 3;
    }
    return MT_UCI_REPLY;
}
static mt_uci_result_t poll(void *context, mt_uci_reply_t *r)
{
    (void)context;
    mt_iec_dos_t *d = c64_iec_service_dos();
    if (!d)
        return binary(r, 5);
    if (state == READ_FILE) {
        size_t count = 65536U - (unsigned)start_address - snapshot_size;
        if (count > sizeof block) count = sizeof block;
        if (!count) { state = HOLD_LOAD; return MT_UCI_WAIT; }
        if (mt_iec_dos_peek_many(d, 0, snapshot + snapshot_size, count, &prepared, &last_window))
            return finish_memory(d, r, 0x80);
        if (mt_iec_dos_consume_many(d, 0, prepared))
            return finish_memory(d, r, 0x80);
        snapshot_size += prepared;
        if (!prepared || last_window || snapshot_size == 65536U - (unsigned)start_address)
            state = HOLD_LOAD;
        prepared = 0;
        return MT_UCI_WAIT;
    }
    if (state == HOLD_LOAD || state == HOLD_SAVE) {
        int held = c64_uci_service_hold_begin();
        if (held > 0)
            return MT_UCI_WAIT;
        if (held < 0)
            return finish_memory(d, r, state == HOLD_SAVE ? 2 : 0x80);
        state = state == HOLD_SAVE ? SAVE : LOAD;
        return MT_UCI_WAIT;
    }
    if (state == LOAD) {
        if (!prepared) {
            size_t count = snapshot_size - snapshot_cursor;
            if (count > sizeof block)
                count = sizeof block;
            prepared = count;
            memcpy(block, snapshot + snapshot_cursor, count);
            if (!prepared)
                return finish_memory(d, r, 0);
        }
        if (verify) {
            int rc = c64_uci_ram_read(address, memory, prepared);
            if (rc > 0)
                return MT_UCI_WAIT;
            if (rc < 0)
                return finish_memory(d, r, 0x80);
            if (memcmp(block, memory, prepared))
                return finish_memory(d, r, 0x80);
        } else if (c64_uci_ram_write(address, block, prepared))
            return finish_memory(d, r, 0x80);
        address = (uint16_t)(address + prepared);
        snapshot_cursor += prepared;
        prepared = 0;
        if (snapshot_cursor == snapshot_size || !address)
            return finish_memory(d, r, 0);
        return MT_UCI_WAIT;
    }
    if (state == SAVE) {
        if (address == end_address) {
            c64_uci_service_hold_end();
            snapshot_cursor = 0;
            state = WRITE_FILE;
            return MT_UCI_WAIT;
        }
        size_t count = (uint16_t)(end_address - address);
        if (count > sizeof block)
            count = sizeof block;
        size_t boundary = 65536U - (unsigned)address;
        if (count > boundary)
            count = boundary;
        int rc = c64_uci_ram_read(address, snapshot + snapshot_size, count);
        if (rc > 0)
            return MT_UCI_WAIT;
        if (rc < 0)
            return finish_memory(d, r, 2);
        snapshot_size += count;
        address = (uint16_t)(address + count);
        return MT_UCI_WAIT;
    }
    if (state == WRITE_FILE) {
        size_t count = snapshot_size - snapshot_cursor;
        if (count > sizeof block) count = sizeof block;
        int rc = mt_iec_dos_write(d, 1, snapshot + snapshot_cursor, count, count == 0);
        if (rc) {
            c64_iec_service_write_failure(rc);
            return finish_memory(d, r, 2);
        }
        snapshot_cursor += count;
        if (!count) return finish_memory(d, r, 0);
        return MT_UCI_WAIT;
    }
    return binary(r, 3);
}
static mt_uci_result_t abort_target(void *context, size_t consumed)
{
    (void)context;
    mt_iec_dos_t *d = c64_iec_service_cleanup_dos();
    int cancel = c64_uci_ram_cancel();
    if (cancel > 0)
        return MT_UCI_WAIT;
    if (cancel < 0)
        return MT_UCI_FAILED;
    c64_uci_service_hold_end();
    if (state == INPUT && d) {
        (void)commit_input(d, consumed);
        release_channel();
        state = IDLE;
    } else if (state != IDLE && d) {
        if (!close_owned_channel(d, channel))
            return MT_UCI_FAILED;
        release_channel();
        state = IDLE;
        load_ready = false;
    }
    /* LOAD_SU's channel remains open across its normal command completion. */
    c64_uci_service_hold_end();
    return MT_UCI_REPLY;
}
static void reset_target(void *context)
{
    if (abort_target(context, 0) != MT_UCI_REPLY)
        return;
    mt_iec_dos_t *d = c64_iec_service_cleanup_dos();
    if (load_ready && d) {
        if (!close_owned_channel(d, 0))
            return;
    }
    release_channel();
    load_ready = false;
    state = IDLE;
    prepared = 0;
    /* Partitions belong to the shared IEC drive, not to a UCI transaction. */
}
static mt_uci_result_t next(void *context, size_t consumed, mt_uci_reply_t *r)
{
    (void)context;
    mt_iec_dos_t *d = c64_iec_service_dos();
    if (!d || state != INPUT)
        return binary(r, 3);
    if (commit_input(d, consumed)) {
        release_channel();
        state = IDLE;
        return binary(r, 3);
    }
    return input_reply(d, r, 256, false);
}
static mt_uci_result_t begin(void *context, const uint8_t command[896], size_t length,
                             mt_uci_reply_t *r)
{
    (void)context;
    mt_iec_dos_t *d = c64_iec_service_dos();
    if (!d || !c64_iec_service_active())
        return binary(r, 5);
    mt_uci_reply_clear(r);
    if (length < 2)
        return binary(r, 6);
    unsigned op = command[1], ch = command[2] & 15U;
    if (op == 1) {
        mt_uci_reply_text(r, "SOFTWARE IEC TARGET V1.0", "00,OK");
        return MT_UCI_REPLY;
    }
    if (op == 0x10) {
        if (length < 8)
            return binary(r, 6);
        if (load_ready) {
            if (!close_owned_channel(d, 0))
                return MT_UCI_FAILED;
        }
        if (!acquire(0))
            return binary(r, 1);
        if (open_name(d, 0, command + 8, length - 8)) {
            release_channel();
            return binary(r, 1);
        }
        size_t n = 0;
        bool eof = false;
        if (mt_iec_dos_peek_many(d, 0, block, 2, &n, &eof) || n != 2) {
            if (!close_owned_channel(d, 0))
                return MT_UCI_FAILED;
            return binary(r, 1);
        }
        (void)mt_iec_dos_consume_many(d, 0, 2);
        start_address = (uint16_t)(block[0] | (unsigned)block[1] << 8);
        binary(r, 0);
        r->data[0] = block[0];
        r->data[1] = block[1];
        r->data_length = 2;
        if (!command[2])
            start_address = (uint16_t)(command[4] | (unsigned)command[5] << 8);
        load_ready = true;
        return MT_UCI_REPLY;
    }
    if (op == 0x11) {
        if (length < 4)
            return binary(r, 6);
        if (!load_ready || !leased)
            return binary(r, 1);
        verify = command[3] != 0;
        address = start_address;
        prepared = 0;
        snapshot_size = snapshot_cursor = 0;
        state = READ_FILE;
        return MT_UCI_WAIT;
    }
    if (op == 0x12) {
        if (length < 8)
            return binary(r, 6);
        if (!acquire(1))
            return binary(r, 2);
        if (open_name(d, 1, command + 8, length - 8)) {
            release_channel();
            return binary(r, 2);
        }
        address = (uint16_t)(command[4] | (unsigned)command[5] << 8);
        end_address = (uint16_t)(command[6] | (unsigned)command[7] << 8);
        uint8_t header[2] = {(uint8_t)address, (uint8_t)(address >> 8)};
        int rc = mt_iec_dos_write(d, 1, header, 2, false);
        state = HOLD_SAVE;
        snapshot_size = snapshot_cursor = 0;
        verify = false;
        if (rc) {
            c64_iec_service_write_failure(rc);
            return finish_memory(d, r, 2);
        }
        return MT_UCI_WAIT;
    }
    if (op == 0x13 || op == 0x14 || op == 0x15 || op == 0x16) {
        if (length < (op == 0x14 || op == 0x15 ? 3U : 4U))
            return binary(r, 6);
        if (!acquire(ch))
            return binary(r, 3);
        if (op == 0x15) {
            state = INPUT;
            return input_reply(d, r, 32, true);
        }
        int rc = 0;
        if (op == 0x14 || (op == 0x16 && (command[2] & 0xf0) == 0xe0)) {
            bool writing = ch < 15 && (d->channels[ch].kind == 2 ||
                                       (d->channels[ch].kind == 5 && d->channels[ch].writable));
            rc = mt_iec_dos_close(d, ch);
            if (writing && (rc == 23 || rc == 25 || rc == 72 || rc == 74))
                c64_iec_service_write_failure(rc);
            if (ch == 0)
                load_ready = false;
        } else if (op == 0x13 || (command[2] & 0xf0) == 0xf0) {
            rc = open_name(d, ch, command + 4, length - 4);
        } else if (ch == 15)
            rc = mt_iec_dos_command(d, command + 4, length - 4);
        else {
            bool writing = d->channels[ch].kind == 2 ||
                           (d->channels[ch].kind == 5 && d->channels[ch].writable);
            rc = mt_iec_dos_write(d, ch, command + 4, length - 4, true);
            if (writing && (rc == 23 || rc == 25 || rc == 72 || rc == 74))
                c64_iec_service_write_failure(rc);
        }
        release_channel();
        /* OPEN deliberately returns an empty status, as upstream; failures
         * are available through the shared channel15 DOS status. */
        if (op == 0x13)
            return MT_UCI_REPLY;
        (void)rc;
        mt_uci_reply_status(r, "00,OK");
        return MT_UCI_REPLY;
    }
    if (op == 0x20) {
        if (length < 6 || !command[2])
            return binary(r, 6);
        char value[896];
        if (!text(command + 3, length - 3, value, sizeof value))
            return binary(r, 6);
        char *colon = strchr(value, ':');
        if (!colon || colon == value)
            return binary(r, 6);
        char path[C64_UCI_PATH];
        if (!c64_uci_path(d->cwd, (const uint8_t *)colon + 1, strlen(colon + 1), path))
            return binary(r, 9);
        *colon = 0;
        return binary(r, mt_iec_dos_partition_add(d, command[2], path, value) ? 9 : 0);
    }
    if (op == 0x21) {
        if (length != 3 || !command[2])
            return binary(r, 6);
        return binary(r, mt_iec_dos_partition_remove(d, command[2]) ? 8 : 0);
    }
    if (op == 0x22) {
        if (length < 4)
            return binary(r, 6);
        char path[MT_IEC_PATH];
        size_t name_length = length - 3;
        if (name_length && !command[length - 1]) --name_length;
        int rc = mt_iec_dos_resolve_name(d, ch, command + 3, name_length, path);
        if (rc)
            return binary(r, rc == 77 ? 8 : rc == 33 || rc == 30 ? 7 : 9);
        binary(r, 0);
        const size_t root_length = strlen(d->root);
        const char *relative = path;
        if (!strncmp(path, d->root, root_length) &&
            (path[root_length] == '/' || path[root_length] == 0))
            relative = path[root_length] ? path + root_length : "/";
        r->data_length = strlen(relative);
        memcpy(r->data, relative, r->data_length);
        return MT_UCI_REPLY;
    }
    if (op == 0x23) {
        if (length < 3)
            return binary(r, 6);
        char name[MT_IEC_PATH], alias[17];
        uint8_t type = 0;
        if (!text(command + 2, length - 2, name, sizeof name))
            return binary(r, 7);
        char absolute[MT_IEC_PATH];
        const char *query = name;
        if (strchr(name, '/')) {
            if (!c64_uci_path(d->cwd, (const uint8_t *)name, strlen(name), absolute))
                return binary(r, 9);
            query = absolute;
        }
        int rc = mt_iec_dos_iec_name(d, query, alias, &type);
        if (rc)
            return binary(r, rc == 33 ? 7 : 9);
        binary(r, 0);
        r->data[0] = type;
        r->data_length = 1 + strlen(alias);
        memcpy(r->data + 1, alias, r->data_length - 1);
        return MT_UCI_REPLY;
    }
    return binary(r, 4);
}
mt_uci_target_t c64_uci_softiec_target(void)
{
    return (mt_uci_target_t){NULL, begin, poll, next, abort_target, reset_target};
}
