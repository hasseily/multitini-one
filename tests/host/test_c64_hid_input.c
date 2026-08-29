/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_hid_input.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t code[64];
    uint16_t value[64];
    size_t count;
    int fail_once;
} event_log_t;

static int record_event(void *context, uint16_t code, uint16_t value)
{
    event_log_t *const log = context;
    if (log->fail_once != 0) {
        log->fail_once = 0;
        return -1;
    }
    assert(log->count < 64U);
    log->code[log->count] = code;
    log->value[log->count] = value;
    ++log->count;
    return 0;
}

static void clear_log(event_log_t *log)
{
    log->count = 0U;
}

int main(void)
{
    mt_c64_hid_input_t input;
    event_log_t log = {0};
    const uint8_t key_a[6] = {0x04U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t key_up[6] = {0x52U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t rollover[6] = {0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U};
    const uint8_t no_keys[6] = {0};

    mt_c64_hid_input_init(&input, record_event, &log);
    assert(input.joystick_port == 2U);

    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_a,
                                      sizeof(key_a)) == 0);
    assert(log.count == 1U && log.code[0] == UINT16_C(0x001C) &&
           log.value[0] == UINT16_C(1));
    assert(mt_c64_hid_keyboard_report(&input, 1U, 0U, key_a,
                                      sizeof(key_a)) == 0);
    assert(log.count == 1U);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, no_keys,
                                      sizeof(no_keys)) == 0);
    assert(log.count == 1U);
    assert(mt_c64_hid_disconnect(&input, 1U) == 0);
    assert(log.count == 2U && log.code[1] == UINT16_C(0x001C) &&
           log.value[1] == UINT16_C(0));

    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0x02U, key_a,
                                      sizeof(key_a)) == 0);
    assert(log.count == 2U);
    assert(log.code[0] == UINT16_C(0x0012) && log.value[0] == UINT16_C(1));
    assert(log.code[1] == UINT16_C(0x001C) && log.value[1] == UINT16_C(1));
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, rollover,
                                      sizeof(rollover)) == -2);
    assert(log.count == 2U);
    assert(mt_c64_hid_disconnect(&input, 0U) == 0);
    assert(log.count == 4U);

    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_up,
                                      sizeof(key_up)) == 0);
    assert(log.count == 1U && log.code[0] == UINT16_C(0x0075) &&
           log.value[0] == UINT16_C(3));
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, no_keys,
                                      sizeof(no_keys)) == 0);
    assert(log.count == 2U && log.value[1] == UINT16_C(2));

    clear_log(&log);
    assert(mt_c64_hid_joystick_report(&input, 2U, 0x51U) == 0);
    assert(log.count == 3U);
    assert(log.code[0] == UINT16_C(0x0200) && log.value[0] == 1U);
    assert(log.code[1] == UINT16_C(0x0204) && log.value[1] == 1U);
    assert(log.code[2] == UINT16_C(0x0206) && log.value[2] == 1U);
    assert(mt_c64_hid_set_joystick_port(&input, 1U) == 0);
    assert(log.count == 9U);
    assert(log.code[3] == UINT16_C(0x0200) && log.value[3] == 0U);
    assert(log.code[6] == UINT16_C(0x0100) && log.value[6] == 1U);
    assert(mt_c64_hid_disconnect(&input, 2U) == 0);
    assert(log.count == 12U);

    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_a,
                                      sizeof(key_a)) == 0);
    assert(mt_c64_hid_joystick_report(&input, 0U, 0x08U) == 0);
    clear_log(&log);
    mt_c64_hid_reassert(&input);
    assert(mt_c64_hid_input_poll(&input) == 0);
    assert(log.count == 2U);
    assert(log.code[0] == UINT16_C(0x001C));
    assert(log.code[1] == UINT16_C(0x0103));

    assert(mt_c64_hid_release_all(&input) == 0);
    clear_log(&log);
    log.fail_once = 1;
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_a,
                                      sizeof(key_a)) == -1);
    assert(log.count == 0U);
    assert(mt_c64_hid_input_poll(&input) == 0);
    assert(log.count == 1U && log.code[0] == UINT16_C(0x001C));

    return 0;
}
