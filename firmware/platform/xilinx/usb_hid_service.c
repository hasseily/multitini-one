/* SPDX-License-Identifier: GPL-3.0-only */
/* C64 USB keyboard, HID gamepad, and XInput adapter on USB1. */
#include "usb_hid_service.h"

#include "cherryusb_platform.h"
#include "multitini_c64_hid_input.h"
#include "multitini_c64_xinput.h"
#include "usb_config.h"
#include "usbh_core.h"
#include "usbh_hid.h"
#include "usbh_xbox.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "xil_printf.h"
#include "xparameters.h"
#include "xiltimer.h"

#ifdef XPAR_XUSBPS_1_BASEADDR
#define MT_USB1_BASE XPAR_XUSBPS_1_BASEADDR
#else
#define MT_USB1_BASE UINT32_C(0xE0003000)
#endif

#define MT_HID_REPORT_BYTES 64U
#define MT_HID_DESCRIPTOR_BYTES CONFIG_USBHOST_REQUEST_BUFFER_LEN
#define MT_HID_RETRY_TICKS ((XTime)(COUNTS_PER_SECOND / UINT64_C(20)))
#define MT_INPUT_COMMAND_POLLS UINT32_C(10000)
#define MT_XINPUT_PACKET_BYTES 64U
#define MT_XINPUT_SLOT_BASE CONFIG_USBHOST_MAX_HID_CLASS

#if (MT_XINPUT_PACKET_BYTES % CONFIG_USB_ALIGN_SIZE) != 0
#error XInput DMA buffers must span whole cache lines
#endif

#if (CONFIG_USBHOST_MAX_HID_CLASS + CONFIG_USBHOST_MAX_XBOX_CLASS) > \
    MT_C64_HID_SLOT_COUNT
#error Multitini input slots do not cover all USB class instances
#endif

typedef struct {
    struct usbh_hid *hid;
    struct usbh_hid_report_info report_info;
    XTime retry_started;
    uint32_t reports;
    uint32_t submit_errors;
    uint32_t transfer_errors;
    int last_error;
    uint8_t active;
    uint8_t keyboard;
    uint8_t gamepad;
    uint8_t gamepad_state;
    uint8_t report_pending;
    uint8_t retry_armed;
} mt_hid_slot_t;

typedef struct {
    struct usbh_xbox *xbox;
    XTime retry_started;
    uint32_t reports;
    uint32_t submit_errors;
    uint32_t transfer_errors;
    int last_error;
    uint8_t active;
    uint8_t report_pending;
    uint8_t retry_armed;
} mt_xinput_slot_t;

static mt_hid_slot_t g_slots[MT_C64_HID_SLOT_COUNT];
static uint8_t g_reports[MT_C64_HID_SLOT_COUNT][MT_HID_REPORT_BYTES]
    __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static uint8_t g_descriptors[MT_C64_HID_SLOT_COUNT][MT_HID_DESCRIPTOR_BYTES]
    __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static mt_xinput_slot_t g_xinput_slots[CONFIG_USBHOST_MAX_XBOX_CLASS];
static uint8_t
    g_xinput_reports[CONFIG_USBHOST_MAX_XBOX_CLASS][MT_XINPUT_PACKET_BYTES]
    __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static uint8_t
    g_xinput_control[CONFIG_USBHOST_MAX_XBOX_CLASS]
                    [MT_XINPUT_PACKET_BYTES]
    __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static mt_c64_hid_input_t g_c64_input;
static uint32_t g_reports_total;
static uint32_t g_submit_errors;
static uint32_t g_transfer_errors;
static uint32_t g_input_errors;
static int g_last_error;
static uint8_t g_started;
static uint8_t g_power_shutdown_required;
static uint8_t g_core_ready;

static int emit_c64_input(void *context, uint16_t code, uint16_t value)
{
    mt_core_link_t *const link = context;
    mt_link_result_t result;

    if (link == NULL || g_core_ready == 0U) return -1;
    result = mt_core_link_input_event(link, code, value,
                                      MT_INPUT_COMMAND_POLLS, NULL, NULL);
    if (result != MT_LINK_OK) {
        ++g_input_errors;
        g_last_error = -(int)result;
        return -1;
    }
    return 0;
}

static mt_hid_slot_t *slot_for_hid(struct usbh_hid *hid)
{
    if (hid == NULL || hid->minor >= CONFIG_USBHOST_MAX_HID_CLASS) return NULL;
    return &g_slots[hid->minor];
}

static mt_xinput_slot_t *slot_for_xinput(struct usbh_xbox *xbox)
{
    if (xbox == NULL || xbox->minor >= CONFIG_USBHOST_MAX_XBOX_CLASS) {
        return NULL;
    }
    return &g_xinput_slots[xbox->minor];
}

static uint8_t xinput_aggregate_slot(const mt_xinput_slot_t *slot)
{
    return (uint8_t)(MT_XINPUT_SLOT_BASE +
                     (uint8_t)(slot - g_xinput_slots));
}

static void slot_clear(mt_hid_slot_t *slot)
{
    if (slot == NULL) return;
    memset(slot, 0, sizeof(*slot));
}

static bool report_id_matches(const struct usbh_hid_report_item *item,
                              const uint8_t *report, uint32_t length)
{
    if (item->attribute.report_id == 0U) return true;
    return report != NULL && length > 0U &&
           report[0] == item->attribute.report_id;
}

static bool extract_bits(const struct usbh_hid_report_item *item,
                         const uint8_t *report, uint32_t length,
                         uint32_t field, uint32_t *value)
{
    uint32_t bit_offset;
    uint32_t bit_count;
    uint32_t result = 0U;

    if (item == NULL || report == NULL || value == NULL ||
        item->attribute.report_size == 0U ||
        item->attribute.report_size > 32U ||
        field >= item->attribute.report_count ||
        !report_id_matches(item, report, length)) {
        return false;
    }
    bit_count = item->attribute.report_size;
    bit_offset = item->report_bit_offset + field * bit_count;
    if (item->attribute.report_id != 0U) bit_offset += 8U;
    if (bit_offset + bit_count > length * 8U) return false;

    for (uint32_t bit = 0U; bit < bit_count; ++bit) {
        const uint32_t source = bit_offset + bit;
        result |= (uint32_t)((report[source / 8U] >> (source % 8U)) & 1U)
                  << bit;
    }
    *value = result;
    return true;
}

static int32_t sign_extend(uint32_t value, uint8_t bits)
{
    uint32_t sign;
    if (bits == 0U || bits >= 32U) return (int32_t)value;
    sign = UINT32_C(1) << (bits - 1U);
    if ((value & sign) != 0U) value |= ~((sign << 1U) - 1U);
    return (int32_t)value;
}

static int32_t item_value(const struct usbh_hid_report_item *item,
                          uint32_t raw)
{
    if (item->attribute.logical_min < 0 ||
        item->attribute.logical_min > item->attribute.logical_max ||
        (item->report_flags & HID_MAINITEM_RELATIVE) != 0U) {
        return sign_extend(raw, item->attribute.report_size);
    }
    return (int32_t)raw;
}

static void item_bounds(const struct usbh_hid_report_item *item,
                        int32_t *minimum, int32_t *maximum)
{
    *minimum = item->attribute.logical_min;
    *maximum = item->attribute.logical_max;
    if (*minimum > *maximum) {
        *minimum = sign_extend((uint32_t)*minimum,
                               item->attribute.report_size);
    }
}

static uint16_t usage_for_field(const struct usbh_hid_report_item *item,
                                uint32_t field)
{
    const uint32_t usage = (uint32_t)item->attribute.usage_min + field;
    if (item->attribute.usage_min <= item->attribute.usage_max &&
        usage <= item->attribute.usage_max) {
        return (uint16_t)usage;
    }
    return item->attribute.usage_min;
}

static int axis_direction(int32_t value, int32_t minimum, int32_t maximum)
{
    int32_t span;
    if (maximum <= minimum) return 0;
    span = maximum - minimum;
    if (value < minimum + span / 3) return -1;
    if (value > minimum + (span * 2) / 3) return 1;
    return 0;
}

static uint8_t hat_state(int32_t value, int32_t minimum)
{
    const int32_t direction = value - minimum;
    switch (direction) {
    case 0: return 0x01U;
    case 1: return 0x09U;
    case 2: return 0x08U;
    case 3: return 0x0AU;
    case 4: return 0x02U;
    case 5: return 0x06U;
    case 6: return 0x04U;
    case 7: return 0x05U;
    default: return 0U;
    }
}

static bool descriptor_has_gamepad(const mt_hid_slot_t *slot)
{
    uint8_t axes = 0U;
    bool hat = false;

    for (uint32_t index = 0U;
         index < slot->report_info.report_item_count; ++index) {
        const struct usbh_hid_report_item *const item =
            &slot->report_info.report_items[index];
        if (item->report_type != HID_REPORT_INPUT ||
            item->attribute.usage_page !=
                HID_USAGE_PAGE_GENERIC_DESKTOP_CONTROLS ||
            (item->report_flags & HID_MAINITEM_CONSTANT) != 0U ||
            (item->report_flags & HID_MAINITEM_RELATIVE) != 0U) {
            continue;
        }
        for (uint32_t field = 0U;
             field < item->attribute.report_count; ++field) {
            const uint16_t usage = usage_for_field(item, field);
            if (usage == HID_DESKTOP_USAGE_X ||
                usage == HID_DESKTOP_USAGE_RX) axes |= 1U;
            else if (usage == HID_DESKTOP_USAGE_Y ||
                     usage == HID_DESKTOP_USAGE_RY) axes |= 2U;
            else if (usage == HID_DESKTOP_USAGE_HATSWITCH ||
                     (usage >= HID_DESKTOP_USAGE_DPAD_UP &&
                      usage <= HID_DESKTOP_USAGE_DPAD_LEFT)) hat = true;
        }
    }
    return axes == 3U || hat;
}

static void process_gamepad_report(mt_hid_slot_t *slot,
                                   const uint8_t *report, uint32_t length)
{
    uint8_t next = slot->gamepad_state;
    uint8_t direction_touched = 0U;
    uint8_t direction_value = 0U;
    uint8_t button_touched = 0U;
    uint8_t button_value = 0U;

    for (uint32_t index = 0U;
         index < slot->report_info.report_item_count; ++index) {
        const struct usbh_hid_report_item *const item =
            &slot->report_info.report_items[index];
        if (item->report_type != HID_REPORT_INPUT ||
            (item->report_flags & HID_MAINITEM_CONSTANT) != 0U ||
            !report_id_matches(item, report, length)) {
            continue;
        }
        for (uint32_t field = 0U;
             field < item->attribute.report_count; ++field) {
            uint32_t raw = 0U;
            uint16_t usage;
            if (!extract_bits(item, report, length, field, &raw)) continue;
            usage = usage_for_field(item, field);

            if (item->attribute.usage_page == HID_USAGE_PAGE_BUTTON &&
                usage >= 1U && usage <= 3U) {
                const uint8_t bit = (uint8_t)(usage + 3U);
                const uint8_t mask = (uint8_t)(1U << bit);
                button_touched |= mask;
                if (raw != 0U) button_value |= mask;
            } else if (item->attribute.usage_page ==
                           HID_USAGE_PAGE_GENERIC_DESKTOP_CONTROLS &&
                       (item->report_flags & HID_MAINITEM_VARIABLE) != 0U &&
                       (item->report_flags & HID_MAINITEM_RELATIVE) == 0U) {
                int32_t minimum;
                int32_t maximum;
                const int32_t value = item_value(item, raw);
                item_bounds(item, &minimum, &maximum);
                if (usage == HID_DESKTOP_USAGE_X ||
                    usage == HID_DESKTOP_USAGE_RX) {
                    const int direction = axis_direction(value, minimum,
                                                         maximum);
                    direction_touched |= 0x0CU;
                    if (direction < 0) direction_value |= 0x04U;
                    else if (direction > 0) direction_value |= 0x08U;
                } else if (usage == HID_DESKTOP_USAGE_Y ||
                           usage == HID_DESKTOP_USAGE_RY) {
                    const int direction = axis_direction(value, minimum,
                                                         maximum);
                    direction_touched |= 0x03U;
                    if (direction < 0) direction_value |= 0x01U;
                    else if (direction > 0) direction_value |= 0x02U;
                } else if (usage == HID_DESKTOP_USAGE_HATSWITCH) {
                    direction_touched |= 0x0FU;
                    direction_value |= hat_state(value, minimum);
                } else if (usage >= HID_DESKTOP_USAGE_DPAD_UP &&
                           usage <= HID_DESKTOP_USAGE_DPAD_LEFT) {
                    static const uint8_t dpad_bits[4] = {
                        0x01U, 0x02U, 0x08U, 0x04U,
                    };
                    const uint8_t mask =
                        dpad_bits[usage - HID_DESKTOP_USAGE_DPAD_UP];
                    direction_touched |= mask;
                    if (raw != 0U) direction_value |= mask;
                }
            }
        }
    }
    next = (uint8_t)((next & (uint8_t)~direction_touched) |
                     direction_value);
    next = (uint8_t)((next & (uint8_t)~button_touched) | button_value);
    slot->gamepad_state = next;
    (void)mt_c64_hid_joystick_report(&g_c64_input,
                                     (uint8_t)(slot - g_slots), next);
}

static uint32_t report_length(const mt_hid_slot_t *slot)
{
    uint32_t length = MT_HID_REPORT_BYTES;
    if (slot != NULL && slot->hid != NULL && slot->hid->intin != NULL) {
        const uint32_t packet =
            USB_GET_MAXPACKETSIZE(slot->hid->intin->wMaxPacketSize);
        if (packet != 0U && packet < length) length = packet;
    }
    return length;
}

static void submit_report(mt_hid_slot_t *slot);

static void report_complete(void *argument, int bytes)
{
    mt_hid_slot_t *const slot = argument;
    uint8_t index;

    if (slot == NULL) return;
    index = (uint8_t)(slot - g_slots);
    slot->report_pending = 0U;
    if (slot->active == 0U || slot->hid == NULL) return;

    if (bytes > 0) {
        slot->last_error = 0;
        ++slot->reports;
        ++g_reports_total;
        if (slot->keyboard != 0U && bytes >= 8) {
            const struct usb_hid_kbd_report *const keyboard =
                (const struct usb_hid_kbd_report *)g_reports[index];
            (void)mt_c64_hid_keyboard_report(
                &g_c64_input, index, keyboard->modifier, keyboard->key,
                sizeof(keyboard->key));
        } else if (slot->gamepad != 0U) {
            process_gamepad_report(slot, g_reports[index], (uint32_t)bytes);
        }
    } else if (bytes < 0) {
        ++slot->transfer_errors;
        ++g_transfer_errors;
        slot->last_error = bytes;
        g_last_error = bytes;
    }
    submit_report(slot);
}

static void submit_report(mt_hid_slot_t *slot)
{
    uint32_t length;
    int result;
    uint8_t index;

    if (slot == NULL || slot->active == 0U || slot->hid == NULL ||
        slot->hid->hport == NULL || slot->hid->intin == NULL) return;

    index = (uint8_t)(slot - g_slots);
    length = report_length(slot);
    memset(g_reports[index], 0, MT_HID_REPORT_BYTES);
    usbh_int_urb_fill(&slot->hid->intin_urb, slot->hid->hport,
                      slot->hid->intin, g_reports[index], length, 0U,
                      report_complete, slot);
    result = usbh_submit_urb(&slot->hid->intin_urb);
    if (result == 0) {
        slot->report_pending = 1U;
        slot->retry_armed = 0U;
        return;
    }
    if (result == -USB_ERR_NOMEM && slot->hid->intin_urb.hcpriv == NULL) {
        slot->hid->intin_urb.errorcode = 0;
    }
    XTime_GetTime(&slot->retry_started);
    slot->retry_armed = 1U;
    ++slot->submit_errors;
    ++g_submit_errors;
    slot->last_error = result;
    g_last_error = result;
}

static uint32_t xinput_report_length(const mt_xinput_slot_t *slot)
{
    uint32_t length;

    if (slot == NULL || slot->xbox == NULL || slot->xbox->intin == NULL) {
        return 0U;
    }
    length = USB_GET_MAXPACKETSIZE(slot->xbox->intin->wMaxPacketSize);
    if (length > MT_XINPUT_PACKET_BYTES) length = MT_XINPUT_PACKET_BYTES;
    return length;
}

static void submit_xinput_report(mt_xinput_slot_t *slot);

static void xinput_report_complete(void *argument, int bytes)
{
    mt_xinput_slot_t *const slot = argument;
    const uint8_t index = slot != NULL ?
        (uint8_t)(slot - g_xinput_slots) : 0U;

    if (slot == NULL) return;
    slot->report_pending = 0U;
    if (slot->active == 0U || slot->xbox == NULL) return;
    if (bytes == -USB_ERR_SHUTDOWN || slot->xbox->hport == NULL ||
        !slot->xbox->hport->connected) {
        return;
    }

    if (bytes > 0) {
        uint8_t state;
        int decode_result;
        slot->last_error = 0;
        ++slot->reports;
        ++g_reports_total;
        decode_result = mt_c64_xinput_decode(
            g_xinput_reports[index], (size_t)bytes, &state);
        if (decode_result == 0) {
            (void)mt_c64_hid_joystick_report(
                &g_c64_input, xinput_aggregate_slot(slot), state);
        } else {
            ++g_input_errors;
            slot->last_error = decode_result;
            g_last_error = decode_result;
        }
    } else if (bytes < 0) {
        ++slot->transfer_errors;
        ++g_transfer_errors;
        slot->last_error = bytes;
        g_last_error = bytes;
    }
    submit_xinput_report(slot);
}

static void submit_xinput_report(mt_xinput_slot_t *slot)
{
    uint32_t length;
    uint8_t index;
    int result;

    if (slot == NULL || slot->active == 0U || slot->xbox == NULL ||
        slot->xbox->hport == NULL || slot->xbox->intin == NULL) {
        return;
    }

    index = (uint8_t)(slot - g_xinput_slots);
    length = xinput_report_length(slot);
    if (length < MT_C64_XINPUT_REPORT_BYTES) {
        slot->last_error = -USB_ERR_RANGE;
        g_last_error = slot->last_error;
        return;
    }
    memset(g_xinput_reports[index], 0, MT_XINPUT_PACKET_BYTES);
    usbh_int_urb_fill(&slot->xbox->intin_urb, slot->xbox->hport,
                      slot->xbox->intin, g_xinput_reports[index], length, 0U,
                      xinput_report_complete, slot);
    result = usbh_submit_urb(&slot->xbox->intin_urb);
    if (result == 0) {
        slot->report_pending = 1U;
        slot->retry_armed = 0U;
        return;
    }
    if (result == -USB_ERR_NOMEM && slot->xbox->intin_urb.hcpriv == NULL) {
        slot->xbox->intin_urb.errorcode = 0;
    }
    XTime_GetTime(&slot->retry_started);
    slot->retry_armed = 1U;
    ++slot->submit_errors;
    ++g_submit_errors;
    slot->last_error = result;
    g_last_error = result;
}

static void initialize_xinput_device(mt_xinput_slot_t *slot)
{
    struct usb_setup_packet *setup;
    uint8_t index;
    int result;

    if (slot == NULL || slot->xbox == NULL || slot->xbox->hport == NULL ||
        slot->xbox->hport->setup == NULL) {
        return;
    }
    index = (uint8_t)(slot - g_xinput_slots);
    setup = slot->xbox->hport->setup;
    memset(setup, 0, sizeof(*setup));
    memset(g_xinput_control[index], 0, MT_XINPUT_PACKET_BYTES);
    setup->bmRequestType = USB_REQUEST_DIR_IN | USB_REQUEST_VENDOR |
                           USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = UINT8_C(0x01);
    setup->wValue = UINT16_C(0x0100);
    setup->wIndex = slot->xbox->intf;
    setup->wLength = MT_C64_XINPUT_REPORT_BYTES;
    result = usbh_control_transfer(slot->xbox->hport, setup,
                                   g_xinput_control[index]);
    if (result < 0 && result != -USB_ERR_STALL) {
        xil_printf("[usb1] XInput initialization warning: %d\r\n", result);
    }
}

void usbh_xbox_run(struct usbh_xbox *xbox)
{
    mt_xinput_slot_t *slot;
    uint8_t aggregate_slot;

    if (xbox == NULL || xbox->hport == NULL || xbox->intin == NULL) return;
    if ((xbox->intin->bEndpointAddress & USB_ENDPOINT_DIRECTION_MASK) == 0U ||
        (xbox->intin->bmAttributes & USB_ENDPOINT_TYPE_MASK) !=
            USB_ENDPOINT_TYPE_INTERRUPT) {
        xil_printf("[usb1] XInput has no interrupt-IN endpoint\r\n");
        return;
    }
    slot = slot_for_xinput(xbox);
    if (slot == NULL) return;
    aggregate_slot = xinput_aggregate_slot(slot);
    (void)mt_c64_hid_disconnect(&g_c64_input, aggregate_slot);
    memset(slot, 0, sizeof(*slot));
    slot->xbox = xbox;
    slot->active = 1U;
    xil_printf("[usb1] XInput connected slot=%u aggregate=%u\r\n",
               (unsigned int)xbox->minor, (unsigned int)aggregate_slot);
    submit_xinput_report(slot);
    initialize_xinput_device(slot);
}

void usbh_xbox_stop(struct usbh_xbox *xbox)
{
    mt_xinput_slot_t *slot;

    if (xbox == NULL) return;
    slot = slot_for_xinput(xbox);
    if (slot == NULL || slot->xbox != xbox) return;
    xil_printf("[usb1] XInput disconnected slot=%u\r\n",
               (unsigned int)xbox->minor);
    (void)mt_c64_hid_disconnect(&g_c64_input,
                                xinput_aggregate_slot(slot));
    memset(slot, 0, sizeof(*slot));
}

static void retry_reports(void)
{
    XTime now;
    XTime_GetTime(&now);
    for (uint8_t index = 0U; index < CONFIG_USBHOST_MAX_HID_CLASS;
         ++index) {
        mt_hid_slot_t *const slot = &g_slots[index];
        if (slot->active != 0U && slot->report_pending == 0U &&
            slot->retry_armed != 0U &&
            now - slot->retry_started >= MT_HID_RETRY_TICKS) {
            slot->retry_armed = 0U;
            submit_report(slot);
        }
    }
    for (uint8_t index = 0U; index < CONFIG_USBHOST_MAX_XBOX_CLASS;
         ++index) {
        mt_xinput_slot_t *const slot = &g_xinput_slots[index];
        if (slot->active != 0U && slot->report_pending == 0U &&
            slot->retry_armed != 0U &&
            now - slot->retry_started >= MT_HID_RETRY_TICKS) {
            slot->retry_armed = 0U;
            submit_xinput_report(slot);
        }
    }
}

static void parse_descriptor(mt_hid_slot_t *slot)
{
    const uint8_t index = (uint8_t)(slot - g_slots);
    int result;
    if (slot->hid->report_size == 0U ||
        slot->hid->report_size > MT_HID_DESCRIPTOR_BYTES) return;
    result = usbh_hid_get_report_descriptor(
        slot->hid, g_descriptors[index], slot->hid->report_size);
    if (result < 0) {
        slot->last_error = result;
        g_last_error = result;
        return;
    }
    result = usbh_hid_parse_report_descriptor(
        g_descriptors[index], slot->hid->report_size, &slot->report_info);
    if (result < 0) {
        slot->last_error = result;
        g_last_error = result;
        return;
    }
    slot->gamepad = descriptor_has_gamepad(slot) ? 1U : 0U;
}

static void host_event(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
                       uint8_t interface, uint8_t event)
{
    xil_printf("[usb1] event bus=%u hub=%u port=%u intf=%u ev=%u\r\n",
               (unsigned int)busid, (unsigned int)hub_index,
               (unsigned int)hub_port, (unsigned int)interface,
               (unsigned int)event);
}

void usbh_hid_run(struct usbh_hid *hid)
{
    mt_hid_slot_t *slot;
    struct usb_interface_descriptor *descriptor;

    if (hid == NULL || hid->hport == NULL) return;
    slot = slot_for_hid(hid);
    if (slot == NULL) return;
    (void)mt_c64_hid_disconnect(&g_c64_input, hid->minor);
    slot_clear(slot);
    descriptor = &hid->hport->config.intf[hid->intf]
                      .altsetting[0].intf_desc;
    slot->hid = hid;
    slot->active = 1U;
    slot->keyboard =
        (uint8_t)(hid->protocol == HID_PROTOCOL_KEYBOARD &&
                  descriptor->bInterfaceSubClass == HID_SUBCLASS_BOOTIF);
    hid->user_data = slot;
    (void)usbh_hid_set_idle(hid, 0U, 0U);
    if (slot->keyboard != 0U) {
        (void)usbh_hid_set_protocol(hid, HID_PROTOCOL_BOOT);
    } else {
        parse_descriptor(slot);
    }
    xil_printf("[usb1] HID connected slot=%u keyboard=%u gamepad=%u\r\n",
               (unsigned int)hid->minor, (unsigned int)slot->keyboard,
               (unsigned int)slot->gamepad);
    submit_report(slot);
}

void usbh_hid_stop(struct usbh_hid *hid)
{
    mt_hid_slot_t *slot;
    if (hid == NULL) return;
    slot = hid->user_data;
    if (slot == NULL) slot = slot_for_hid(hid);
    if (slot == NULL || slot->hid != hid) return;
    xil_printf("[usb1] HID disconnected slot=%u\r\n",
               (unsigned int)hid->minor);
    (void)mt_c64_hid_disconnect(&g_c64_input, hid->minor);
    hid->user_data = NULL;
    slot_clear(slot);
}

void usb_hid_service_init(mt_core_link_t *link)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(g_xinput_slots, 0, sizeof(g_xinput_slots));
    g_reports_total = 0U;
    g_submit_errors = 0U;
    g_transfer_errors = 0U;
    g_input_errors = 0U;
    g_last_error = 0;
    g_started = 0U;
    g_power_shutdown_required = 0U;
    g_core_ready = 0U;
    mt_c64_hid_input_init(&g_c64_input, emit_c64_input, link);
}

int usb_hid_service_start(void)
{
    int cleanup_result;
    int result;
    if (g_started != 0U) return 0;
    if (g_power_shutdown_required != 0U) {
        result = cherryusb_usb1_host_power_stop();
        if (result != 0) {
            g_last_error = result;
            return result;
        }
        g_power_shutdown_required = 0U;
    }
    xil_printf("[usb1] CherryUSB keyboard/gamepad host start\r\n");
    result = usbh_initialize(CHERRYUSB_USB1_BUSID, MT_USB1_BASE, host_event);
    if (result == 0) {
        g_started = 1U;
        result = cherryusb_usb1_host_power_start();
        if (result == 0) {
            g_power_shutdown_required = 1U;
        } else {
            if (cherryusb_usb1_power_stop_result() != 0) {
                g_power_shutdown_required = 1U;
            }
            cleanup_result = usb_hid_service_stop();
            if (cleanup_result != 0) {
                result = cleanup_result;
            }
        }
    }
    if (result != 0) {
        g_started = 0U;
        g_last_error = result;
        xil_printf("[usb1] host start failed: %d\r\n", result);
    }
    return result;
}

int usb_hid_service_stop(void)
{
    int power_result = 0;

    if (g_started != 0U || g_power_shutdown_required != 0U) {
        g_power_shutdown_required = 1U;
        power_result = cherryusb_usb1_host_power_stop();
        if (power_result == 0) {
            g_power_shutdown_required = 0U;
        }
    }
    if (g_started != 0U) {
        (void)usbh_deinitialize(CHERRYUSB_USB1_BUSID);
        if (power_result != 0 &&
            cherryusb_usb1_power_stop_result() == 0) {
            /* The independent pre-stop failed, but the low-level deinit hook
             * subsequently removed and verified VBUS. */
            power_result = 0;
            g_power_shutdown_required = 0U;
        } else if (power_result != 0) {
            /* If CherryUSB returned before the low-level hook, make one final
             * independent attempt while the ULPI viewport is still active. */
            power_result = cherryusb_usb1_host_power_stop();
            if (power_result == 0) {
                g_power_shutdown_required = 0U;
            }
        }
        g_started = 0U;
    }
    (void)mt_c64_hid_release_all(&g_c64_input);
    memset(g_slots, 0, sizeof(g_slots));
    memset(g_xinput_slots, 0, sizeof(g_xinput_slots));
    if (power_result != 0) {
        g_power_shutdown_required = 1U;
        g_last_error = power_result;
    }
    return power_result;
}

void usb_hid_service_set_core_ready(bool ready)
{
    const uint8_t next = ready ? 1U : 0U;
    if (next != 0U && g_core_ready == 0U) {
        g_core_ready = 1U;
        mt_c64_hid_reassert(&g_c64_input);
        (void)mt_c64_hid_input_poll(&g_c64_input);
    } else {
        g_core_ready = next;
    }
}

void usb_hid_service_poll(void)
{
    if (g_started != 0U) {
        cherryusb_host_poll(CHERRYUSB_USB1_BUSID);
        retry_reports();
    }
    (void)mt_c64_hid_input_poll(&g_c64_input);
}

void cherryusb_background_poll(void)
{
    (void)mt_c64_hid_input_poll(&g_c64_input);
}

int usb_hid_service_set_joystick_port(uint8_t port)
{
    return mt_c64_hid_set_joystick_port(&g_c64_input, port);
}

uint8_t usb_hid_service_joystick_port(void)
{
    return g_c64_input.joystick_port;
}

int usb_hid_service_set_symbolic_keyboard(bool enabled)
{
    return mt_c64_hid_set_keyboard_layout(
        &g_c64_input,
        enabled ? MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC
                : MT_C64_KEYBOARD_LAYOUT_POSITIONAL);
}

bool usb_hid_service_symbolic_keyboard(void)
{
    return mt_c64_hid_get_keyboard_layout(&g_c64_input) ==
           MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC;
}

void usb_hid_service_get_status(mt_usb_hid_status_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    status->reports = g_reports_total;
    status->submit_errors = g_submit_errors;
    status->transfer_errors = g_transfer_errors;
    status->input_errors = g_input_errors;
    status->last_error = g_last_error;
    status->portsc = cherryusb_usb1_portsc();
    status->started = g_started;
    status->joystick_port = g_c64_input.joystick_port;
    status->symbolic_keyboard =
        (uint8_t)(usb_hid_service_symbolic_keyboard() ? 1U : 0U);
    for (uint8_t index = 0U; index < CONFIG_USBHOST_MAX_HID_CLASS;
         ++index) {
        status->keyboards += g_slots[index].keyboard;
        status->gamepads += g_slots[index].gamepad;
    }
    for (uint8_t index = 0U; index < CONFIG_USBHOST_MAX_XBOX_CLASS;
         ++index) {
        status->gamepads += g_xinput_slots[index].active;
    }
}

void usb_hid_service_dump_status(void)
{
    mt_usb_hid_status_t status;
    cherryusb_usb1_phy_status_t phy;
    usb_hid_service_get_status(&status);
    cherryusb_usb1_phy_status(&phy);
    xil_printf("usb1 started=%u portsc=%08x keyboards=%u gamepads=%u "
               "joyport=%u keymap=%s reports=%u submit_err=%u "
               "transfer_err=%u input_err=%u last=%d\r\n",
               (unsigned int)status.started, (unsigned int)status.portsc,
               (unsigned int)status.keyboards,
               (unsigned int)status.gamepads,
               (unsigned int)status.joystick_port,
               status.symbolic_keyboard != 0U ? "us" : "positional",
               (unsigned int)status.reports,
               (unsigned int)status.submit_errors,
               (unsigned int)status.transfer_errors,
               (unsigned int)status.input_errors, status.last_error);
    xil_printf("usb1 phy setup_rc=%d disable_rc=%d drive_known=%u drive=%u "
               "vbus_known=%u vbus_valid=%u ulpi_err=%d timeouts=%u "
               "id=%04x:%04x func=%02x iface=%02x otg=%02x int=%02x "
               "debug=%02x otgsc=%08x viewport=%08x\r\n",
               phy.power_result, phy.disable_result,
               (unsigned int)phy.vbus_drive_known,
               (unsigned int)phy.vbus_drive_enabled,
               (unsigned int)phy.vbus_valid_known,
               (unsigned int)phy.vbus_valid, phy.last_error,
               (unsigned int)phy.timeouts,
               (unsigned int)phy.vendor_id, (unsigned int)phy.product_id,
               (unsigned int)phy.function_control,
               (unsigned int)phy.interface_control,
               (unsigned int)phy.otg_control,
               (unsigned int)phy.interrupt_status,
               (unsigned int)phy.debug, (unsigned int)phy.otgsc,
               (unsigned int)phy.viewport);
}
