/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute transport callbacks, routing and menu capture with real adapters. */
#include <assert.h>
#include <stdio.h>
#include "multitini_menu.h"
#include "../../firmware/platform/xilinx/usb_hid_service.c"
static XTime fake_now;
static unsigned submits;
static unsigned string_mode, string_requests;
static uint8_t serial_suffix = '1';
static uint32_t input_regs[MT_REGISTER_SPAN / 4U];
static uint16_t routed_code, routed_value;
static unsigned routed_events;
static uint32_t input_read(void *context, uint32_t offset)
{ (void)context; return input_regs[offset / 4U]; }
static void input_write(void *context, uint32_t offset, uint32_t value)
{
    (void)context;
    input_regs[offset / 4U] = value;
    if (offset == MT_REG_COMMAND_DOORBELL) {
        assert(input_regs[MT_REG_COMMAND_OPCODE / 4U] == MT_COMMAND_INPUT_EVENT);
        routed_code = (uint16_t)input_regs[MT_REG_COMMAND_ARG0 / 4U];
        routed_value = (uint16_t)input_regs[MT_REG_COMMAND_ARG1 / 4U];
        ++routed_events;
        input_regs[MT_REG_RESPONSE_STATUS / 4U] = MT_RESPONSE_OK;
        input_regs[MT_REG_RESPONSE_SEQ / 4U] = value;
    }
}
struct usbh_bus g_usbhost_bus[CONFIG_USBHOST_MAX_BUS];
void XTime_GetTime(XTime *value) { *value = fake_now; }
int xil_printf(const char *format, ...) { (void)format; return 0; }
int cherryusb_usb1_host_power_start(void) { return 0; }
int cherryusb_usb1_host_power_stop(void) { return 0; }
int cherryusb_usb1_power_result(void) { return 0; }
int cherryusb_usb1_power_stop_result(void) { return 0; }
uint32_t cherryusb_usb1_portsc(void) { return 0; }
void cherryusb_usb1_phy_status(cherryusb_usb1_phy_status_t *status) { memset(status, 0, sizeof(*status)); }
void cherryusb_host_poll(uint8_t bus) { (void)bus; }
void usb_xbox_one_poll(void) {}
void usb_xbox_one_stop(void) {}
void usb_xbox_one_dump_status(void) {}
int usbh_initialize(uint8_t bus, uintptr_t base, usbh_event_handler_t handler)
{ (void)bus; (void)base; (void)handler; return 0; }
int usbh_deinitialize(uint8_t bus) { (void)bus; return 0; }
int usbh_submit_urb(struct usbh_urb *urb)
{
    ++submits;
    if (urb->timeout != MT_CONTROLLER_STRING_TIMEOUT_MS) return 0;
    ++string_requests;
    assert(urb->setup->bRequest == USB_REQUEST_GET_DESCRIPTOR);
    assert(urb->setup->bmRequestType == 0x80U);
    assert(urb->transfer_buffer_length == 255U);
    assert(((uintptr_t)urb->transfer_buffer % CONFIG_USB_ALIGN_SIZE) == 0U);
    const unsigned index = urb->setup->wValue & 255U;
    assert(index == 0U || index == 9U || index == 7U);
    if (index == 0U) {
        assert(urb->setup->wIndex == 0U);
        if (string_mode == 2U) return -USB_ERR_TIMEOUT;
        const uint8_t languages[] = {4U, USB_DESCRIPTOR_TYPE_STRING, 0x0cU, 4U};
        memcpy(urb->transfer_buffer, languages, sizeof(languages));
        urb->actual_length = sizeof(languages);
    } else {
        assert(urb->setup->wIndex == (string_mode == 2U ? 0x0409U : 0x040cU));
        const char *value = index == 9U ? "My Pad" : "serial-";
        const size_t chars = strlen(value) + (index == 7U ? 1U : 0U);
        urb->transfer_buffer[0] = (uint8_t)(2U + chars * 2U);
        urb->transfer_buffer[1] = USB_DESCRIPTOR_TYPE_STRING;
        for (size_t i = 0U; i < chars; ++i)
            urb->transfer_buffer[2U + i * 2U] = i < strlen(value) ? (uint8_t)value[i] : serial_suffix;
        urb->actual_length = urb->transfer_buffer[0];
        if (string_mode == 3U) urb->actual_length = 2U; /* Truncated descriptor rejected. */
    }
    return 0;
}
int usb_osal_mutex_take(usb_osal_mutex_t mutex) { (void)mutex; return 0; }
int usb_osal_mutex_give(usb_osal_mutex_t mutex) { (void)mutex; return 0; }
int usbh_control_transfer(struct usbh_hubport *port, struct usb_setup_packet *setup, uint8_t *buffer)
{ (void)port; (void)setup; (void)buffer; return 0; }
int usbh_hid_get_report_descriptor(struct usbh_hid *hid, uint8_t *buffer, uint32_t length)
{ (void)hid; (void)buffer; (void)length; return 0; }
int usbh_hid_set_idle(struct usbh_hid *hid, uint8_t id, uint8_t duration)
{ (void)hid; (void)id; (void)duration; return 0; }
int usbh_hid_set_protocol(struct usbh_hid *hid, uint8_t protocol)
{ (void)hid; (void)protocol; return 0; }
int usbh_hid_parse_report_descriptor(const uint8_t *data, uint32_t size, struct usbh_hid_report_info *info)
{ (void)data; (void)size; memset(info, 0, sizeof(*info)); return 0; }

static void test_transport_slots(void)
{
    struct usbh_hubport port = {0};
    struct usb_endpoint_descriptor endpoint = {0};
    struct usbh_hid hid = {0};
    struct usbh_xbox xbox = {0};
    port.connected = true;
    endpoint.bEndpointAddress = 0x81U;
    endpoint.bmAttributes = USB_ENDPOINT_TYPE_INTERRUPT;
    endpoint.wMaxPacketSize = 64;
    hid.hport = &port; hid.intin = &endpoint;
    usb_hid_service_init(NULL);
    const int vendor = usb_gamepad_input_connect();
    assert(vendor == 0);
    usbh_hid_run(&hid);
    assert(g_slots[0].input_slot == 1U && (g_gamepads.allocated & 3U) == 3U);
    unsigned before = submits;
    report_complete(&g_slots[0], -USB_ERR_IO);
    assert(submits == before && g_slots[0].retry_armed && !g_slots[0].report_pending);
    fake_now = MT_HID_RETRY_TICKS - 1U;
    retry_reports(); assert(submits == before);
    ++fake_now;
    retry_reports(); assert(submits == before + 1U && g_slots[0].report_pending);
    /* A successful report rearms immediately. */
    report_complete(&g_slots[0], 8);
    assert(submits == before + 2U);
    xbox.hport = &port; xbox.intin = &endpoint;
    usbh_xbox_run(&xbox);
    before = submits;
    xinput_report_complete(&g_xinput_slots[0], -USB_ERR_IO);
    assert(submits == before && g_xinput_slots[0].retry_armed);
    fake_now += MT_HID_RETRY_TICKS;
    retry_reports(); assert(submits == before + 1U);
    assert(g_xinput_slots[0].input_slot == 8U);

    /* A second Xbox 360 owns a distinct class and shared input slot. */
    struct usbh_xbox second = xbox;
    second.minor = 1U;
    usbh_xbox_run(&second);
    assert(g_xinput_slots[1].active && g_xinput_slots[1].input_slot == 2U);
    const uint8_t packet[20] = {0, 20, 0, 0x14}; /* A and Guide */
    memcpy(g_xinput_reports[1], packet, sizeof(packet));
    xinput_report_complete(&g_xinput_slots[1], sizeof(packet));
    assert(g_gamepads.raw[2].buttons == (1U | (1U << MT_USB_PAD_HOME)));
    assert(g_gamepads.raw[8].buttons == 0U);
    usbh_xbox_stop(&second);
    /* Shared slot disconnect cannot release either neighboring transport. */
    usb_gamepad_input_disconnect((uint8_t)vendor);
    assert(!g_gamepads.raw[vendor].connected && (g_gamepads.allocated & 0x102U) == 0x102U);
    usbh_hid_stop(&hid); usbh_xbox_stop(&xbox);
    assert(g_gamepads.allocated == 0U);
    puts("PASS USB HID/XInput backoff and independent mixed transport slots");
}

static unsigned menu_down, screenshot_events, raw_events, printscreen_seen;
static bool observe_keys(void *context, uint8_t slot, uint8_t modifiers,
                          const uint8_t *keys, size_t count)
{
    (void)context; (void)slot; (void)modifiers;
    menu_down = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (keys[i] == 0x45U) ++menu_down;
        if (keys[i] == 0x46U) ++printscreen_seen;
    }
    return false;
}
static void observe_action(void *context, unsigned action)
{
    (void)context;
    assert(action == MT_CONTROL_SCREENSHOT || action == MT_CONTROL_SCREENSHOT_OUTPUT);
    ++screenshot_events;
}
static void observe_raw(void *context, uint8_t slot, const mt_gamepad_state_t *raw)
{
    (void)context; (void)slot;
    assert(raw->connected);
    ++raw_events;
}
static void buttons(uint8_t slot, uint32_t held)
{
    mt_gamepad_report_t report = {0};
    report.buttons_valid = 1U; report.buttons = held;
    usb_gamepad_input_report(slot, &report, 8U, 0U);
}

static void test_controller_routing(void)
{
    mt_core_link_t link;
    mt_core_link_init(&link, NULL, input_read, input_write);
    usb_hid_service_init(&link);
    usb_hid_service_set_active_core(MT_C64_CORE_ID);
    usb_hid_service_set_keyboard_filter(observe_keys, NULL);
    usb_hid_service_set_controller_action_handler(observe_action, NULL);
    usb_hid_service_set_controller_report_handler(observe_raw, NULL);
    struct usbh_hubport first_port = {0}, second_port = {0};
    first_port.port = 1U; second_port.port = 2U;
    first_port.device_desc.idVendor = second_port.device_desc.idVendor = 0x045eU;
    first_port.device_desc.idProduct = second_port.device_desc.idProduct = 0x02eaU;
    uint8_t first = (uint8_t)usb_gamepad_input_connect();
    uint8_t second = (uint8_t)usb_gamepad_input_connect();
    usb_gamepad_input_identify(first, &first_port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    usb_gamepad_input_identify(second, &second_port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    assert(!mt_controls_identity_equal(&g_devices[first].identity, &g_devices[second].identity));
    assert(mt_controls_identity_model_equal(&g_devices[first].identity, &g_devices[second].identity));
    usb_hid_service_set_core_ready(true);
    buttons(first, 1U); buttons(second, 2U);
    assert(g_c64_input.emitted_joystick[1] == 0x10U);
    assert(g_c64_input.emitted_joystick[0] == 0x20U);
    assert(g_controller_player[first] == 1U && g_controller_player[second] == 0U);
    buttons(first, 0xc0U); /* A brief pair does not open the menu. */
    assert(menu_down == 0U);
    buttons(first, 0U);
    buttons(first, UINT32_C(1) << MT_USB_PAD_HOME);
    assert(menu_down == 1U && g_c64_input.emitted_joystick[1] == 0U);
    buttons(first, 0U);
    usb_hid_service_set_menu_capture(true);
    assert(g_c64_input.emitted_joystick[0] == 0U);
    usb_hid_service_set_menu_capture(false);
    assert(g_c64_input.emitted_joystick[0] == 0U); /* Held action cannot leak after close. */
    buttons(second, 0U); buttons(second, 2U);
    assert(g_c64_input.emitted_joystick[0] == 0x20U);

    usb_hid_service_set_active_core(MT_VECTREX_CORE_ID);
    usb_hid_service_set_core_ready(true);
    buttons(first, 0U); buttons(second, 0U);
    buttons(first, 1U); buttons(second, 2U);
    int controls[2][6];
    usb_hid_service_vectrex_controls(controls);
    assert(controls[0][2] == 1 && controls[1][3] == 1);
    usb_gamepad_input_disconnect(first);
    usb_hid_service_vectrex_controls(controls);
    assert(controls[0][2] == 0 && controls[1][3] == 1 && g_controller_player[second] == 1U);

    first = (uint8_t)usb_gamepad_input_connect();
    usb_gamepad_input_identify(first, &first_port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    usb_hid_service_set_active_core(MT_GENESIS_CORE_ID);
    usb_hid_service_set_core_ready(true);
    buttons(first, 0U); buttons(second, 0U);
    buttons(first, 0x24U);
    assert(routed_code == 0x3000U && routed_value == (MT_GENESIS_C | MT_GENESIS_Z));
    assert(emit_active_input(&link, 0x1000U, 1U) == -1);
    mt_controller_config_t config = g_controller_config[first];
    config.binding[MT_CONTROL_C] = 32U;
    config.assignment = MT_CONTROLLER_PORT1;
    assert(usb_hid_service_set_controller_config(first, &config));
    mt_controller_config_t conflict = g_controller_config[second];
    conflict.assignment = MT_CONTROLLER_PORT1;
    assert(!usb_hid_service_set_controller_config(second, &conflict));
    buttons(first, 0U); buttons(first, UINT32_C(1) << 31U);
    assert(routed_value == MT_GENESIS_C);
    config.binding[MT_CONTROL_SCREENSHOT] = 1U;
    assert(usb_hid_service_set_controller_config(first, &config));
    buttons(first, 0U);
    const unsigned before = screenshot_events, reports = raw_events;
    buttons(first, 1U); buttons(first, 1U); buttons(first, 0U);
    assert(screenshot_events == before + 1U && raw_events == reports + 3U);
    assert(g_genesis_input.gamepad[first] == 0U);
    usb_hid_service_set_gamepad_preview(true);
    buttons(first, 1U); buttons(first, 0U);
    assert(screenshot_events == before + 1U);
    usb_hid_service_set_gamepad_preview(false);

    usb_hid_service_set_active_core(MT_GBC_CORE_ID);
    usb_hid_service_set_core_ready(true);
    buttons(first, 0U); buttons(second, 0U);
    buttons(first, 1U); buttons(second, 2U);
    assert(g_controller_player[first] == 0U && g_controller_player[second] == UINT8_MAX);
    assert(g_gameboy_input.gamepads[first] == 0x10U && g_gameboy_input.gamepads[second] == 0U);
    puts("PASS controller identity, two ports, stable ownership, Home and screenshot capture");
}

static void test_identity_strings_and_spare(void)
{
    usb_hid_service_init(NULL);
    usb_hid_service_set_active_core(MT_VECTREX_CORE_ID);
    struct usb_setup_packet setup = {0};
    struct usbh_hubport port = {0};
    port.setup = &setup; port.connected = true; port.port = 1U;
    port.device_desc.idVendor = 0x045eU; port.device_desc.idProduct = 0x02eaU;
    port.device_desc.iProduct = 9U; port.device_desc.iSerialNumber = 7U;
    string_mode = 1U; string_requests = 0U;
    uint8_t first = (uint8_t)usb_gamepad_input_connect();
    usb_gamepad_input_identify(first, &port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    assert(string_requests == 3U && strcmp(g_devices[first].name, "My Pad") == 0);
    const mt_controller_identity_t identity = g_devices[first].identity;
    assert(identity.serial_hash != 0U && port.ep0_urb.timeout == 0U);
    uint8_t second = (uint8_t)usb_gamepad_input_connect();
    serial_suffix = '2'; port.port = 2U;
    usb_gamepad_input_identify(second, &port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    assert(!mt_controls_identity_equal(&identity, &g_devices[second].identity));
    assert(g_controller_player[first] == 0U && g_controller_player[second] == 1U);
    uint8_t spare = (uint8_t)usb_gamepad_input_connect();
    serial_suffix = '3'; port.port = 3U; string_mode = 2U;
    usb_gamepad_input_identify(spare, &port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    assert(strcmp(g_devices[spare].name, "My Pad") == 0); /* Language-list failure falls back. */
    assert(g_controller_player[spare] == UINT8_MAX);
    usb_gamepad_input_disconnect(first);
    assert(g_controller_player[second] == 1U && g_controller_player[spare] == 0U);
    first = (uint8_t)usb_gamepad_input_connect();
    serial_suffix = '1'; port.port = 4U; string_mode = 1U;
    usb_gamepad_input_identify(first, &port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    assert(mt_controls_identity_equal(&identity, &g_devices[first].identity));
    assert(g_controller_player[first] == UINT8_MAX && g_controller_player[second] == 1U);
    usb_gamepad_input_disconnect(first);
    first = (uint8_t)usb_gamepad_input_connect();
    string_mode = 3U;
    usb_gamepad_input_identify(first, &port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    assert(g_devices[first].identity.serial_hash == 0U);
    assert(strcmp(g_devices[first].name, "Xbox One 045E:02EA") == 0);
    /* Saved profiles can conflict after an absent controller is replaced.
     * Reconnecting the old owner must not silence its live replacement. */
    g_controller_config[second].assignment = MT_CONTROLLER_PORT2;
    g_controller_config[spare].assignment = MT_CONTROLLER_PORT1;
    refresh_controller_outputs(false, false);
    g_controller_config[first].assignment = MT_CONTROLLER_PORT1;
    refresh_controller_outputs(false, false);
    assert(g_controller_player[first] == UINT8_MAX && g_controller_player[spare] == 0U);
    usb_gamepad_input_disconnect(spare);
    assert(g_controller_player[first] == 0U && g_controller_player[second] == 1U);
    spare = (uint8_t)usb_gamepad_input_connect();
    usb_gamepad_input_identify(spare, &port, 0U, MT_CONTROLLER_TRANSPORT_XBOXONE);
    g_controller_config[spare].assignment = MT_CONTROLLER_PORT1;
    refresh_controller_outputs(false, false);
    assert(g_controller_player[first] == 0U && g_controller_player[spare] == UINT8_MAX);
    string_mode = 0U;
    puts("PASS optional descriptor language/product/serial reads and stable spare-controller ownership");
}

static void test_hid_fields(void)
{
    static const uint8_t descriptor[] = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,
        0x85, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x10,
        0x15, 0, 0x25, 1, 0x75, 1, 0x95, 16, 0x81, 2,
        0x85, 0x02, 0x05, 1, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
        0x15, 0, 0x26, 255, 0, 0x75, 8, 0x95, 4, 0x81, 2,
        0x85, 0x03, 0x05, 9, 0x19, 17, 0x29, 32,
        0x15, 0, 0x25, 1, 0x75, 1, 0x95, 16, 0x81, 2, 0xc0
    };
    usb_hid_service_init(NULL);
    mt_hid_slot_t slot = {0};
    slot.input_slot = (uint8_t)mt_gamepad_connect(&g_gamepads, 0U);
    assert(mt_usb_parse_gamepad_descriptor(descriptor, sizeof(descriptor), &slot.report_info) == 0);
    assert(slot.report_info.using_report_id && slot.report_info.report_item_count == 36U);
    const uint8_t report1[] = {1, 0xc0, 3}; /* 7,8,9,10 remain distinct */
    process_gamepad_report(&slot, report1, sizeof(report1));
    assert(g_gamepads.raw[0].buttons == 0x3c0U);
    const uint8_t release7[] = {1, 0x80, 3};
    process_gamepad_report(&slot, release7, sizeof(release7));
    assert(g_gamepads.raw[0].buttons == 0x380U);
    const uint8_t report2[] = {2, 128, 128, 0, 255};
    process_gamepad_report(&slot, report2, sizeof(report2));
    assert(g_gamepads.raw[0].buttons == 0x380U);
    assert(g_gamepads.raw[0].axis_valid_mask == 0x27U);
    assert(g_gamepads.raw[0].axis[MT_GAMEPAD_AXIS_Z] == INT16_MIN);
    assert(g_gamepads.raw[0].axis[MT_GAMEPAD_AXIS_RZ] == INT16_MAX);
    const uint8_t report3[] = {3, 0, 0x80};
    process_gamepad_report(&slot, report3, sizeof(report3));
    assert(g_gamepads.raw[0].buttons == UINT32_C(0x80000380));
    const uint8_t clear1[] = {1, 0, 0};
    process_gamepad_report(&slot, clear1, sizeof(clear1));
    assert(g_gamepads.raw[0].buttons == UINT32_C(0x80000000));
    const uint8_t truncated[] = {0x05};
    assert(mt_usb_parse_gamepad_descriptor(truncated, sizeof(truncated), &slot.report_info) < 0);
    const uint8_t underflow[] = {0xb4};
    assert(mt_usb_parse_gamepad_descriptor(underflow, sizeof(underflow), &slot.report_info) < 0);
    puts("PASS real HID descriptor parsing: separate report IDs, usages and 32 buttons");
}

static void test_keyboard_screenshot(void)
{
    usb_hid_service_init(NULL);
    usb_hid_service_set_keyboard_filter(observe_keys, NULL);
    struct usbh_hubport port = {0};
    struct usb_endpoint_descriptor endpoint = {0};
    struct usbh_hid hid = {0};
    hid.hport = &port; hid.intin = &endpoint;
    hid.protocol = HID_PROTOCOL_KEYBOARD;
    port.config.intf[0].altsetting[0].intf_desc.bInterfaceSubClass = HID_SUBCLASS_BOOTIF;
    usbh_hid_run(&hid);
    uint8_t *report = g_reports[0];
    report[0] = 2U; report[2] = 0x46U; report[3] = 4U;
    const unsigned before = printscreen_seen;
    report_complete(&g_slots[0], 8);
    assert(printscreen_seen == before + 1U);
    assert((g_c64_input.usages[0][0] & (1U << 4U)) != 0U);
    assert((g_c64_input.usages[0][0x46U / 8U] & (1U << (0x46U % 8U))) == 0U);
    assert((g_c64_input.usages[0][0xe1U / 8U] & (1U << (0xe1U % 8U))) != 0U);
    puts("PASS PrintScreen reaches system handler without losing held game keys");
}

typedef struct {
    mt_menu_t menu;
    mt_menu_view_t view;
    bool held[MT_GAMEPAD_SLOTS][256];
    unsigned reports, tab_presses, menu_presses;
} navigation_fixture_t;

static bool navigate_menu(void *context, uint8_t slot, uint8_t modifiers,
                          const uint8_t *keys, size_t count)
{
    navigation_fixture_t *fixture = context;
    bool next[256] = {false};
    assert(slot < MT_GAMEPAD_SLOTS && count <= 7U);
    ++fixture->reports;
    for (size_t i = 0U; i < count; ++i) {
        next[keys[i]] = true;
        if (keys[i] == MT_MENU_HID_F12) assert(count == 1U && modifiers == 0U);
    }
    for (unsigned usage = 0U; usage < 256U; ++usage) {
        if (next[usage] == fixture->held[slot][usage]) continue;
        if (usage == MT_MENU_HID_TAB && next[usage]) ++fixture->tab_presses;
        if (usage == MT_MENU_HID_F12 && next[usage]) ++fixture->menu_presses;
        (void)mt_menu_key_with_modifiers(&fixture->menu, &fixture->view,
            (uint8_t)usage, next[usage], modifiers);
    }
    memcpy(fixture->held[slot], next, sizeof(next));
    usb_hid_service_set_menu_capture(fixture->menu.visible);
    return fixture->menu.visible;
}

static uint8_t chord_controller(uint8_t transport, uint32_t present, uint8_t port_number)
{
    struct usbh_hubport port = {0};
    port.port = port_number;
    const uint8_t slot = (uint8_t)usb_gamepad_input_connect();
    identify_controller(slot, &port, 0U, transport, 1U, present, 0U);
    return slot;
}

static void assert_controller_neutral(uint32_t core, uint8_t slot)
{
    if (core == MT_C64_CORE_ID) assert(g_c64_input.joysticks[slot] == 0U);
    else if (core == MT_GBC_CORE_ID) assert(g_gameboy_input.gamepads[slot] == 0U);
    else if (core == MT_GENESIS_CORE_ID) assert(g_genesis_input.gamepad[slot] == 0U);
    else if (core == MT_VECTREX_CORE_ID)
        assert(g_vectrex_input.pad_buttons[slot] == 0U &&
            g_vectrex_input.pad_x[slot] == 0 && g_vectrex_input.pad_y[slot] == 0);
}

static void test_menu_chord(void)
{
    static const uint32_t cores[] = {0U, MT_C64_CORE_ID, MT_GBC_CORE_ID,
        MT_GENESIS_CORE_ID, MT_VECTREX_CORE_ID};
    for (unsigned machine = 0U; machine < sizeof(cores) / sizeof(cores[0]); ++machine) {
        for (unsigned layout = 0U; layout < 5U; ++layout) {
            const uint8_t transport = layout < 2U ? MT_CONTROLLER_TRANSPORT_HID :
                (uint8_t)(layout - 1U);
            const uint32_t pair = layout == 1U ? 0x300U : 0xc0U;
            const uint32_t select = layout == 1U ? 0x100U : 0x40U;
            navigation_fixture_t fixture = {0};
            mt_core_link_t link;
            mt_core_link_init(&link, NULL, input_read, input_write);
            usb_hid_service_init(&link);
            usb_hid_service_set_active_core(cores[machine]);
            usb_hid_service_set_core_ready(true);
            mt_menu_init(&fixture.menu, &fixture.view);
            usb_hid_service_set_keyboard_filter(navigate_menu, &fixture);
            const uint8_t slot = chord_controller(transport,
                layout == 0U ? 0xffU : 0x3ffU, 1U);
            /* Physical detection must survive game remapping, even on cores
             * that do not have native Select/Start actions. */
            if (cores[machine] != 0U) {
                mt_controller_config_t config = g_controller_config[slot];
                config.binding[MT_CONTROL_A] = MT_CONTROL_SOURCE_BUTTON +
                    (layout == 1U ? 8U : 6U);
                assert(usb_hid_service_set_controller_config(slot, &config));
            }
            buttons(slot, 0U);
            buttons(slot, select);
            assert(g_menu_chord[slot].state == MT_MENU_CHORD_IDLE);
            if (cores[machine] == MT_C64_CORE_ID) assert(g_c64_input.joysticks[slot] == 0x10U);
            else if (cores[machine] == MT_GBC_CORE_ID) assert((g_gameboy_input.gamepads[slot] & 0x10U) != 0U);
            else if (cores[machine] == MT_GENESIS_CORE_ID) assert((g_genesis_input.gamepad[slot] & MT_GENESIS_A) != 0U);
            else if (cores[machine] == MT_VECTREX_CORE_ID) assert(g_vectrex_input.pad_buttons[slot] == 1U);
            fake_now += MT_MENU_CHORD_TICKS * 2U;
            usb_hid_service_poll();
            assert(fixture.menu_presses == 0U); /* Solo Select has no long-press action. */
            buttons(slot, 0U);
            const XTime started = fake_now;
            buttons(slot, pair | 0x11U); /* Enter and L must not accompany F12. */
            assert_controller_neutral(cores[machine], slot);
            fake_now = started + MT_MENU_CHORD_TICKS - 1U;
            usb_hid_service_poll();
            assert(!fixture.menu.visible && fixture.menu_presses == 0U);
            ++fake_now;
            const unsigned before = routed_events;
            usb_hid_service_poll_deferred(); /* No new HID report is required. */
            assert(routed_events == before);
            assert(fixture.menu.visible && fixture.menu_presses == 1U);
            assert(!fixture.held[slot][MT_MENU_HID_F12] && fixture.tab_presses == 0U);
            fake_now += MT_MENU_CHORD_TICKS * 4U;
            usb_hid_service_poll();
            buttons(slot, select | 0x11U);
            buttons(slot, pair | 0x11U);
            usb_hid_service_poll();
            assert(fixture.menu_presses == 1U); /* Both must be released to rearm. */
            buttons(slot, 0x11U);
            assert(fixture.tab_presses == 0U); /* Other held opener controls need neutral. */
            buttons(slot, 0U);
            buttons(slot, 0x20U);
            assert(fixture.tab_presses == 1U && fixture.menu.page == MT_MENU_PAGE_MEDIA);
            buttons(slot, 0U);
            buttons(slot, pair);
            fake_now += MT_MENU_CHORD_TICKS;
            usb_hid_service_poll();
            assert(!fixture.menu.visible && fixture.menu_presses == 2U);
            buttons(slot, select);
            assert_controller_neutral(cores[machine], slot);
            buttons(slot, 0U);
            buttons(slot, select);
            assert(g_menu_chord[slot].state == MT_MENU_CHORD_IDLE &&
                g_release_guard[slot] == 0U);
            usb_gamepad_input_disconnect(slot);
        }
    }
    puts("PASS 500 ms menu chord across supervisor/all cores and eight/ten-button HID/vendor pads");
}

static void test_menu_chord_guards(void)
{
    navigation_fixture_t fixture = {0};
    usb_hid_service_init(NULL);
    usb_hid_service_set_active_core(MT_GBC_CORE_ID);
    mt_menu_init(&fixture.menu, &fixture.view);
    usb_hid_service_set_keyboard_filter(navigate_menu, &fixture);
    usb_hid_service_set_controller_report_handler(observe_raw, NULL);
    uint8_t first = chord_controller(MT_CONTROLLER_TRANSPORT_XBOXONE, 0x7ffU, 1U);
    const uint8_t second = chord_controller(MT_CONTROLLER_TRANSPORT_XBOXONE, 0x7ffU, 2U);
    buttons(first, 0x40U); buttons(second, 0x80U);
    fake_now += MT_MENU_CHORD_TICKS;
    usb_hid_service_poll();
    assert(fixture.menu_presses == 0U); /* Separate controllers cannot form a chord. */
    buttons(first, 0xc0U);
    fake_now += MT_MENU_CHORD_TICKS - 1U;
    buttons(first, 0x40U); /* A broken hold is consumed until both release. */
    fake_now += MT_MENU_CHORD_TICKS;
    usb_hid_service_poll();
    assert(fixture.menu_presses == 0U);
    buttons(first, 0U); buttons(first, 0xc0U);
    usb_hid_service_set_active_core(MT_C64_CORE_ID);
    fake_now += MT_MENU_CHORD_TICKS;
    usb_hid_service_poll();
    assert(fixture.menu_presses == 0U); /* A core transition cancels an old hold. */
    buttons(first, 0U); buttons(first, 0xc0U);
    usb_gamepad_input_disconnect(first);
    fake_now += MT_MENU_CHORD_TICKS;
    usb_hid_service_poll();
    assert(fixture.menu_presses == 0U);
    first = chord_controller(MT_CONTROLLER_TRANSPORT_XBOXONE, 0x7ffU, 1U);
    buttons(first, 0U); buttons(first, 0xc0U);
    fake_now += MT_MENU_CHORD_TICKS;
    usb_hid_service_poll();
    assert(fixture.menu_presses == 1U && fixture.menu.visible);
    buttons(first, 0U); buttons(second, 0U);

    /* F12 escapes assignment, test and stick editing despite preview capture. */
    for (unsigned mode = 0U; mode < 3U; ++mode) {
        fixture.menu.page = MT_MENU_PAGE_INPUT;
        fixture.menu.gamepad_edit = true;
        fixture.menu.gamepad_capture = mode == 0U;
        fixture.menu.gamepad_test = mode == 1U;
        fixture.menu.gamepad_sticks = mode == 2U;
        usb_hid_service_set_gamepad_preview(true);
        const unsigned reports = fixture.reports, presses = fixture.menu_presses;
        const unsigned raw_reports = raw_events;
        buttons(first, 0xc0U);
        assert(fixture.reports == reports);
        assert(raw_events == raw_reports && usb_hid_service_controller_menu_chord(first));
        mt_gamepad_state_t raw;
        usb_hid_service_controller_snapshot(first, &raw, NULL);
        assert(raw.buttons == 0xc0U); /* Diagnostics retain the actual held pair. */
        fake_now += MT_MENU_CHORD_TICKS;
        usb_hid_service_poll();
        assert(fixture.menu_presses == presses + 1U && fixture.menu.visible);
        assert(!fixture.menu.gamepad_capture && !fixture.menu.gamepad_test &&
            !fixture.menu.gamepad_sticks && fixture.menu.gamepad_edit);
        assert(!fixture.held[first][MT_MENU_HID_F12]);
        buttons(first, 0U);
        assert(!usb_hid_service_controller_menu_chord(first));
        usb_hid_service_set_gamepad_preview(false);
    }
    puts("PASS menu chord cancellation, disconnect/core guards, independent slots and preview escape");
}

static void test_shoulder_navigation(void)
{
    for (uint8_t transport = MT_CONTROLLER_TRANSPORT_HID;
         transport <= MT_CONTROLLER_TRANSPORT_PS4; ++transport) {
        navigation_fixture_t fixture = {0};
        mt_core_link_t link;
        mt_core_link_init(&link, NULL, input_read, input_write);
        usb_hid_service_init(&link);
        usb_hid_service_set_active_core(MT_GENESIS_CORE_ID);
        usb_hid_service_set_core_ready(true);
        mt_menu_init(&fixture.menu, &fixture.view);
        fixture.menu.visible = true;
        usb_hid_service_set_keyboard_filter(navigate_menu, &fixture);
        usb_hid_service_set_menu_capture(true);
        struct usbh_hubport port = {0};
        port.port = 1U;
        const uint8_t first = (uint8_t)usb_gamepad_input_connect();
        usb_gamepad_input_identify(first, &port, 0U, transport);
        const uint8_t second = (uint8_t)usb_gamepad_input_connect();
        port.port = 2U;
        usb_gamepad_input_identify(second, &port, 0U, transport);

        buttons(first, 0x10U); /* LB/L1 wraps backwards. */
        assert(fixture.menu.page == MT_MENU_PAGE_SYSTEM && fixture.tab_presses == 1U);
        buttons(first, 0x10U);
        assert(fixture.menu.page == MT_MENU_PAGE_SYSTEM && fixture.tab_presses == 1U);
        buttons(first, 0x20U); /* Direct left-to-right change still has a Tab edge. */
        assert(fixture.menu.page == MT_MENU_PAGE_CORES && fixture.tab_presses == 2U);
        buttons(first, 0x30U); /* Both held cancel; releasing one restores direction. */
        assert(fixture.menu.page == MT_MENU_PAGE_CORES && fixture.tab_presses == 2U);
        buttons(first, 0x10U);
        assert(fixture.menu.page == MT_MENU_PAGE_SYSTEM && fixture.tab_presses == 3U);
        buttons(first, 0U);
        buttons(second, 0x20U);
        assert(fixture.menu.page == MT_MENU_PAGE_CORES && fixture.tab_presses == 4U);
        assert(g_genesis_input.gamepad[first] == 0U && g_genesis_input.gamepad[second] == 0U);

        buttons(second, 0U);
        fixture.menu.page = MT_MENU_PAGE_INPUT;
        fixture.menu.gamepad_edit = true;
        fixture.menu.gamepad_capture = true;
        usb_hid_service_set_gamepad_preview(true);
        const unsigned before = fixture.reports;
        buttons(first, 0x20U); buttons(first, 0U);
        assert(fixture.reports == before && fixture.menu.page == MT_MENU_PAGE_INPUT);
        assert(fixture.menu.gamepad_capture); /* Shoulders remain assignable, not Tab/Skip. */
        fixture.menu.gamepad_edit = fixture.menu.gamepad_capture = false;
        usb_hid_service_set_gamepad_preview(false);

        buttons(first, 0x20U);
        assert(fixture.menu.page == MT_MENU_PAGE_CORE_CONFIG);
        usb_gamepad_input_disconnect(first);
        assert(!fixture.held[first][MT_MENU_HID_TAB] && g_controller_tab[first] == 0U);

        fixture.menu.visible = false;
        usb_hid_service_set_menu_capture(false);
        const unsigned tabs = fixture.tab_presses;
        buttons(second, 0x10U);
        assert(fixture.tab_presses == tabs && !fixture.menu.visible);
        assert(g_genesis_input.gamepad[second] == MT_GENESIS_Y);
        buttons(second, 0x20U);
        assert(g_genesis_input.gamepad[second] == MT_GENESIS_Z);
        assert(fixture.tab_presses == tabs);
        buttons(second, 0U);
        usb_gamepad_input_disconnect(second);
    }
    puts("PASS controller shoulders navigate menu sections without changing capture or gameplay");
}

static void test_iigs_input_transport(void)
{
    mt_core_link_t link;
    mt_core_link_init(&link,NULL,input_read,input_write);
    usb_hid_service_init(&link);
    usb_hid_service_set_active_core(MT_IIGS_CORE_ID);
    usb_hid_service_set_core_ready(true);
    struct usbh_hubport port={0};
    struct usb_endpoint_descriptor endpoint={0};
    struct usbh_hid hid={0};
    port.config.intf[0].altsetting[0].intf_desc.bInterfaceSubClass=HID_SUBCLASS_BOOTIF;
    port.connected=true;
    endpoint.bEndpointAddress=0x81U;endpoint.bmAttributes=USB_ENDPOINT_TYPE_INTERRUPT;endpoint.wMaxPacketSize=8U;
    hid.hport=&port;hid.intin=&endpoint;hid.protocol=HID_PROTOCOL_MOUSE;
    usbh_hid_run(&hid);
    assert(g_slots[0].mouse && !g_slots[0].gamepad);
    g_reports[0][0]=1U;g_reports[0][1]=4U;g_reports[0][2]=(uint8_t)-5;
    report_complete(&g_slots[0],3);usb_hid_service_poll();
    assert(routed_code==0x4001U && (routed_value&255U)==5U && g_iigs_input.mouse_emitted_buttons==1U);
    usb_hid_service_set_menu_capture(true);usb_hid_service_poll();
    assert(g_iigs_input.mouse_emitted_buttons==0U);
    usb_hid_service_set_menu_capture(false);
    g_reports[0][0]=1U;g_reports[0][1]=4U;g_reports[0][2]=(uint8_t)-5;
    report_complete(&g_slots[0],3);usb_hid_service_poll();
    assert(g_iigs_input.mouse_emitted_buttons==1U);
    usbh_hid_stop(&hid);assert(g_iigs_input.mouse_emitted_buttons==0U);
    const uint8_t pad=(uint8_t)usb_gamepad_input_connect();
    port.port=1U;usb_gamepad_input_identify(pad,&port,0U,MT_CONTROLLER_TRANSPORT_XBOXONE);
    assert(g_controller_player[pad]==0U);
    buttons(pad,1U);assert(g_iigs_input.emitted_buttons==0x10U);
    buttons(pad,0U);usb_gamepad_input_disconnect(pad);assert(g_iigs_input.emitted_buttons==0U);
    puts("PASS Apple IIgs USB boot mouse capture/disconnect and one-player gamepad routing");
}

static void test_nes_input_transport(void)
{
    mt_core_link_t link;
    mt_core_link_init(&link,NULL,input_read,input_write);
    usb_hid_service_init(&link);
    usb_hid_service_set_active_core(MT_NES_CORE_ID);
    usb_hid_service_set_core_ready(true);
    struct usbh_hubport port={0};port.port=1U;
    const uint8_t first=(uint8_t)usb_gamepad_input_connect();
    usb_gamepad_input_identify(first,&port,0U,MT_CONTROLLER_TRANSPORT_XBOXONE);
    port.port=2U;
    const uint8_t second=(uint8_t)usb_gamepad_input_connect();
    usb_gamepad_input_identify(second,&port,0U,MT_CONTROLLER_TRANSPORT_XBOXONE);
    assert(g_controller_player[first]==0U && g_controller_player[second]==1U);
    buttons(first,1U);buttons(second,2U);
    assert(g_nes_input.emitted[0]==(1U<<MT_NES_BUTTON_A));
    assert(g_nes_input.emitted[1]==(1U<<MT_NES_BUTTON_B));
    assert(routed_code==0x5001U && routed_value==(1U<<MT_NES_BUTTON_B));
    usb_hid_service_set_menu_capture(true);
    assert(g_nes_input.emitted[0]==0U && g_nes_input.emitted[1]==0U);
    buttons(first,0U);buttons(second,0U);
    usb_hid_service_set_menu_capture(false);
    const uint8_t keys[]={0x1dU,0x28U};
    submit_keyboard_state(3U,0x20U,keys,sizeof(keys));
    assert(g_nes_input.emitted[0]==((1U<<MT_NES_BUTTON_A)|(1U<<MT_NES_BUTTON_SELECT)|(1U<<MT_NES_BUTTON_START)));
    submit_keyboard_state(3U,0U,NULL,0U);
    usb_gamepad_input_disconnect(first);usb_gamepad_input_disconnect(second);
    assert(g_nes_input.emitted[0]==0U && g_nes_input.emitted[1]==0U);
    puts("PASS NES two-player USB routing, keyboard fallback and menu capture release");
}

int main(void)
{
    test_transport_slots();
    test_controller_routing();
    test_identity_strings_and_spare();
    test_hid_fields();
    test_keyboard_screenshot();
    test_menu_chord();
    test_menu_chord_guards();
    test_shoulder_navigation();
    test_iigs_input_transport();
    test_nes_input_transport();
    return 0;
}
