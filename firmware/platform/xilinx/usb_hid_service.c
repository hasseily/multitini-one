/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared USB keyboard, HID gamepad, and XInput adapter on USB1. */
/* Startup hardening adapted from Appletini ONE 1fd4f371e8f4ef08bc63875550564590609fcfeb. */
#include "usb_hid_service.h"

#include "cherryusb_platform.h"
#include "multitini_c64_hid_input.h"
#include "multitini_c64_interface.h"
#include "multitini_c64_xinput.h"
#include "multitini_gameboy_hid_input.h"
#include "multitini_gameboy_interface.h"
#include "multitini_vectrex_hid_input.h"
#include "multitini_vectrex_interface.h"
#include "multitini_genesis_hid_input.h"
#include "multitini_genesis_interface.h"
#include "multitini_nes_interface.h"
#include "multitini_nes_hid_input.h"
#include "multitini_iigs_hid_input.h"
#include "usb_config.h"
#include "usbh_core.h"
#include "usbh_hid.h"
#include "usbh_xbox.h"
#include "usb_gamepad_input.h"
#include "usb_ps4_report.h"
#include "usb_xbox_one.h"
#include "usb_gamepad_descriptor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

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
#define MT_HID_RETRY_TICKS ((XTime)(COUNTS_PER_SECOND / UINT64_C(100)))
#define MT_MENU_CHORD_TICKS ((XTime)(COUNTS_PER_SECOND / UINT64_C(2)))
#define MT_INPUT_COMMAND_POLLS UINT32_C(10000)
#define MT_XINPUT_PACKET_BYTES 64U
#define MT_XINPUT_DIAGNOSTIC_BYTES 10U
#define MT_XINPUT_OUTPUT_ATTEMPTS 3U
#define MT_CONTROLLER_STRING_TIMEOUT_MS 25U
#define MT_CONTROLLER_STRING_BYTES 256U

#if (MT_XINPUT_PACKET_BYTES % CONFIG_USB_ALIGN_SIZE) != 0
#error XInput DMA buffers must span whole cache lines
#endif

#if CONFIG_USBHOST_MAX_HID_CLASS > MT_GAMEPAD_SLOTS || \
    CONFIG_USBHOST_MAX_XBOX_CLASS > MT_GAMEPAD_SLOTS
#error USB class instances exceed the shared input allocator
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
    uint8_t mouse;
    uint8_t gamepad;
    uint8_t input_slot;
    uint8_t ps4;
    uint8_t dpad;
    uint8_t report_pending;
    uint8_t retry_armed;
} mt_hid_slot_t;

typedef struct {
    struct usbh_xbox *xbox;
    XTime retry_started;
    XTime output_retry_started;
    uint32_t reports;
    uint32_t state_changes;
    uint32_t submit_errors;
    uint32_t transfer_errors;
    int last_error;
    int output_last_error;
    uint8_t last_packet[MT_XINPUT_DIAGNOSTIC_BYTES];
    uint8_t input_slot;
    uint8_t decoded_state;
    uint8_t observed_state;
    uint8_t last_packet_length;
    uint8_t active;
    uint8_t report_pending;
    uint8_t retry_armed;
    uint8_t output_pending;
    uint8_t output_retry_armed;
    uint8_t output_attempts;
    uint8_t output_sent;
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
static uint8_t
    g_xinput_outputs[CONFIG_USBHOST_MAX_XBOX_CLASS]
                    [MT_XINPUT_PACKET_BYTES]
    __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static mt_gamepad_input_t g_gamepads;
static mt_controller_device_t g_devices[MT_GAMEPAD_SLOTS];
static uint8_t g_controller_string[MT_CONTROLLER_STRING_BYTES]
    __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)));
static bool g_controller_string_busy;
static mt_controller_config_t g_controller_config[MT_GAMEPAD_SLOTS];
static mt_controls_output_t g_controller_output[MT_GAMEPAD_SLOTS];
static uint8_t g_controller_player[MT_GAMEPAD_SLOTS];
static mt_usb_controller_config_fn g_config_loader;
static void *g_config_loader_context;
static mt_usb_controller_action_fn g_action_handler;
static void *g_action_handler_context;
static mt_usb_controller_report_fn g_report_handler;
static void *g_report_handler_context;
static uint8_t g_vendor_slots[MT_GAMEPAD_SLOTS];
static uint8_t g_release_guard[MT_GAMEPAD_SLOTS];
static uint8_t g_controller_tab[MT_GAMEPAD_SLOTS];
enum { MT_MENU_CHORD_IDLE, MT_MENU_CHORD_HOLDING, MT_MENU_CHORD_BLOCKED };
typedef struct {
    XTime started;
    uint32_t buttons;
    uint8_t state;
} mt_menu_chord_t;
static mt_menu_chord_t g_menu_chord[MT_GAMEPAD_SLOTS];
static uint8_t g_menu_navigation_guard[MT_GAMEPAD_SLOTS];
static bool g_menu_capture;
static bool g_gamepad_preview;
static mt_c64_hid_input_t g_c64_input;
static mt_gameboy_hid_input_t g_gameboy_input;
static mt_vectrex_hid_input_t g_vectrex_input;
static mt_genesis_hid_input_t g_genesis_input;
static mt_nes_hid_input_t g_nes_input;
static mt_iigs_hid_input_t g_iigs_input;
static uint32_t g_reports_total;
static uint32_t g_submit_errors;
static uint32_t g_transfer_errors;
static uint32_t g_input_errors;
static int g_last_error;
static uint8_t g_started;
static uint8_t g_power_shutdown_required;
static uint8_t g_core_ready;
static uint8_t g_emit_deferred;
static uint32_t g_active_core_id;
static mt_usb_keyboard_filter_fn g_keyboard_filter;
static void *g_keyboard_filter_context;
static void refresh_controller_outputs(bool reload, bool reset_players);
static void deliver_controllers(void);

static int emit_active_input(void *context, uint16_t code, uint16_t value)
{
    mt_core_link_t *const link = context;
    mt_link_result_t result;
    const uint8_t input_namespace = (uint8_t)(code >> 8U);

    if (link == NULL || g_core_ready == 0U || g_emit_deferred != 0U) return -1;
    if (g_active_core_id == MT_IIGS_CORE_ID) {
        if(input_namespace != MT_INPUT_PS2_SET2 && input_namespace != MT_INPUT_IIGS_MOUSE &&
            input_namespace != MT_INPUT_IIGS_JOYSTICK)return -1;
    } else if(input_namespace == MT_INPUT_IIGS_MOUSE || input_namespace == MT_INPUT_IIGS_JOYSTICK)return -1;
    if ((g_active_core_id == MT_VECTREX_CORE_ID) !=
        (input_namespace == MT_INPUT_VECTREX)) return -1;
    if ((g_active_core_id == MT_NES_CORE_ID) !=
        (input_namespace == MT_INPUT_NES_GAMEPAD)) return -1;
    if ((g_active_core_id == MT_GENESIS_CORE_ID) !=
        (input_namespace == MT_INPUT_GENESIS_GAMEPAD)) return -1;
    if ((g_active_core_id == MT_GBC_CORE_ID &&
         input_namespace != MT_INPUT_GB_GAMEPAD) ||
        (g_active_core_id != MT_GBC_CORE_ID &&
         input_namespace == MT_INPUT_GB_GAMEPAD)) {
        return -1;
    }
    result = mt_core_link_input_event(link, code, value,
                                      MT_INPUT_COMMAND_POLLS, NULL, NULL);
    if (result != MT_LINK_OK) {
        ++g_input_errors;
        g_last_error = -(int)result;
        return -1;
    }
    return 0;
}

static void submit_keyboard_state(uint8_t slot, uint8_t modifiers,
                                  const uint8_t *keys, size_t key_count)
{
    if(g_active_core_id == MT_IIGS_CORE_ID) {
        (void)mt_iigs_hid_keyboard_report(&g_iigs_input,slot,modifiers,keys,key_count);
        return;
    }
    (void)mt_c64_hid_keyboard_report(&g_c64_input, slot, modifiers,
                                     keys, key_count);
    (void)mt_gameboy_hid_keyboard_report(&g_gameboy_input, slot, modifiers,
                                         keys, key_count);
    (void)mt_vectrex_hid_keyboard_report(&g_vectrex_input, slot, modifiers,
                                         keys, key_count);
    (void)mt_genesis_hid_keyboard_report(&g_genesis_input, slot, modifiers,
                                         keys, key_count);
    (void)mt_nes_hid_keyboard_report(&g_nes_input, slot, modifiers, keys, key_count);
}

static void disconnect_input_slot(uint8_t slot)
{
    g_controller_tab[slot] = 0U;
    memset(&g_menu_chord[slot], 0, sizeof(g_menu_chord[slot]));
    g_menu_navigation_guard[slot] = 0U;
    memset(&g_devices[slot], 0, sizeof(g_devices[slot]));
    memset(&g_controller_output[slot], 0, sizeof(g_controller_output[slot]));
    g_controller_player[slot] = UINT8_MAX;
    (void)mt_c64_hid_disconnect(&g_c64_input, slot);
    (void)mt_gameboy_hid_disconnect(&g_gameboy_input, slot);
    (void)mt_vectrex_hid_disconnect(&g_vectrex_input, slot);
    (void)mt_genesis_hid_disconnect(&g_genesis_input, slot);
    (void)mt_nes_hid_disconnect(&g_nes_input, slot);
    (void)mt_iigs_hid_disconnect(&g_iigs_input,slot);
    refresh_controller_outputs(false, false);
}

static unsigned controller_player_count(void)
{
    return g_active_core_id == MT_GBC_CORE_ID || g_active_core_id == MT_IIGS_CORE_ID ? 1U : 2U;
}

static void resolve_controller_players(void)
{
    uint8_t occupied = 0U, explicit_owner[2] = {UINT8_MAX, UINT8_MAX};
    const unsigned players = controller_player_count();
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        if (!g_devices[slot].connected) continue;
        const unsigned choice = g_controller_config[slot].assignment;
        if (choice == MT_CONTROLLER_PORT1 || choice == MT_CONTROLLER_PORT2) {
            const unsigned player = choice - 1U;
            if (explicit_owner[player] == UINT8_MAX || g_controller_player[slot] == player)
                explicit_owner[player] = (uint8_t)slot;
        }
    }
    for (unsigned player = 0U; player < players; ++player)
        if (explicit_owner[player] != UINT8_MAX) occupied |= (uint8_t)(1U << player);
    /* A reconnecting saved profile cannot evict the current explicit owner.
     * At boot the first claimant wins. A conflicting newcomer stays neutral. */
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        const unsigned choice = g_controller_config[slot].assignment;
        if (!g_devices[slot].connected || choice == MT_CONTROLLER_OFF)
            g_controller_player[slot] = UINT8_MAX;
        else if (choice == MT_CONTROLLER_PORT1 || choice == MT_CONTROLLER_PORT2) {
            const unsigned player = choice - 1U;
            g_controller_player[slot] = player < players && explicit_owner[player] == slot ?
                (uint8_t)player : UINT8_MAX;
        } else if (g_controller_player[slot] < players &&
                   (occupied & (1U << g_controller_player[slot])) == 0U)
            occupied |= (uint8_t)(1U << g_controller_player[slot]);
        else g_controller_player[slot] = UINT8_MAX;
    }
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        if (!g_devices[slot].connected || g_controller_config[slot].assignment != MT_CONTROLLER_AUTO ||
            g_controller_player[slot] != UINT8_MAX) continue;
        /* C64's default first controller uses its selected joystick port. */
        for (unsigned candidate = 0U; candidate < players; ++candidate) {
            unsigned player = candidate;
            if (g_active_core_id != MT_IIGS_CORE_ID && g_active_core_id != MT_GBC_CORE_ID && g_active_core_id != MT_GENESIS_CORE_ID && g_active_core_id != MT_NES_CORE_ID &&
                g_active_core_id != MT_VECTREX_CORE_ID && g_c64_input.joystick_port == 2U)
                player = 1U - candidate;
            if ((occupied & (1U << player)) == 0U) {
                g_controller_player[slot] = (uint8_t)player;
                occupied |= (uint8_t)(1U << player);
                break;
            }
        }
    }
}

static void load_controller_config(uint8_t slot)
{
    mt_controls_default(g_active_core_id, &g_devices[slot], &g_controller_config[slot]);
    if (g_config_loader != NULL) {
        mt_controller_config_t saved = g_controller_config[slot];
        if (g_config_loader(g_config_loader_context, g_active_core_id, slot,
                            &g_devices[slot], &saved) &&
            mt_controls_config_valid(g_active_core_id, &saved))
            g_controller_config[slot] = saved;
    }
    memset(&g_controller_output[slot], 0, sizeof(g_controller_output[slot]));
}

static uint32_t identity_hash(uint32_t hash, const void *bytes, size_t length)
{
    const uint8_t *data = bytes;
    for (size_t i = 0U; i < length; ++i) hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}

static size_t controller_string_descriptor(struct usbh_hubport *port,
                                            uint8_t index, uint16_t language)
{
    struct usb_setup_packet *setup = port->setup;
    struct usbh_urb *control = &port->ep0_urb;
    if (!port->connected || setup == NULL || control->errorcode == -USB_ERR_BUSY)
        return 0U;
    usb_osal_mutex_take(port->mutex);
    setup->bmRequestType = USB_REQUEST_DIR_IN | USB_REQUEST_STANDARD |
                           USB_REQUEST_RECIPIENT_DEVICE;
    setup->bRequest = USB_REQUEST_GET_DESCRIPTOR;
    setup->wValue = (uint16_t)((USB_DESCRIPTOR_TYPE_STRING << 8U) | index);
    setup->wIndex = language;
    setup->wLength = 255U;
    memset(g_controller_string, 0, sizeof(g_controller_string));
    /* Optional identity reads get one short attempt. Do not use the generic
     * control wrapper's one-second timeout and three retries during hotplug. */
    usbh_control_urb_fill(control, port, setup, g_controller_string, 255U,
                          MT_CONTROLLER_STRING_TIMEOUT_MS, NULL, NULL);
    int result = usbh_submit_urb(control);
    if (result == 0) result = (int)control->actual_length;
    if (result == -USB_ERR_NOMEM && control->hcpriv == NULL) control->errorcode = 0;
    control->timeout = 0U;
    usb_osal_mutex_give(port->mutex);
    if (result < 2 || g_controller_string[1] != USB_DESCRIPTOR_TYPE_STRING ||
        g_controller_string[0] < 2U || (g_controller_string[0] & 1U) != 0U ||
        g_controller_string[0] > result) return 0U;
    return g_controller_string[0];
}

static void controller_optional_strings(mt_controller_device_t *device,
                                        struct usbh_hubport *port)
{
    const bool product = device->name[0] == '\0' && port->device_desc.iProduct != 0U;
    const bool serial = device->identity.serial_hash == 0U && port->device_desc.iSerialNumber != 0U;
    if ((!product && !serial) || g_controller_string_busy) return;
    g_controller_string_busy = true;
    uint16_t language = UINT16_C(0x0409);
    if (controller_string_descriptor(port, 0U, 0U) >= 4U)
        language = (uint16_t)((uint16_t)g_controller_string[2] |
                              ((uint16_t)g_controller_string[3] << 8U));
    if (product) {
        const size_t length = controller_string_descriptor(port, port->device_desc.iProduct, language);
        size_t out = 0U;
        for (size_t at = 2U; at + 1U < length && out + 1U < sizeof(device->name); at += 2U) {
            const uint8_t ascii = g_controller_string[at];
            device->name[out++] = g_controller_string[at + 1U] == 0U && ascii >= 32U && ascii < 127U ?
                (char)ascii : '?';
        }
        device->name[out] = '\0';
    }
    if (serial) {
        const size_t length = controller_string_descriptor(port, port->device_desc.iSerialNumber, language);
        /* Hash complete UTF-16 content, independently of display truncation. */
        if (length > 2U) device->identity.serial_hash = identity_hash(
            UINT32_C(2166136261), g_controller_string + 2U, length - 2U);
    }
    g_controller_string_busy = false;
}

static void identify_controller(uint8_t slot, struct usbh_hubport *port,
    uint8_t interface_id, uint8_t transport, uint32_t descriptor_hash,
    uint32_t buttons, uint8_t axes)
{
    if (slot >= MT_GAMEPAD_SLOTS || port == NULL) return;
    mt_controller_device_t *device = &g_devices[slot];
    memset(device, 0, sizeof(*device));
    device->connected = 1U;
    device->home_button = transport == MT_CONTROLLER_TRANSPORT_HID ? UINT8_MAX : MT_USB_PAD_HOME;
    device->buttons_present = buttons;
    device->axis_mask = axes;
    device->identity.vendor_id = port->device_desc.idVendor;
    device->identity.product_id = port->device_desc.idProduct;
    device->identity.interface_id = interface_id;
    device->identity.transport = transport;
    device->identity.descriptor_hash = descriptor_hash;
    if (port->iSerialNumber != NULL && port->iSerialNumber[0] != '\0')
        device->identity.serial_hash = identity_hash(UINT32_C(2166136261), port->iSerialNumber,
                                                     strlen(port->iSerialNumber));
    uint32_t path = UINT32_C(2166136261);
    const struct usbh_hubport *ancestor = port;
    for (unsigned depth = 0U; ancestor != NULL && depth < 16U; ++depth) {
        path = identity_hash(path, &ancestor->port, 1U);
        ancestor = ancestor->parent != NULL ? ancestor->parent->parent : NULL;
    }
    device->identity.port_path = path;
    if (port->iProduct != NULL && port->iProduct[0] != '\0')
        (void)snprintf(device->name, sizeof(device->name), "%s", port->iProduct);
    controller_optional_strings(device, port);
    if (device->name[0] == '\0') {
        const char *name = transport == MT_CONTROLLER_TRANSPORT_XBOX360 ? "Xbox 360" :
            transport == MT_CONTROLLER_TRANSPORT_XBOXONE ? "Xbox One" :
            transport == MT_CONTROLLER_TRANSPORT_PS4 ? "DualShock 4" : "USB controller";
        (void)snprintf(device->name, sizeof(device->name), "%s %04X:%04X", name,
            (unsigned)device->identity.vendor_id, (unsigned)device->identity.product_id);
    }
    load_controller_config(slot);
    g_controller_player[slot] = UINT8_MAX;
    resolve_controller_players();
}

void usb_gamepad_input_identify(uint8_t slot, struct usbh_hubport *port,
                                uint8_t interface_id, uint8_t transport)
{
    const uint32_t buttons = transport == MT_CONTROLLER_TRANSPORT_PS4 ? UINT32_C(0x3fff) : UINT32_C(0x7ff);
    identify_controller(slot, port, interface_id, transport,
                         UINT32_C(0x80000000) | transport, buttons, 0x3fU);
}

static void poll_active_input(void)
{
    if (g_active_core_id == MT_IIGS_CORE_ID)
        (void)mt_iigs_hid_poll(&g_iigs_input);
    else if (g_active_core_id == MT_VECTREX_CORE_ID)
        (void)mt_vectrex_hid_input_poll(&g_vectrex_input);
    else if (g_active_core_id == MT_GBC_CORE_ID)
        (void)mt_gameboy_hid_input_poll(&g_gameboy_input);
    else if (g_active_core_id == MT_GENESIS_CORE_ID)
        (void)mt_genesis_hid_input_poll(&g_genesis_input);
    else if (g_active_core_id == MT_NES_CORE_ID)
        (void)mt_nes_hid_input_poll(&g_nes_input);
    else
        (void)mt_c64_hid_input_poll(&g_c64_input);
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
    return slot->input_slot;
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

static bool descriptor_has_gamepad(const mt_hid_slot_t *slot)
{
    uint8_t axes = 0U;
    bool hat = false;

    for (uint32_t index = 0U;
         index < slot->report_info.report_item_count; ++index) {
        const struct usbh_hid_report_item *const item =
            &slot->report_info.report_items[index];
        if (item->report_type == HID_REPORT_INPUT &&
            item->attribute.usage_page == HID_USAGE_PAGE_BUTTON &&
            (item->report_flags & HID_MAINITEM_CONSTANT) == 0U) return true;
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

static void deliver_gamepad(uint8_t index)
{
    if (!g_devices[index].connected) return;
    mt_controls_output_t output = g_controller_output[index];
    if (g_menu_capture || g_release_guard[index] != 0U ||
        g_menu_chord[index].state != MT_MENU_CHORD_IDLE) {
        memset(&output, 0, sizeof(output));
    }
    const uint8_t player = g_controller_player[index];
    const uint32_t actions = output.actions;
    const uint8_t directions = (uint8_t)(actions & 15U);
    if (g_active_core_id == MT_IIGS_CORE_ID) {
        (void)mt_iigs_hid_assigned_report(&g_iigs_input,index,player,
            (uint8_t)((actions>>MT_CONTROL_A)&7U),output.x,output.y,directions);
    } else if (g_active_core_id == MT_GENESIS_CORE_ID || g_active_core_id == MT_NES_CORE_ID) {
        mt_gamepad_state_t state = {0};
        state.connected = 1U;
        state.hat = 8U;
        for (uint8_t hat = 0U; hat < 8U; ++hat)
            if (mt_gamepad_hat_bits(hat) == directions) state.hat = hat;
        state.buttons = (actions >> MT_CONTROL_A) & 0x3fU;
        if (actions & (UINT32_C(1) << MT_CONTROL_SELECT)) state.buttons |= 0x40U;
        if (actions & (UINT32_C(1) << MT_CONTROL_START)) state.buttons |= 0x80U;
        if (g_active_core_id == MT_NES_CORE_ID)
            (void)mt_nes_hid_assigned_report(&g_nes_input, index, player, &state);
        else (void)mt_genesis_hid_assigned_report(&g_genesis_input, index, player, &state);
    } else if (g_active_core_id == MT_VECTREX_CORE_ID) {
        int16_t x = output.x;
        int16_t y = output.y == INT16_MIN ? INT16_MAX : (int16_t)-output.y;
        if ((directions & 12U) != 0U)
            x = (directions & 12U) == 12U ? 0 : ((directions & 4U) != 0U ? INT16_MIN : INT16_MAX);
        if ((directions & 3U) != 0U)
            y = (directions & 3U) == 3U ? 0 : ((directions & 1U) != 0U ? INT16_MAX : INT16_MIN);
        (void)mt_vectrex_hid_assigned_report(&g_vectrex_input, index, player,
            (uint8_t)((actions >> MT_CONTROL_A) & 15U), x, y);
    } else if (g_active_core_id == MT_GBC_CORE_ID) {
        uint8_t state = (uint8_t)(directions | (actions & 0x30U));
        if (actions & (UINT32_C(1) << MT_CONTROL_SELECT)) state |= 0x40U;
        if (actions & (UINT32_C(1) << MT_CONTROL_START)) state |= 0x80U;
        (void)mt_gameboy_hid_gamepad_report(&g_gameboy_input, index, player == 0U ? state : 0U);
    } else {
        (void)mt_c64_hid_assigned_report(&g_c64_input, index, player,
            (uint8_t)(directions | (actions & 0x70U)));
    }
}

static void deliver_controllers(void)
{
    /* Publish complete assignment changes together, without a transient move
     * or old controller state appearing in another player's output. */
    const uint8_t deferred = g_emit_deferred;
    g_emit_deferred = 1U;
    for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) deliver_gamepad(slot);
    g_emit_deferred = deferred;
    poll_active_input();
}

static void refresh_controller_outputs(bool reload, bool reset_players)
{
    if (reset_players) memset(g_controller_player, UINT8_MAX, sizeof(g_controller_player));
    for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        if (!g_devices[slot].connected) continue;
        if (reload) load_controller_config(slot);
        mt_controls_evaluate(g_active_core_id, &g_controller_config[slot],
            &g_gamepads.raw[slot], g_controller_output[slot].actions, &g_controller_output[slot]);
    }
    resolve_controller_players();
    deliver_controllers();
}

static bool pad_active(const mt_gamepad_state_t *state)
{
    return !mt_controls_neutral(state);
}

static uint32_t menu_chord_buttons(uint8_t index)
{
    if (g_devices[index].identity.transport != MT_CONTROLLER_TRANSPORT_HID)
        return UINT32_C(0xc0); /* Normalized Select and Start. */
    /* Generic HID preserves button usages. Ten-button pads use 9/10;
     * eight-button SNES pads use 7/8 after their four face and two shoulders. */
    const uint32_t present = g_devices[index].buttons_present;
    if ((present & ~UINT32_C(0xff)) == 0U &&
        (present & UINT32_C(0xc0)) == UINT32_C(0xc0)) return UINT32_C(0xc0);
    return UINT32_C(0x300);
}

static void reset_menu_chord(uint8_t index)
{
    mt_menu_chord_t *chord = &g_menu_chord[index];
    memset(chord, 0, sizeof(*chord));
    chord->buttons = menu_chord_buttons(index);
    /* A held opener cannot cross a core/configuration transition. */
    if ((g_gamepads.raw[index].buttons & chord->buttons) != 0U)
        chord->state = MT_MENU_CHORD_BLOCKED;
}

static void update_menu_chord(uint8_t index)
{
    mt_menu_chord_t *chord = &g_menu_chord[index];
    if (chord->state == MT_MENU_CHORD_IDLE) chord->buttons = menu_chord_buttons(index);
    const uint32_t held = g_gamepads.raw[index].buttons & chord->buttons;
    if (held == 0U) {
        chord->state = MT_MENU_CHORD_IDLE;
    } else if (chord->state == MT_MENU_CHORD_HOLDING && held != chord->buttons) {
        chord->state = MT_MENU_CHORD_BLOCKED;
    } else if (chord->state == MT_MENU_CHORD_IDLE && held == chord->buttons) {
        XTime_GetTime(&chord->started);
        chord->state = MT_MENU_CHORD_HOLDING;
    }
}

static void poll_menu_chords(void)
{
    XTime now;
    XTime_GetTime(&now);
    for (uint8_t index = 0U; index < MT_GAMEPAD_SLOTS; ++index) {
        mt_menu_chord_t *chord = &g_menu_chord[index];
        if (chord->state != MT_MENU_CHORD_HOLDING ||
            (XTime)(now - chord->started) < MT_MENU_CHORD_TICKS) continue;
        chord->state = MT_MENU_CHORD_BLOCKED;
        if (g_keyboard_filter == NULL) continue;
        /* A pulse, containing only F12, also escapes controller capture/test.
         * Keep it separate from Enter/Escape, directions and Tab. */
        g_controller_tab[index] = 0U;
        g_menu_navigation_guard[index] = pad_active(&g_gamepads.raw[index]) ? 1U : 0U;
        const uint8_t key = 0x45U;
        (void)g_keyboard_filter(g_keyboard_filter_context, index, 0U, &key, 1U);
        (void)g_keyboard_filter(g_keyboard_filter_context, index, 0U, NULL, 0U);
    }
}

static void controller_navigation(uint8_t index)
{
    if (g_keyboard_filter == NULL || g_gamepad_preview) return;
    uint8_t keys[7], count = 0U;
    const mt_gamepad_state_t *raw = &g_gamepads.raw[index];
    const bool chord = g_menu_chord[index].state != MT_MENU_CHORD_IDLE;
    const bool menu = !chord &&
        (g_controller_output[index].actions & (UINT32_C(1) << MT_CONTROL_MENU)) != 0U;
    const bool navigation = !chord && !menu && g_menu_navigation_guard[index] == 0U;
    const uint8_t directions = navigation ? mt_gamepad_digital(raw) & 15U : 0U;
    if ((directions & 1U) != 0U) keys[count++] = 0x52U;
    if ((directions & 2U) != 0U) keys[count++] = 0x51U;
    if ((directions & 4U) != 0U) keys[count++] = 0x50U;
    if ((directions & 8U) != 0U) keys[count++] = 0x4fU;
    if (navigation && (raw->buttons & 1U) != 0U) keys[count++] = 0x28U;
    if (navigation && (raw->buttons & 2U) != 0U) keys[count++] = 0x29U;
    if (menu) keys[count++] = 0x45U;
    /* Physical shoulders navigate the UI independently of the core map.
     * Generic HID follows Button 5/6; both held cancel. */
    const uint32_t shoulders = raw->buttons & UINT32_C(0x30);
    const uint8_t tab = g_menu_capture && navigation ?
        (shoulders == 0x10U ? 1U : shoulders == 0x20U ? 2U : 0U) : 0U;
    if (g_controller_tab[index] != 0U && tab != g_controller_tab[index])
        (void)g_keyboard_filter(g_keyboard_filter_context, index, 0U, keys, count);
    if (tab != 0U) keys[count++] = 0x2bU;
    g_controller_tab[index] = tab;
    /* Synthetic navigation never reaches a core. */
    (void)g_keyboard_filter(g_keyboard_filter_context, index,
        tab == 1U ? 0x02U : 0U, keys, count);
}

static void process_shared_report(uint8_t index, const mt_gamepad_report_t *report,
                                  uint8_t hat, uint8_t extra)
{
    mt_gamepad_report(&g_gamepads, index, report, hat, extra);
    /* Test/vendor callers may not provide USB identity; expose a generic device. */
    if (!g_devices[index].connected) {
        g_devices[index].connected = 1U;
        g_devices[index].home_button = UINT8_MAX;
        (void)snprintf(g_devices[index].name, sizeof(g_devices[index].name), "USB controller %u", index + 1U);
        load_controller_config(index);
    }
    g_devices[index].buttons_present |= report->buttons_mask | report->buttons;
    g_devices[index].axis_mask |= report->axis_valid_mask;
    update_menu_chord(index);
    /* A simultaneous escape pair is reserved during assignment/test capture;
     * keep the raw snapshot intact without assigning its first member. */
    if (g_report_handler != NULL &&
        !(g_gamepad_preview && g_menu_chord[index].state != MT_MENU_CHORD_IDLE))
        g_report_handler(g_report_handler_context, index, &g_gamepads.raw[index]);
    const uint32_t previous = g_controller_output[index].actions;
    mt_controls_evaluate(g_active_core_id, &g_controller_config[index],
                         &g_gamepads.raw[index], previous, &g_controller_output[index]);
    resolve_controller_players();
    if (!pad_active(&g_gamepads.raw[index])) {
        g_release_guard[index] = 0U;
        g_menu_navigation_guard[index] = 0U;
    }
    poll_menu_chords();
    if (g_action_handler != NULL && !g_gamepad_preview &&
        g_menu_chord[index].state == MT_MENU_CHORD_IDLE) {
        const uint32_t edges = g_controller_output[index].actions & ~previous;
        if (edges & (UINT32_C(1) << MT_CONTROL_SCREENSHOT))
            g_action_handler(g_action_handler_context, MT_CONTROL_SCREENSHOT);
        if (edges & (UINT32_C(1) << MT_CONTROL_SCREENSHOT_OUTPUT))
            g_action_handler(g_action_handler_context, MT_CONTROL_SCREENSHOT_OUTPUT);
    }
    controller_navigation(index);
    deliver_controllers();
}

int usb_gamepad_input_connect(void)
{
    const int slot = mt_gamepad_connect(&g_gamepads, MT_GAMEPAD_SLOTS);
    if (slot >= 0) g_vendor_slots[slot] = 1U;
    return slot;
}

void usb_gamepad_input_disconnect(uint8_t slot)
{
    if (slot >= MT_GAMEPAD_SLOTS || g_vendor_slots[slot] == 0U) return;
    g_vendor_slots[slot] = 0U;
    g_release_guard[slot] = 0U;
    disconnect_input_slot(slot);
    mt_gamepad_disconnect(&g_gamepads, slot);
    if (g_keyboard_filter != NULL)
        (void)g_keyboard_filter(g_keyboard_filter_context, slot, 0U, NULL, 0U);
}

void usb_gamepad_input_report(uint8_t slot, const mt_gamepad_report_t *report,
                              uint8_t hat, uint8_t extra_active)
{
    if (slot >= MT_GAMEPAD_SLOTS || g_vendor_slots[slot] == 0U || report == NULL) return;
    ++g_reports_total;
    process_shared_report(slot, report, hat, extra_active);
}

static void process_gamepad_report(mt_hid_slot_t *slot,
                                   const uint8_t *data, uint32_t length)
{
    mt_gamepad_report_t report = {0};
    uint8_t hat = UINT8_MAX, extra = 0U;
    if (slot->ps4 != 0U) {
        if (usb_ps4_decode(data, length, &report, &hat, &extra) != 0U)
            process_shared_report(slot->input_slot, &report, hat, extra);
        return;
    }
    for (uint32_t index = 0U; index < slot->report_info.report_item_count; ++index) {
        const struct usbh_hid_report_item *item = &slot->report_info.report_items[index];
        if (item->report_type != HID_REPORT_INPUT ||
            (item->report_flags & HID_MAINITEM_CONSTANT) != 0U ||
            !report_id_matches(item, data, length)) continue;
        for (uint32_t field = 0U; field < item->attribute.report_count; ++field) {
            uint32_t raw;
            if (!extract_bits(item, data, length, field, &raw)) continue;
            const uint16_t usage = usage_for_field(item, field);
            if (item->attribute.usage_page == HID_USAGE_PAGE_BUTTON) {
                if ((item->report_flags & HID_MAINITEM_VARIABLE) == 0U) {
                    /* Array fields contain usage numbers, not one bit per key. */
                    report.buttons_valid = 1U;
                    for (unsigned id = item->attribute.usage_min; id <= item->attribute.usage_max && id <= 32U; ++id)
                        if (id != 0U) report.buttons_mask |= UINT32_C(1) << (id - 1U);
                    if (raw >= item->attribute.usage_min && raw <= item->attribute.usage_max && raw > 0U && raw <= 32U)
                        report.buttons |= UINT32_C(1) << (raw - 1U);
                } else if (usage >= 1U && usage <= 32U) {
                    report.buttons_valid = 1U;
                    report.buttons_mask |= UINT32_C(1) << (usage - 1U);
                    if (raw != 0U) report.buttons |= UINT32_C(1) << (usage - 1U);
                }
            } else if (item->attribute.usage_page == 2U && (usage == 0xc4U || usage == 0xc5U)) {
                const unsigned axis = usage == 0xc4U ? MT_GAMEPAD_AXIS_Z : MT_GAMEPAD_AXIS_RZ;
                report.axis_valid_mask |= (uint8_t)(1U << axis);
                report.trigger_mask |= (uint8_t)(1U << axis);
                report.axis[axis] = item_value(item, raw);
                item_bounds(item, &report.logical_min[axis], &report.logical_max[axis]);
            } else if (item->attribute.usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP_CONTROLS &&
                       (item->report_flags & HID_MAINITEM_VARIABLE) != 0U &&
                       (item->report_flags & HID_MAINITEM_RELATIVE) == 0U) {
                int32_t minimum, maximum;
                const int32_t value = item_value(item, raw);
                item_bounds(item, &minimum, &maximum);
                if (usage >= HID_DESKTOP_USAGE_X && usage <= HID_DESKTOP_USAGE_RZ) {
                    const unsigned axis = usage - HID_DESKTOP_USAGE_X;
                    report.axis_valid_mask |= (uint8_t)(1U << axis);
                    report.axis[axis] = value;
                    report.logical_min[axis] = minimum;
                    report.logical_max[axis] = maximum;
                } else if (usage == HID_DESKTOP_USAGE_HATSWITCH) {
                    const int64_t direction = (int64_t)value - minimum;
                    hat = direction >= 0 && direction < 8 ? (uint8_t)direction : 8U;
                } else if (usage >= HID_DESKTOP_USAGE_DPAD_UP && usage <= HID_DESKTOP_USAGE_DPAD_LEFT) {
                    static const uint8_t bits[4] = {1U, 2U, 8U, 4U};
                    const uint8_t bit = bits[usage - HID_DESKTOP_USAGE_DPAD_UP];
                    slot->dpad = (uint8_t)((slot->dpad & (uint8_t)~bit) | (raw != 0U ? bit : 0U));
                    hat = 8U;
                    for (uint8_t candidate = 0U; candidate < 8U; ++candidate)
                        if (mt_gamepad_hat_bits(candidate) == slot->dpad) hat = candidate;
                }
            }
        }
    }
    process_shared_report(slot->input_slot, &report, hat, extra);
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
            const bool captured = g_keyboard_filter != NULL &&
                g_keyboard_filter(g_keyboard_filter_context, slot->input_slot,
                                  keyboard->modifier, keyboard->key,
                                  sizeof(keyboard->key));
            if (captured) {
                submit_keyboard_state(slot->input_slot, 0U, NULL, 0U);
            } else {
                uint8_t machine_keys[sizeof(keyboard->key)];
                memcpy(machine_keys, keyboard->key, sizeof(machine_keys));
                for (size_t key = 0U; key < sizeof(machine_keys); ++key)
                    if (machine_keys[key] == 0x46U) machine_keys[key] = 0U;
                submit_keyboard_state(slot->input_slot, keyboard->modifier,
                                      machine_keys, sizeof(machine_keys));
            }
        } else if (slot->mouse != 0U && bytes >= 3 && g_active_core_id == MT_IIGS_CORE_ID) {
            mt_iigs_hid_mouse_report(&g_iigs_input,g_menu_capture ? 0:(int8_t)g_reports[index][1],
                g_menu_capture ? 0:(int8_t)g_reports[index][2],g_menu_capture ? 0U:g_reports[index][0]);
        } else if (slot->gamepad != 0U) {
            process_gamepad_report(slot, g_reports[index], (uint32_t)bytes);
        }
    } else if (bytes < 0) {
        ++slot->transfer_errors;
        ++g_transfer_errors;
        slot->last_error = bytes;
        g_last_error = bytes;
        XTime_GetTime(&slot->retry_started);
        slot->retry_armed = 1U;
        return;
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
        uint32_t diagnostic_length;
        int decode_result;
        slot->last_error = 0;
        ++slot->reports;
        ++g_reports_total;
        diagnostic_length = (uint32_t)bytes;
        if (diagnostic_length > MT_XINPUT_DIAGNOSTIC_BYTES) {
            diagnostic_length = MT_XINPUT_DIAGNOSTIC_BYTES;
        }
        memset(slot->last_packet, 0, sizeof(slot->last_packet));
        memcpy(slot->last_packet, g_xinput_reports[index], diagnostic_length);
        slot->last_packet_length = (uint8_t)diagnostic_length;
        decode_result = mt_c64_xinput_decode(
            g_xinput_reports[index], (size_t)bytes, &state);
        if (decode_result == 0) {
            if (state != slot->decoded_state) ++slot->state_changes;
            slot->decoded_state = state;
            slot->observed_state |= state;
            const uint8_t *data = g_xinput_reports[index];
            mt_gamepad_report_t report = {0};
            report.axis_valid_mask = 0x3fU;
            report.trigger_mask = (1U << MT_GAMEPAD_AXIS_Z) | (1U << MT_GAMEPAD_AXIS_RZ);
            report.buttons_valid = 1U;
            report.buttons = (uint8_t)((data[3] >> 4U) | ((data[3] & 3U) << 4U) |
                ((data[2] & 0x20U) << 1U) | ((data[2] & 0x10U) << 3U));
            if (data[2] & 0x40U) report.buttons |= UINT32_C(1) << MT_USB_PAD_LEFT_STICK;
            if (data[2] & 0x80U) report.buttons |= UINT32_C(1) << MT_USB_PAD_RIGHT_STICK;
            if (data[3] & 4U) report.buttons |= UINT32_C(1) << MT_USB_PAD_HOME;
            static const uint8_t offsets[6] = {6U, 8U, 4U, 10U, 12U, 5U};
            for (unsigned axis = 0U; axis < 6U; ++axis) {
                const unsigned offset = offsets[axis];
                if (axis == MT_GAMEPAD_AXIS_Z || axis == MT_GAMEPAD_AXIS_RZ) {
                    report.axis[axis] = data[offset];
                    report.logical_max[axis] = 255;
                } else {
                    const uint16_t word = (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset+1U] << 8U));
                    int32_t value = word < 0x8000U ? (int32_t)word : (int32_t)word - 65536;
                    if (axis == MT_GAMEPAD_AXIS_Y || axis == MT_GAMEPAD_AXIS_RY) value = -1 - value;
                    report.axis[axis] = value;
                    report.logical_min[axis] = -32768;
                    report.logical_max[axis] = 32767;
                }
            }
            uint8_t hat = 8U;
            for (uint8_t candidate = 0U; candidate < 8U; ++candidate)
                if (mt_gamepad_hat_bits(candidate) == (data[2] & 15U)) hat = candidate;
            process_shared_report(slot->input_slot, &report, hat, 0U);
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
        XTime_GetTime(&slot->retry_started);
        slot->retry_armed = 1U;
        return;
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

static bool xinput_output_can_retry(int error)
{
    return error != -USB_ERR_STALL && error != -USB_ERR_SHUTDOWN &&
           error != -USB_ERR_NODEV && error != -USB_ERR_NOTCONN;
}

static void arm_xinput_output_retry(mt_xinput_slot_t *slot, int error)
{
    if (slot == NULL) return;
    slot->output_last_error = error;
    slot->last_error = error;
    g_last_error = error;
    if (slot->output_attempts >= MT_XINPUT_OUTPUT_ATTEMPTS ||
        !xinput_output_can_retry(error)) {
        return;
    }
    XTime_GetTime(&slot->output_retry_started);
    slot->output_retry_armed = 1U;
}

static void xinput_output_complete(void *argument, int bytes)
{
    mt_xinput_slot_t *const slot = argument;
    int error;

    if (slot == NULL) return;
    slot->output_pending = 0U;
    if (slot->active == 0U || slot->xbox == NULL) return;
    if (bytes == -USB_ERR_SHUTDOWN || slot->xbox->hport == NULL ||
        !slot->xbox->hport->connected) {
        return;
    }
    if (bytes == (int)MT_C64_XINPUT_LED_COMMAND_BYTES) {
        slot->output_sent = 1U;
        slot->output_retry_armed = 0U;
        slot->output_last_error = 0;
        return;
    }

    error = bytes < 0 ? bytes : -USB_ERR_IO;
    ++slot->transfer_errors;
    ++g_transfer_errors;
    arm_xinput_output_retry(slot, error);
}

static void submit_xinput_output(mt_xinput_slot_t *slot)
{
    uint8_t index;
    int command_length;
    int result;

    if (slot == NULL || slot->active == 0U || slot->xbox == NULL ||
        slot->xbox->hport == NULL || slot->output_pending != 0U ||
        slot->output_sent != 0U ||
        slot->output_attempts >= MT_XINPUT_OUTPUT_ATTEMPTS) {
        return;
    }
    if (slot->xbox->intout == NULL ||
        (slot->xbox->intout->bEndpointAddress &
         USB_ENDPOINT_DIRECTION_MASK) != 0U ||
        (slot->xbox->intout->bmAttributes & USB_ENDPOINT_TYPE_MASK) !=
            USB_ENDPOINT_TYPE_INTERRUPT ||
        USB_GET_MAXPACKETSIZE(slot->xbox->intout->wMaxPacketSize) <
            MT_C64_XINPUT_LED_COMMAND_BYTES) {
        slot->output_last_error = -USB_ERR_NOTSUPP;
        return;
    }

    index = (uint8_t)(slot - g_xinput_slots);
    memset(g_xinput_outputs[index], 0, MT_XINPUT_PACKET_BYTES);
    command_length = mt_c64_xinput_player_led_command(
        1U, g_xinput_outputs[index], MT_XINPUT_PACKET_BYTES);
    if (command_length < 0) {
        slot->output_last_error = -USB_ERR_INVAL;
        return;
    }

    slot->output_retry_armed = 0U;
    ++slot->output_attempts;
    usbh_int_urb_fill(&slot->xbox->intout_urb, slot->xbox->hport,
                      slot->xbox->intout, g_xinput_outputs[index],
                      (uint32_t)command_length, 0U,
                      xinput_output_complete, slot);
    result = usbh_submit_urb(&slot->xbox->intout_urb);
    if (result == 0) {
        slot->output_pending = 1U;
        return;
    }
    if ((result == -USB_ERR_NOMEM || result == -USB_ERR_BUSY) &&
        slot->xbox->intout_urb.hcpriv == NULL) {
        slot->xbox->intout_urb.errorcode = 0;
    }
    ++slot->submit_errors;
    ++g_submit_errors;
    arm_xinput_output_retry(slot, result);
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
    const int allocated = mt_gamepad_connect(&g_gamepads, CONFIG_USBHOST_MAX_HID_CLASS);
    if (allocated < 0) return;
    aggregate_slot = (uint8_t)allocated;
    memset(slot, 0, sizeof(*slot));
    slot->input_slot = aggregate_slot;
    slot->xbox = xbox;
    slot->active = 1U;
    usb_gamepad_input_identify(aggregate_slot, xbox->hport, xbox->intf,
                               MT_CONTROLLER_TRANSPORT_XBOX360);
    xil_printf("[usb1] XInput connected slot=%u aggregate=%u\r\n",
               (unsigned int)xbox->minor, (unsigned int)aggregate_slot);
    submit_xinput_report(slot);
    initialize_xinput_device(slot);
    submit_xinput_output(slot);
}

void usbh_xbox_stop(struct usbh_xbox *xbox)
{
    mt_xinput_slot_t *slot;

    if (xbox == NULL) return;
    slot = slot_for_xinput(xbox);
    if (slot == NULL || slot->xbox != xbox) return;
    xil_printf("[usb1] XInput disconnected slot=%u\r\n",
               (unsigned int)xbox->minor);
    disconnect_input_slot(xinput_aggregate_slot(slot));
    mt_gamepad_disconnect(&g_gamepads, slot->input_slot);
    if (g_keyboard_filter != NULL)
        (void)g_keyboard_filter(g_keyboard_filter_context, slot->input_slot, 0U, NULL, 0U);
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
        if (slot->active != 0U && slot->output_pending == 0U &&
            slot->output_retry_armed != 0U &&
            now - slot->output_retry_started >= MT_HID_RETRY_TICKS) {
            slot->output_retry_armed = 0U;
            submit_xinput_output(slot);
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
    result = mt_usb_parse_gamepad_descriptor(
        g_descriptors[index], slot->hid->report_size, &slot->report_info);
    if (result < 0) {
        slot->last_error = result;
        g_last_error = result;
        return;
    }
    slot->gamepad = descriptor_has_gamepad(slot) ? 1U : 0U;
    if (slot->gamepad) {
        uint32_t buttons = 0U;
        uint8_t axes = 0U;
        for (uint32_t i = 0U; i < slot->report_info.report_item_count; ++i) {
            const struct usbh_hid_report_item *item = &slot->report_info.report_items[i];
            if (item->attribute.usage_page == HID_USAGE_PAGE_BUTTON) {
                for (unsigned usage = item->attribute.usage_min; usage <= item->attribute.usage_max && usage <= 32U; ++usage)
                    if (usage > 0U) buttons |= UINT32_C(1) << (usage - 1U);
            } else if (item->attribute.usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP_CONTROLS &&
                       item->attribute.usage_min >= HID_DESKTOP_USAGE_X && item->attribute.usage_min <= HID_DESKTOP_USAGE_RZ)
                axes |= (uint8_t)(1U << (item->attribute.usage_min - HID_DESKTOP_USAGE_X));
            else if (item->attribute.usage_page == 2U && (item->attribute.usage_min == 0xc4U || item->attribute.usage_min == 0xc5U))
                axes |= (uint8_t)(1U << (item->attribute.usage_min == 0xc4U ? MT_GAMEPAD_AXIS_Z : MT_GAMEPAD_AXIS_RZ));
        }
        identify_controller(slot->input_slot, slot->hid->hport, slot->hid->intf,
            MT_CONTROLLER_TRANSPORT_HID,
            identity_hash(UINT32_C(2166136261), g_descriptors[index], slot->hid->report_size), buttons, axes);
    }
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
    const int allocated = mt_gamepad_connect(&g_gamepads, hid->minor);
    if (allocated < 0) return;
    slot_clear(slot);
    slot->input_slot = (uint8_t)allocated;
    descriptor = &hid->hport->config.intf[hid->intf]
                      .altsetting[0].intf_desc;
    slot->hid = hid;
    slot->active = 1U;
    slot->keyboard =
        (uint8_t)(hid->protocol == HID_PROTOCOL_KEYBOARD &&
                  descriptor->bInterfaceSubClass == HID_SUBCLASS_BOOTIF);
    slot->mouse = (uint8_t)(hid->protocol == HID_PROTOCOL_MOUSE &&
        descriptor->bInterfaceSubClass == HID_SUBCLASS_BOOTIF);
    hid->user_data = slot;
    (void)usbh_hid_set_idle(hid, 0U, 0U);
    if (slot->keyboard != 0U || slot->mouse != 0U) {
        (void)usbh_hid_set_protocol(hid, HID_PROTOCOL_BOOT);
    } else {
        slot->ps4 = usb_ps4_supported(hid->hport->device_desc.idVendor,
                                      hid->hport->device_desc.idProduct);
        if (slot->ps4 != 0U) {
            slot->gamepad = 1U;
            usb_gamepad_input_identify(slot->input_slot, hid->hport, hid->intf, MT_CONTROLLER_TRANSPORT_PS4);
        }
        else parse_descriptor(slot);
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
    if(slot->mouse && g_active_core_id==MT_IIGS_CORE_ID)
        mt_iigs_hid_mouse_report(&g_iigs_input,0,0,0U);
    disconnect_input_slot(slot->input_slot);
    mt_gamepad_disconnect(&g_gamepads, slot->input_slot);
    g_release_guard[slot->input_slot] = 0U;
    if (g_keyboard_filter != NULL)
        (void)g_keyboard_filter(g_keyboard_filter_context, slot->input_slot, 0U, NULL, 0U);
    hid->user_data = NULL;
    slot_clear(slot);
}

void usb_hid_service_init(mt_core_link_t *link)
{
    mt_gamepad_init(&g_gamepads);
    memset(g_devices, 0, sizeof(g_devices));
    memset(g_controller_config, 0, sizeof(g_controller_config));
    memset(g_controller_output, 0, sizeof(g_controller_output));
    memset(g_controller_player, UINT8_MAX, sizeof(g_controller_player));
    g_config_loader = NULL;
    g_config_loader_context = NULL;
    g_action_handler = NULL;
    g_action_handler_context = NULL;
    g_report_handler = NULL;
    g_report_handler_context = NULL;
    memset(g_vendor_slots, 0, sizeof(g_vendor_slots));
    memset(g_release_guard, 0, sizeof(g_release_guard));
    memset(g_controller_tab, 0, sizeof(g_controller_tab));
    memset(g_menu_chord, 0, sizeof(g_menu_chord));
    memset(g_menu_navigation_guard, 0, sizeof(g_menu_navigation_guard));
    g_menu_capture = false;
    g_gamepad_preview = false;
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
    g_emit_deferred = 0U;
    g_active_core_id = 0U;
    g_keyboard_filter = NULL;
    g_keyboard_filter_context = NULL;
    mt_c64_hid_input_init(&g_c64_input, emit_active_input, link);
    mt_gameboy_hid_input_init(&g_gameboy_input, emit_active_input, link);
    mt_vectrex_hid_input_init(&g_vectrex_input, emit_active_input, link);
    mt_genesis_hid_input_init(&g_genesis_input, emit_active_input, link);
    mt_nes_hid_input_init(&g_nes_input, emit_active_input, link);
    mt_iigs_hid_init(&g_iigs_input,emit_active_input,link);
}

void usb_hid_service_set_keyboard_filter(mt_usb_keyboard_filter_fn filter,
                                         void *context)
{
    g_keyboard_filter = filter;
    g_keyboard_filter_context = context;
}

int usb_hid_service_start(void)
{
    struct usbh_bus *const bus = &g_usbhost_bus[CHERRYUSB_USB1_BUSID];
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
    g_power_shutdown_required = 1U;
    if (result == 0) {
        g_started = 1U;
        /* This CherryUSB revision discards usbh_hub_initialize() failures.
         * Do not apply connector power unless both hub handles exist. */
        if (bus->hub_mq == NULL || bus->hub_sem == NULL) {
            result = cherryusb_usb1_power_result();
            if (result == 0) result = -USB_ERR_IO;
        } else {
            result = cherryusb_usb1_host_power_start();
        }
    }
    if (result != 0) {
        cleanup_result = usb_hid_service_stop();
        if (cleanup_result != 0) result = cleanup_result;
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
        usb_xbox_one_stop();
        (void)usbh_deinitialize(CHERRYUSB_USB1_BUSID);
        /* Deinit retries power removal and may fail after our first success.
         * Retain its result, and retry while the PHY clock is still live. */
        power_result = cherryusb_usb1_power_stop_result();
        if (power_result != 0) {
            power_result = cherryusb_usb1_host_power_stop();
        }
        g_power_shutdown_required = (power_result != 0) ? 1U : 0U;
        g_started = 0U;
    }
    (void)mt_c64_hid_release_all(&g_c64_input);
    (void)mt_gameboy_hid_release_all(&g_gameboy_input);
    (void)mt_vectrex_hid_release_all(&g_vectrex_input);
    (void)mt_genesis_hid_release_all(&g_genesis_input);
    (void)mt_nes_hid_release_all(&g_nes_input);
    (void)mt_iigs_hid_release_all(&g_iigs_input);
    memset(g_slots, 0, sizeof(g_slots));
    memset(g_xinput_slots, 0, sizeof(g_xinput_slots));
    for (uint8_t i = 0U; i < MT_GAMEPAD_SLOTS; ++i) {
        disconnect_input_slot(i);
        mt_gamepad_disconnect(&g_gamepads, i);
    }
    memset(g_vendor_slots, 0, sizeof(g_vendor_slots));
    memset(g_release_guard, 0, sizeof(g_release_guard));
    if (power_result != 0) {
        g_power_shutdown_required = 1U;
        g_last_error = power_result;
    }
    return power_result;
}

void usb_hid_service_set_active_core(uint32_t core_id)
{
    g_core_ready = 0U;
    g_active_core_id = core_id;
    for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        g_release_guard[slot] = pad_active(&g_gamepads.raw[slot]) ? 1U : 0U;
        reset_menu_chord(slot);
    }
    refresh_controller_outputs(true, true);
    if (g_active_core_id == MT_IIGS_CORE_ID)
        mt_iigs_hid_reassert(&g_iigs_input);
    else if (g_active_core_id == MT_VECTREX_CORE_ID)
        mt_vectrex_hid_reassert(&g_vectrex_input);
    else if (g_active_core_id == MT_GBC_CORE_ID)
        mt_gameboy_hid_reassert(&g_gameboy_input);
    else if (g_active_core_id == MT_GENESIS_CORE_ID)
        mt_genesis_hid_reassert(&g_genesis_input);
    else if (g_active_core_id == MT_NES_CORE_ID)
        mt_nes_hid_reassert(&g_nes_input);
    else
        mt_c64_hid_reassert(&g_c64_input);
}

void usb_hid_service_set_core_ready(bool ready)
{
    const uint8_t next = ready ? 1U : 0U;
    if (next != 0U && g_core_ready == 0U) {
        g_core_ready = 1U;
        if (g_active_core_id == MT_IIGS_CORE_ID)
            mt_iigs_hid_reassert(&g_iigs_input);
        else if (g_active_core_id == MT_VECTREX_CORE_ID)
            mt_vectrex_hid_reassert(&g_vectrex_input);
        else if (g_active_core_id == MT_GBC_CORE_ID)
            mt_gameboy_hid_reassert(&g_gameboy_input);
        else if (g_active_core_id == MT_GENESIS_CORE_ID)
            mt_genesis_hid_reassert(&g_genesis_input);
        else if (g_active_core_id == MT_NES_CORE_ID)
            mt_nes_hid_reassert(&g_nes_input);
        else
            mt_c64_hid_reassert(&g_c64_input);
        poll_active_input();
    } else {
        g_core_ready = next;
    }
}

void usb_hid_service_poll(void)
{
    if (g_started != 0U) {
        cherryusb_host_poll(CHERRYUSB_USB1_BUSID);
        retry_reports();
        usb_xbox_one_poll();
    }
    poll_menu_chords();
    poll_active_input();
}

void usb_hid_service_poll_deferred(void)
{
    const uint8_t previous = g_emit_deferred;
    g_emit_deferred = 1U;
    usb_hid_service_poll();
    g_emit_deferred = previous;
}

void usb_hid_service_vectrex_controls(int controls[2][6])
{
    mt_vectrex_hid_controls(&g_vectrex_input, controls);
}

void cherryusb_background_poll(void)
{
    poll_active_input();
}

int usb_hid_service_set_joystick_port(uint8_t port)
{
    if (port != 1U && port != 2U) return -1;
    g_c64_input.joystick_port = port;
    refresh_controller_outputs(false, true);
    return 0;
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
    for (uint8_t i = 0U; i < MT_GAMEPAD_SLOTS; ++i)
        status->gamepads += g_vendor_slots[i];
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
    for (uint8_t index = 0U; index < CONFIG_USBHOST_MAX_XBOX_CLASS;
         ++index) {
        const mt_xinput_slot_t *const slot = &g_xinput_slots[index];
        if (slot->active == 0U) continue;
        xil_printf("usb1 xinput%u reports=%u state=%02x seen=%02x "
                   "changes=%u in_pending=%u led=%u out_try=%u "
                   "out_pending=%u submit_err=%u transfer_err=%u "
                   "last=%d out_last=%d raw=",
                   (unsigned int)index, (unsigned int)slot->reports,
                   (unsigned int)slot->decoded_state,
                   (unsigned int)slot->observed_state,
                   (unsigned int)slot->state_changes,
                   (unsigned int)slot->report_pending,
                   (unsigned int)slot->output_sent,
                   (unsigned int)slot->output_attempts,
                   (unsigned int)slot->output_pending,
                   (unsigned int)slot->submit_errors,
                   (unsigned int)slot->transfer_errors, slot->last_error,
                   slot->output_last_error);
        if (slot->last_packet_length == 0U) {
            xil_printf("-");
        } else {
            for (uint8_t byte = 0U; byte < slot->last_packet_length; ++byte) {
                xil_printf("%02x", (unsigned int)slot->last_packet[byte]);
            }
        }
        xil_printf("\r\n");
    }
    usb_xbox_one_dump_status();
    xil_printf("usb1 phy cached: setup_rc=%d disable_rc=%d drive_known=%u drive=%u "
               "vbus_known=%u vbus_valid=%u ulpi_err=%d timeouts=%u "
               "id=%04x:%04x func=%02x iface=%02x otg=%02x int=%02x "
               "viewport=%08x\r\n",
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
               (unsigned int)phy.viewport);
}

void usb_hid_service_set_menu_capture(bool captured)
{
    if (g_menu_capture == captured) return;
    g_menu_capture = captured;
    if(captured && g_active_core_id==MT_IIGS_CORE_ID) {
        g_iigs_input.mouse_x=0; g_iigs_input.mouse_y=0;
        mt_iigs_hid_mouse_report(&g_iigs_input,0,0,0U);
    }
    for (uint8_t i = 0U; i < MT_GAMEPAD_SLOTS; ++i) {
        g_menu_navigation_guard[i] = captured && pad_active(&g_gamepads.raw[i]) ? 1U : 0U;
        if (!captured) g_release_guard[i] = pad_active(&g_gamepads.raw[i]) ? 1U : 0U;
        deliver_gamepad(i);
    }
}

void usb_hid_service_gamepad_snapshot(uint8_t slot, mt_gamepad_state_t *raw,
                                      mt_gamepad_state_t *mapped)
{
    if (slot >= MT_GAMEPAD_SLOTS) return;
    if (raw != NULL) *raw = g_gamepads.raw[slot];
    if (mapped != NULL) mt_gamepad_mapped(&g_gamepads, slot, mapped);
}

bool usb_hid_service_set_gamepad_config(uint8_t slot, const mt_gamepad_config_t *config)
{
    if (slot >= MT_GAMEPAD_SLOTS || !mt_gamepad_config_valid(config)) return false;
    g_gamepads.config[slot] = *config;
    deliver_gamepad(slot);
    return true;
}

void usb_hid_service_get_gamepad_config(uint8_t slot, mt_gamepad_config_t *config)
{
    if (slot < MT_GAMEPAD_SLOTS && config != NULL) *config = g_gamepads.config[slot];
}

void usb_hid_service_set_gamepad_preview(bool preview)
{
    g_gamepad_preview = preview;
}

void usb_hid_service_set_controller_config_loader(mt_usb_controller_config_fn loader,
                                                  void *context)
{
    g_config_loader = loader;
    g_config_loader_context = context;
    refresh_controller_outputs(true, true);
}

bool usb_hid_service_controller_device(uint8_t slot, mt_controller_device_t *device)
{
    if (slot >= MT_GAMEPAD_SLOTS || device == NULL) return false;
    *device = g_devices[slot];
    return device->connected != 0U;
}

bool usb_hid_service_get_controller_config(uint8_t slot, mt_controller_config_t *config)
{
    if (slot >= MT_GAMEPAD_SLOTS || config == NULL || !g_devices[slot].connected) return false;
    *config = g_controller_config[slot];
    return true;
}

bool usb_hid_service_set_controller_config(uint8_t slot, const mt_controller_config_t *config)
{
    if (slot >= MT_GAMEPAD_SLOTS || !g_devices[slot].connected || config == NULL) return false;
    mt_controller_config_t configs[MT_GAMEPAD_SLOTS];
    memcpy(configs, g_controller_config, sizeof(configs));
    configs[slot] = *config;
    return usb_hid_service_set_controller_configs(configs);
}

bool usb_hid_service_validate_controller_configs(const mt_controller_config_t configs[MT_GAMEPAD_SLOTS])
{
    uint8_t explicit_ports = 0U;
    if (configs == NULL) return false;
    for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        if (!g_devices[slot].connected) continue;
        const mt_controller_config_t *config = &configs[slot];
        if (!mt_controls_config_valid(g_active_core_id, config)) return false;
        if (config->assignment == MT_CONTROLLER_PORT2 && controller_player_count() < 2U) return false;
        if (config->assignment == MT_CONTROLLER_PORT1 || config->assignment == MT_CONTROLLER_PORT2) {
            const uint8_t bit = (uint8_t)(1U << (config->assignment - 1U));
            if (explicit_ports & bit) return false;
            explicit_ports |= bit;
        }
    }
    return true;
}

bool usb_hid_service_set_controller_configs(const mt_controller_config_t configs[MT_GAMEPAD_SLOTS])
{
    if (!usb_hid_service_validate_controller_configs(configs)) return false;
    for (uint8_t slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        if (!g_devices[slot].connected) continue;
        if (g_controller_config[slot].assignment != configs[slot].assignment)
            g_controller_player[slot] = UINT8_MAX;
        g_controller_config[slot] = configs[slot];
        g_release_guard[slot] = pad_active(&g_gamepads.raw[slot]) ? 1U : 0U;
        reset_menu_chord(slot);
    }
    refresh_controller_outputs(false, false);
    return true;
}

void usb_hid_service_controller_snapshot(uint8_t slot, mt_gamepad_state_t *raw,
                                         mt_controls_output_t *output)
{
    if (slot >= MT_GAMEPAD_SLOTS) return;
    if (raw != NULL) *raw = g_gamepads.raw[slot];
    if (output != NULL) *output = g_controller_output[slot];
}

bool usb_hid_service_controller_menu_chord(uint8_t slot)
{
    return slot < MT_GAMEPAD_SLOTS && g_menu_chord[slot].state != MT_MENU_CHORD_IDLE;
}

uint8_t usb_hid_service_controller_player(uint8_t slot)
{
    return slot < MT_GAMEPAD_SLOTS ? g_controller_player[slot] : UINT8_MAX;
}

void usb_hid_service_set_controller_action_handler(mt_usb_controller_action_fn handler,
                                                    void *context)
{
    g_action_handler = handler;
    g_action_handler_context = context;
}

void usb_hid_service_set_controller_report_handler(mt_usb_controller_report_fn handler,
                                                    void *context)
{
    g_report_handler = handler;
    g_report_handler_context = context;
}
