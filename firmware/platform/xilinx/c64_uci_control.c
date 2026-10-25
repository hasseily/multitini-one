/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_iec_service.h"
#include "c64_uci_geos.h"
#include "c64_uci_internal.h"
#include <stdio.h>
#include <string.h>

enum
{
    CONTROL_IDLE,
    CONTROL_ACTION,
    CONTROL_REU,
    CONTROL_DUMP_HOLD,
    CONTROL_DUMP_READ,
    CONTROL_DUMP_WRITE,
    CONTROL_GEOS
};
static unsigned state;
static uint32_t dump_offset;
static mt_sd_file_t *dump_file;
static uint8_t dump_data[65536];
static const char *failure;
static mt_uci_result_t answer(mt_uci_reply_t *r, const char *status)
{
    mt_uci_reply_clear(r);
    mt_uci_reply_status(r, status);
    return MT_UCI_REPLY;
}
static mt_uci_result_t dump_finish(mt_uci_reply_t *r)
{
    if (dump_file) {
        mt_sd_result_t s = mt_sd_file_close(dump_file);
        if (s.status == MT_SD_OK || mt_sd_file_tell(dump_file) < 0)
            dump_file = NULL;
        if (s.status != MT_SD_OK) {
            c64_uci_environment.fault = true;
            return MT_UCI_FAILED;
        }
    }
    c64_uci_service_hold_end();
    state = CONTROL_IDLE;
    return answer(r, failure ? failure : "00,OK");
}
static mt_uci_result_t poll(void *context, mt_uci_reply_t *r)
{
    (void)context;
    if (state == CONTROL_GEOS) {
        mt_uci_result_t result = c64_uci_geos_poll(r);
        if (result == MT_UCI_REPLY && !c64_uci_environment.fault)
            state = CONTROL_IDLE;
        return result;
    }
    if (state == CONTROL_ACTION) {
        int result = c64_uci_action_result();
        if (result > 0)
            return MT_UCI_WAIT;
        state = CONTROL_IDLE;
        return answer(r, result ? "87,INTERNAL ERROR" : NULL);
    }
    if (state == CONTROL_REU) {
        mt_uci_result_t result = c64_uci_named_reu_poll(r);
        if (result == MT_UCI_REPLY && !c64_uci_environment.fault)
            state = CONTROL_IDLE;
        return result;
    }
    if (state == CONTROL_DUMP_HOLD) {
        int held = c64_uci_service_hold_begin();
        if (held > 0)
            return MT_UCI_WAIT;
        if (held < 0) {
            failure = "87,INTERNAL ERROR";
            return dump_finish(r);
        }
        state = CONTROL_DUMP_READ;
        return MT_UCI_WAIT;
    }
    if (state == CONTROL_DUMP_READ) {
        int read = c64_uci_ram_read((uint16_t)dump_offset, dump_data + dump_offset, 256);
        if (read > 0)
            return MT_UCI_WAIT;
        if (read < 0) {
            failure = "87,INTERNAL ERROR";
            return dump_finish(r);
        }
        dump_offset += 256;
        if (dump_offset == sizeof dump_data) {
            c64_uci_service_hold_end();
            dump_offset = 0;
            state = CONTROL_DUMP_WRITE;
        }
        return MT_UCI_WAIT;
    }
    if (state == CONTROL_DUMP_WRITE) {
        mt_sd_result_t s = mt_sd_file_write(dump_file, dump_data + dump_offset, 4096);
        if (s.status != MT_SD_OK || s.size != 4096) {
            failure = c64_uci_sd_status(s);
            return dump_finish(r);
        }
        dump_offset += 4096;
        if (dump_offset == 65536)
            return dump_finish(r);
        return MT_UCI_WAIT;
    }
    return answer(r, "83,NOT IN DATA MODE");
}
static mt_uci_result_t abort_target(void *context, size_t consumed)
{
    (void)context;
    (void)consumed;
    if (state == CONTROL_GEOS) {
        mt_uci_result_t result = c64_uci_geos_abort();
        if (result != MT_UCI_REPLY)
            return result;
    }
    if (state == CONTROL_ACTION && !c64_uci_cancel_action())
        return MT_UCI_WAIT;
    if (state == CONTROL_REU) {
        mt_uci_result_t a = c64_uci_named_reu_abort();
        if (a != MT_UCI_REPLY)
            return a;
    }
    int pending = c64_uci_ram_cancel();
    if (pending > 0)
        return MT_UCI_WAIT;
    if (pending < 0)
        return MT_UCI_FAILED;
    c64_uci_service_hold_end();
    if (dump_file) {
        mt_sd_result_t s = mt_sd_file_close(dump_file);
        if (s.status == MT_SD_OK || mt_sd_file_tell(dump_file) < 0)
            dump_file = NULL;
        if (s.status != MT_SD_OK) {
            c64_uci_environment.fault = true;
            return MT_UCI_FAILED;
        }
    }
    c64_uci_service_hold_end();
    state = CONTROL_IDLE;
    return MT_UCI_REPLY;
}
static void reset_target(void *context)
{
    (void)abort_target(context, 0);
}
static mt_uci_result_t begin(void *context, const uint8_t command[896], size_t length,
                             mt_uci_reply_t *r)
{
    (void)context;
    state = CONTROL_IDLE;
    failure = NULL;
    c64_uci_callbacks_t *c = &c64_uci_environment.callbacks;
    switch (command[1]) {
    case 1:
        mt_uci_reply_text(r, "CONTROL TARGET V1.1", "00,OK");
        return MT_UCI_REPLY;
    case 5:
    case 6:
        if (!c64_uci_post(command[1] == 5 ? C64_UCI_ACTION_FREEZE : C64_UCI_ACTION_REBOOT, "", 0))
            return answer(r, "87,INTERNAL ERROR");
        state = CONTROL_ACTION;
        return MT_UCI_WAIT;
    case 8:
    case 9: {
        if (length < 5)
            return answer(r, "81,INVALID PARAMS");
        mt_uci_result_t result = c64_uci_named_reu_begin(command[1] == 9, r);
        if (result == MT_UCI_WAIT)
            state = CONTROL_REU;
        return result;
    }
    case 0x0f: {
        char path[C64_UCI_PATH];
        static const uint8_t default_name[] = "/Temp/c64_memory.bin";
        const uint8_t *name = length > 2 ? command + 2 : default_name;
        size_t count = length > 2 ? length - 2 : sizeof(default_name) - 1;
        if (!c64_uci_path(c64_uci_environment.home, name, count, path) ||
            c64_uci_protected(path, true))
            return answer(r, "98,FUNCTION PROHIBITED");
        mt_sd_result_t s = mt_sd_storage_ensure_directory(c64_uci_environment.temporary);
        if (s.status != MT_SD_OK)
            return answer(r, c64_uci_sd_status(s));
        s = mt_sd_file_open(path, "wb", &dump_file);
        if (s.status != MT_SD_OK)
            return answer(r, c64_uci_sd_status(s));
        dump_offset = 0;
        state = CONTROL_DUMP_HOLD;
        return MT_UCI_WAIT;
    }
    case 0x28: {
        unsigned sub = length > 2 ? command[2] : 0;
        if (sub == 0) {
            mt_uci_reply_text(r, "MULTITINI ONE", "00,OK");
            return MT_UCI_REPLY;
        }
        mt_uci_reply_clear(r);
        if ((sub == 1 || sub == 2) && c->sid_hwinfo) {
            r->data_length = c->sid_hwinfo(c->context, sub, r->data, sizeof r->data);
            mt_uci_reply_status(r, "00,OK");
            return MT_UCI_REPLY;
        }
        return answer(r, "81,INVALID PARAMS");
    }
    case 0x29:
        mt_uci_reply_clear(r);
        r->data[0] = 1;
        r->data[1] = 0;
        r->data[2] = 8;
        r->data[3] = 1;
        r->data_length = 4;
        if (c64_iec_service_active()) {
            r->data[0] = 2;
            r->data[4] = 15;
            r->data[5] = 9;
            r->data[6] = 1;
            r->data_length = 7;
        }
        mt_uci_reply_status(r, "00,OK");
        return MT_UCI_REPLY;
    case 0x30:
    case 0x32:
    case 0x33:
        return answer(r, "00,OK");
    case 0x34:
        mt_uci_reply_text(r, "on ", "00,OK");
        return MT_UCI_REPLY;
    case 0x35:
        mt_uci_reply_text(r, "off", "00,OK");
        return MT_UCI_REPLY;
    case 0x50: {
        char path[C64_UCI_PATH], log[512];
        static const uint8_t default_name[] = "/Temp/uci_config.cfg";
        const uint8_t *name = length > 2 ? command + 2 : default_name;
        size_t count = length > 2 ? length - 2 : sizeof(default_name) - 1;
        if (!c->apply_config || !c64_uci_path(c64_uci_environment.home, name, count, path) ||
            c64_uci_protected(path, false))
            return answer(r, "88,CANNOT OPEN CONFIG FILE");
        memset(log, 0, sizeof log);
        int result = c->apply_config(c->context, path, log, sizeof log);
        log[511] = 0;
        mt_uci_reply_text(r, log,
                          result == 0    ? "00,OK"
                          : result == 89 ? "89,CONFIG FILE HAD ERRORS"
                          : result == 88 ? "88,CANNOT OPEN CONFIG FILE"
                                         : "87,INTERNAL ERROR");
        return MT_UCI_REPLY;
    }
    // Commands tied to absent Ultimate hardware are recognized explicitly.
    case 0x11:
    case 0x40: {
        mt_uci_result_t result = c64_uci_geos_begin(command, length, c64_uci_environment.home, r);
        if (result == MT_UCI_WAIT)
            state = CONTROL_GEOS;
        return result;
    }
    case 3:
    case 0x20:
    case 0x31:
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
        return answer(r, "99,FUNCTION NOT IMPLEMENTED");
    default:
        return answer(r, "21,UNKNOWN COMMAND");
    }
}
mt_uci_target_t c64_uci_control_target(void)
{
    state = CONTROL_IDLE;
    dump_file = NULL;
    failure = NULL;
    return (mt_uci_target_t){NULL, begin, poll, NULL, abort_target, reset_target};
}
