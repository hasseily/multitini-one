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
    CONTROL_GEOS,
    CONTROL_PALETTE,
    CONTROL_ERASE_HOLD,
    CONTROL_ERASE_WAIT,
    CONTROL_REBOOT_HOLD
};
static unsigned state;
static uint32_t dump_offset;
static mt_sd_file_t *dump_file;
static uint8_t dump_data[65536];
static const char *failure;
static const uint8_t default_palette[48] = {
    0,0,0, 255,255,255, 129,51,56, 117,206,200,
    142,60,151, 86,172,77, 46,44,155, 237,241,113,
    142,80,41, 85,56,0, 196,108,113, 74,74,74,
    123,123,123, 169,255,159, 112,109,235, 178,178,178
};
static uint8_t palette[48], palette_pending[48];
static unsigned palette_index, palette_end;
static bool palette_sent;
static uint64_t palette_start;
static uint32_t erase_word;
static uint8_t erase_token;
static uint64_t erase_start;
static bool capability(uint32_t bit)
{
    return c64_uci_environment.available &&
        (c64_uci_read(MT_REG_ABI_VERSION) & 0xffff) >= MT_C64_UCI315_MIN_ABI_MINOR &&
        (c64_uci_read(MT_REG_C64_EXPANSION_CAPS) & bit) != 0;
}
static bool drive_a_power(void)
{
    return !capability(MT_C64_EXPANSION_CAPS_DRIVE_POWER) ||
        !(c64_uci_read(MT_REG_C64_PERIPHERAL_CONTROL) & MT_C64_PERIPHERAL_DRIVE_A_OFF);
}
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
    if (state == CONTROL_REBOOT_HOLD) {
        int held = c64_uci_service_hold_begin();
        if (held > 0) return MT_UCI_WAIT;
        if (held < 0) { state = CONTROL_IDLE; return answer(r, "87,INTERNAL ERROR"); }
        /* Ultimate start_cartridge(NULL) clears the RAM CBM80 signature before
         * asserting reset. Our RAM writer is also reset by CORE_RESET, so this
         * write must complete while the paused machine still accepts DMA. */
        const uint8_t zero = 0;
        if (c64_uci_ram_write(0x8005, &zero, 1) != 0) return MT_UCI_FAILED;
        if (!c64_uci_post(C64_UCI_ACTION_REBOOT, "", 0)) {
            c64_uci_service_hold_end(); state = CONTROL_IDLE;
            return answer(r, "87,INTERNAL ERROR");
        }
        state = CONTROL_ACTION;
        return MT_UCI_WAIT;
    }
    if (state == CONTROL_ERASE_HOLD) {
        int held = c64_uci_service_hold_begin();
        if (held > 0) return MT_UCI_WAIT;
        if (held < 0) { state = CONTROL_IDLE; return answer(r, "87,INTERNAL ERROR"); }
        const uint32_t s = c64_uci_read(MT_REG_C64_CART_ERASE_STATUS);
        if (!(s & MT_C64_CART_ERASE_VALID) || (s & MT_C64_CART_ERASE_BUSY)) {
            c64_uci_service_hold_end(); state = CONTROL_IDLE;
            return answer(r, "87,INTERNAL ERROR");
        }
        erase_token = (uint8_t)((s + 1) & 15);
        c64_uci_write(MT_REG_C64_CART_ERASE, erase_word);
        erase_start = c64_uci_time();
        state = CONTROL_ERASE_WAIT;
        return MT_UCI_WAIT;
    }
    if (state == CONTROL_ERASE_WAIT) {
        const uint32_t s = c64_uci_read(MT_REG_C64_CART_ERASE_STATUS);
        if ((s & MT_C64_CART_ERASE_BUSY) || (s & 15) != erase_token) {
            if (c64_uci_time() - erase_start < 1000000) return MT_UCI_WAIT;
            c64_uci_service_hold_fault();
            return MT_UCI_FAILED;
        }
        if (s & MT_C64_CART_ERASE_FAULT) {
            c64_uci_service_hold_fault();
            return MT_UCI_FAILED;
        }
        state = CONTROL_IDLE;
        c64_uci_service_hold_end();
        return answer(r, "00,OK");
    }
    if (state == CONTROL_PALETTE) {
        if (c64_uci_read(MT_REG_C64_PALETTE_STATUS) & MT_C64_PALETTE_STATUS_BUSY) {
            if (c64_uci_time() - palette_start < 1000000)
                return MT_UCI_WAIT;
            state = CONTROL_IDLE;
            return answer(r, "87,INTERNAL ERROR");
        }
        if (palette_sent) {
            memcpy(palette + palette_index * 3, palette_pending + palette_index * 3, 3);
            palette_index++;
            palette_sent = false;
        }
        if (palette_index == palette_end) {
            state = CONTROL_IDLE;
            return answer(r, "00,OK");
        }
        const uint8_t *rgb = palette_pending + palette_index * 3;
        c64_uci_write(MT_REG_C64_PALETTE_DATA, (palette_index << 24) |
            ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | rgb[2]);
        palette_sent = true;
        palette_start = c64_uci_time();
        return MT_UCI_WAIT;
    }
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
    if (state == CONTROL_ERASE_WAIT) {
        const uint32_t s = c64_uci_read(MT_REG_C64_CART_ERASE_STATUS);
        if ((s & MT_C64_CART_ERASE_BUSY) || (s & 15) != erase_token)
            return MT_UCI_WAIT;
        if (s & MT_C64_CART_ERASE_FAULT) {
            c64_uci_service_hold_fault();
            return MT_UCI_FAILED;
        }
    }
    if (palette_sent) {
        if (c64_uci_read(MT_REG_C64_PALETTE_STATUS) & MT_C64_PALETTE_STATUS_BUSY)
            return MT_UCI_WAIT;
        memcpy(palette + palette_index * 3, palette_pending + palette_index * 3, 3);
        palette_sent = false;
    }
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
void c64_uci_control_prepare_reload(void)
{
    /* Called only after service verifies core reset and drained external
     * masters. PL replacement discards these mailboxes; SD-owning states must
     * still go through their ordinary abort/close path. */
    if (state == CONTROL_ERASE_HOLD || state == CONTROL_ERASE_WAIT || state == CONTROL_PALETTE)
        state = CONTROL_IDLE;
    palette_sent = false;
}
static mt_uci_result_t begin(void *context, const uint8_t command[896], size_t length,
                             mt_uci_reply_t *r)
{
    (void)context;
    /* A timed-out mailbox write still belongs to this target until the pixel
     * clock acknowledges it. Reconcile it before another command can replace
     * its saved RGB value, including after an error reply has been consumed. */
    if (palette_sent &&
        !(c64_uci_read(MT_REG_C64_PALETTE_STATUS) & MT_C64_PALETTE_STATUS_BUSY)) {
        memcpy(palette + palette_index * 3, palette_pending + palette_index * 3, 3);
        palette_sent = false;
    }
    state = CONTROL_IDLE;
    failure = NULL;
    c64_uci_callbacks_t *c = &c64_uci_environment.callbacks;
    switch (command[1]) {
    case 1:
        mt_uci_reply_text(r, "CONTROL TARGET V1.1", "00,OK");
        return MT_UCI_REPLY;
    case 3:
        /* There is no tape-capture session on this board. Ultimate's finish
         * operation is also a successful no-op when no recorder is active. */
        return answer(r, "00,OK");
    case 5:
        if (!c64_uci_post(C64_UCI_ACTION_FREEZE, "", 0))
            return answer(r, "87,INTERNAL ERROR");
        state = CONTROL_ACTION;
        return MT_UCI_WAIT;
    case 6:
        state = CONTROL_REBOOT_HOLD;
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
        r->data[2] = length > 2 && command[2] == 0 ? 8 : c64_uci_service_drive_address();
        r->data[3] = drive_a_power() ? 1 : 0;
        r->data_length = 4;
        if (c64_iec_service_active()) {
            r->data[0] = 2;
            r->data[4] = 15;
            r->data[5] = (uint8_t)c64_iec_service_device_number();
            r->data[6] = 1;
            r->data_length = 7;
        }
        mt_uci_reply_status(r, "00,OK");
        return MT_UCI_REPLY;
    case 0x30:
    case 0x31:
        if (!capability(MT_C64_EXPANSION_CAPS_DRIVE_POWER))
            return answer(r, command[1] == 0x30 ? "00,OK" : "99,FUNCTION NOT IMPLEMENTED");
        c64_uci_write(MT_REG_C64_PERIPHERAL_CONTROL,
            (c64_uci_read(MT_REG_C64_PERIPHERAL_CONTROL) & ~MT_C64_PERIPHERAL_DRIVE_A_OFF) |
            (command[1] == 0x31 ? MT_C64_PERIPHERAL_DRIVE_A_OFF : 0));
        return answer(r, "00,OK");
    case 0x32:
    case 0x33:
        return answer(r, "00,OK");
    case 0x34:
        mt_uci_reply_text(r, drive_a_power() ? "on " : "off", "00,OK");
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
    case 0x20:
        if (length < 5 || command[2] != 0)
            return answer(r, "81,INVALID PARAMS");
        if (!capability(MT_C64_EXPANSION_CAPS_CART_ERASE))
            return answer(r, "99,FUNCTION NOT IMPLEMENTED");
        erase_word = (command[3] & 0x38) | ((command[4] & 0x20) ? 0x40 : 0);
        state = CONTROL_ERASE_HOLD;
        return MT_UCI_WAIT;
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54: {
        if (!capability(MT_C64_EXPANSION_CAPS_PALETTE))
            return answer(r, "99,FUNCTION NOT IMPLEMENTED");
        if (palette_sent)
            return answer(r, "87,INTERNAL ERROR");
        const unsigned op = command[1];
        if (((op == 0x51 || op == 0x54) && length != 2) ||
            (op == 0x52 && length != 50) ||
            (op == 0x53 && (length != 6 || command[2] >= 16)))
            return answer(r, "81,INVALID PARAMS");
        if (op == 0x51) {
            mt_uci_reply_clear(r);
            memcpy(r->data, palette, sizeof palette);
            r->data_length = sizeof palette;
            mt_uci_reply_status(r, "00,OK");
            return MT_UCI_REPLY;
        }
        memcpy(palette_pending, palette, sizeof palette);
        palette_index = op == 0x53 ? command[2] : 0;
        palette_end = op == 0x53 ? palette_index + 1 : 16;
        memcpy(palette_pending + palette_index * 3,
            op == 0x54 ? default_palette : command + (op == 0x53 ? 3 : 2),
            (palette_end - palette_index) * 3);
        palette_sent = false;
        palette_start = c64_uci_time();
        state = CONTROL_PALETTE;
        return MT_UCI_WAIT;
    }
    default:
        return answer(r, "21,UNKNOWN COMMAND");
    }
}
mt_uci_target_t c64_uci_control_target(void)
{
    state = CONTROL_IDLE;
    dump_file = NULL;
    failure = NULL;
    memcpy(palette, default_palette, sizeof palette);
    palette_sent = false;
    return (mt_uci_target_t){NULL, begin, poll, NULL, abort_target, reset_target};
}
