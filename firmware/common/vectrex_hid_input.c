/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_vectrex_hid_input.h"
#include "multitini_vectrex_interface.h"
#include <string.h>

void mt_vectrex_hid_input_init(mt_vectrex_hid_input_t *in,
    mt_vectrex_hid_emit_fn emit, void *context)
{
    if (in == NULL) return;
    memset(in, 0, sizeof(*in)); in->emit = emit; in->context = context;
    memset(in->player, 2, sizeof(in->player));
}

int16_t mt_vectrex_axis_normalize(int32_t value, int32_t low, int32_t high)
{
    int64_t result;
    if (high <= low) return 0;
    if (value <= low) return INT16_MIN;
    if (value >= high) return INT16_MAX;
    result = (((int64_t)value - low) * INT64_C(65535)) /
        ((int64_t)high - low) - INT64_C(32768);
    /* USB axes often have even ranges with two possible center codes. */
    if (result >= -256 && result <= 256) return 0;
    return (int16_t)result;
}

void mt_vectrex_hid_controls(const mt_vectrex_hid_input_t *in, int controls[2][6])
{
    uint8_t directions = 0U, keyboard = 0U;
    unsigned player = 0U, slot, button;
    if (in == NULL || controls == NULL) return;
    memset(controls, 0, sizeof(int) * 12U);
    for (slot = 0U; slot < MT_VECTREX_HID_SLOTS; ++slot) {
        directions |= in->keyboard_directions[slot];
        keyboard |= in->keyboard_buttons[slot];
        if (in->pad_active[slot] == 0U) continue;
        const unsigned target = in->player[slot] == 2U ? player++ : in->player[slot];
        if (target >= 2U) continue;
        controls[target][0] = in->pad_x[slot];
        controls[target][1] = in->pad_y[slot];
        for (button = 0U; button < 4U; ++button)
            controls[target][button + 2U] = (int)(((unsigned)in->pad_buttons[slot] >> button) & 1U);
    }
    if ((directions & 12U) != 0U)
        controls[0][0] = (directions & 12U) == 12U ? 0 :
            ((directions & 4U) != 0U ? INT16_MIN : INT16_MAX);
    if ((directions & 3U) != 0U)
        controls[0][1] = (directions & 3U) == 3U ? 0 :
            ((directions & 1U) != 0U ? INT16_MAX : INT16_MIN);
    for (button = 0U; button < 4U; ++button)
        controls[0][button + 2U] |= (int)(((unsigned)keyboard >> button) & 1U);
}

int mt_vectrex_hid_input_poll(mt_vectrex_hid_input_t *in)
{
    int controls[2][6];
    uint16_t values[5] = {0};
    unsigned player, button, index;
    if (in == NULL || in->emit == NULL) return -1;
    mt_vectrex_hid_controls(in, controls);
    for (player = 0U; player < 2U; ++player) {
        values[1U + player * 2U] = (uint16_t)controls[player][0];
        values[2U + player * 2U] = (uint16_t)controls[player][1];
        for (button = 0U; button < 4U; ++button)
            if (controls[player][button + 2U] != 0)
                values[0] |= (uint16_t)(1U << (player * 4U + button));
    }
    for (index = 0U; index < 5U; ++index) {
        const uint8_t mask = (uint8_t)(1U << index);
        if ((in->emitted_valid & mask) != 0U && in->emitted[index] == values[index])
            continue;
        if (in->emit(in->context, (uint16_t)(0x2000U | index), values[index]) != 0)
            return -1;
        in->emitted[index] = values[index];
        in->emitted_valid |= mask;
    }
    return 0;
}

int mt_vectrex_hid_keyboard_report(mt_vectrex_hid_input_t *in, uint8_t slot,
    uint8_t modifiers, const uint8_t *keys, size_t count)
{
    uint8_t buttons = 0U, directions = 0U;
    size_t i;
    (void)modifiers;
    if (in == NULL || slot >= MT_VECTREX_HID_SLOTS || (keys == NULL && count != 0U))
        return -1;
    for (i = 0U; i < count; ++i) {
        if (keys[i] > 0U && keys[i] <= 3U) return -2;
        switch (keys[i]) {
        case 0x1dU: buttons |= 1U; break; /* Z = 1 */
        case 0x1bU: buttons |= 2U; break; /* X = 2 */
        case 0x06U: buttons |= 4U; break; /* C = 3 */
        case 0x19U: buttons |= 8U; break; /* V = 4 */
        case 0x52U: directions |= 1U; break;
        case 0x51U: directions |= 2U; break;
        case 0x50U: directions |= 4U; break;
        case 0x4fU: directions |= 8U; break;
        default: break;
        }
    }
    in->keyboard_buttons[slot] = buttons;
    in->keyboard_directions[slot] = directions;
    return mt_vectrex_hid_input_poll(in);
}

int mt_vectrex_hid_gamepad_report(mt_vectrex_hid_input_t *in, uint8_t slot,
    uint8_t buttons, int16_t x, int16_t y)
{
    if (in == NULL || slot >= MT_VECTREX_HID_SLOTS) return -1;
    in->pad_active[slot] = 1U; in->pad_buttons[slot] = buttons & 15U;
    in->pad_x[slot] = x; in->pad_y[slot] = y;
    return mt_vectrex_hid_input_poll(in);
}

int mt_vectrex_hid_disconnect(mt_vectrex_hid_input_t *in, uint8_t slot)
{
    if (in == NULL || slot >= MT_VECTREX_HID_SLOTS) return -1;
    in->keyboard_buttons[slot] = 0U; in->keyboard_directions[slot] = 0U;
    in->pad_active[slot] = 0U;
    return mt_vectrex_hid_input_poll(in);
}

int mt_vectrex_hid_assigned_report(mt_vectrex_hid_input_t *in, uint8_t slot,
    uint8_t player, uint8_t buttons, int16_t x, int16_t y)
{
    if (in == NULL || slot >= MT_VECTREX_HID_SLOTS ||
        (player >= 2U && player != UINT8_MAX)) return -1;
    in->player[slot] = player;
    return mt_vectrex_hid_gamepad_report(in, slot, buttons, x, y);
}

void mt_vectrex_hid_reassert(mt_vectrex_hid_input_t *in)
{ if (in != NULL) in->emitted_valid = 0U; }

int mt_vectrex_hid_release_all(mt_vectrex_hid_input_t *in)
{
    if (in == NULL) return -1;
    memset(in->keyboard_buttons, 0, sizeof(in->keyboard_buttons));
    memset(in->keyboard_directions, 0, sizeof(in->keyboard_directions));
    memset(in->pad_active, 0, sizeof(in->pad_active));
    return mt_vectrex_hid_input_poll(in);
}

static int16_t read_le16(const uint8_t *p)
{
    uint16_t u = (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8U));
    return (int16_t)(u < 0x8000U ? (int32_t)u : (int32_t)u - INT32_C(65536));
}

int mt_vectrex_xinput_decode(const uint8_t *report, size_t size,
    uint8_t *buttons, int16_t *x, int16_t *y)
{
    if (report == NULL || buttons == NULL || x == NULL || y == NULL) return -1;
    if (size < 20U || report[0] != 0U || report[1] != 20U) return -2;
    *buttons = (uint8_t)(report[3] >> 4U); /* A/B/X/Y, no Select/Start alias */
    *x = read_le16(report + 6U); *y = read_le16(report + 8U);
    if ((report[2] & 12U) != 0U)
        *x = (report[2] & 12U) == 12U ? 0 :
            ((report[2] & 4U) != 0U ? INT16_MIN : INT16_MAX);
    if ((report[2] & 3U) != 0U)
        *y = (report[2] & 3U) == 3U ? 0 :
            ((report[2] & 1U) != 0U ? INT16_MAX : INT16_MIN);
    return 0;
}
