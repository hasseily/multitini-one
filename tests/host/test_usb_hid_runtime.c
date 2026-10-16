/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute transport callbacks, routing and menu capture with real adapters. */
#include <assert.h>
#include <stdio.h>
#include "multitini_menu.h"
#include "xiltimer.h"
#undef COUNTS_PER_SECOND
/* The real BSP macro is not parenthesized. */
#define TEST_CPU_FREQ UINT32_C(666666687)
#define TEST_SLEEP_FREQ TEST_CPU_FREQ/2
#define COUNTS_PER_SECOND TEST_SLEEP_FREQ
#include "../../firmware/platform/xilinx/usb_hid_service.c"
static XTime fake_now;
static XTime host_poll_duration;
static unsigned host_polls;
static bool host_mouse_once;
static unsigned submits;
static unsigned string_mode, string_requests;
static const uint8_t *hid_descriptor_data;
static uint32_t hid_descriptor_size;
static uint8_t hid_protocol_reject_mask;
static unsigned hid_protocol_calls[2];
static uint8_t hid_last_protocol;
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
void cherryusb_host_poll(uint8_t bus)
{
    (void)bus;++host_polls;fake_now+=host_poll_duration;
    if(host_mouse_once) {
        host_mouse_once=false;
        mt_iigs_hid_mouse_report(&g_iigs_input,4,-2,1U);
    }
}
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
{
    (void)hid;
    if (hid_descriptor_data == NULL) return 0;
    const uint32_t bytes = hid_descriptor_size < length ? hid_descriptor_size : length;
    memcpy(buffer, hid_descriptor_data, bytes);
    return (int)bytes;
}
int usbh_hid_set_idle(struct usbh_hid *hid, uint8_t id, uint8_t duration)
{ (void)hid; (void)id; (void)duration; return 0; }
int usbh_hid_set_protocol(struct usbh_hid *hid, uint8_t protocol)
{
    (void)hid;
    assert(protocol < 2U);
    ++hid_protocol_calls[protocol];
    hid_last_protocol = protocol;
    return (hid_protocol_reject_mask & (1U << protocol)) != 0U ? -1 : 0;
}
int usbh_hid_parse_report_descriptor(const uint8_t *data, uint32_t size, struct usbh_hid_report_info *info)
{ (void)data; (void)size; memset(info, 0, sizeof(*info)); return 0; }

static void test_shared_poll_deadline(void)
{
    const uint64_t frequency=(uint64_t)COUNTS_PER_SECOND;
    const XTime millisecond=(frequency+UINT64_C(999))/UINT64_C(1000);
    mt_core_link_t link;
    mt_core_link_init(&link,NULL,input_read,input_write);
    usb_hid_service_init(&link);usb_hid_service_set_active_core(MT_IIGS_CORE_ID);
    usb_hid_service_set_core_ready(true);
    g_usbhost_bus[CHERRYUSB_USB1_BUSID].hub_mq=&fake_now;
    g_usbhost_bus[CHERRYUSB_USB1_BUSID].hub_sem=&fake_now;
    assert(usb_hid_service_start()==0);
    fake_now=0U;host_polls=0U;host_poll_duration=0U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==1U);
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==1U); /* Tick zero is valid. */
    fake_now=millisecond-1U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==1U);
    fake_now=millisecond;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==2U);
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==2U); /* Deferred poll refreshes it. */
    usb_hid_service_poll();assert(host_polls==3U); /* Normal polling is never throttled. */
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==3U);
    fake_now+=millisecond-1U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==3U);
    ++fake_now;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==4U);
    host_poll_duration=2U*millisecond;
    usb_hid_service_poll();assert(host_polls==5U);
    host_poll_duration=0U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==5U); /* Completion, not entry. */
    fake_now+=millisecond-1U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==5U);
    ++fake_now;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==6U);
    usb_hid_service_poll_deferred_if_due(0U);assert(host_polls==7U);
    usb_hid_service_poll_deferred_if_due(0U);assert(host_polls==8U);
    host_poll_duration=2U*millisecond;fake_now+=millisecond;
    usb_hid_service_poll_deferred_if_due(1000U);
    const unsigned after_long_deferred=host_polls;
    host_poll_duration=0U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==after_long_deferred);
    fake_now+=millisecond-1U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==after_long_deferred);
    ++fake_now;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==after_long_deferred+1U);
    g_emit_deferred=1U;
    usb_hid_service_poll_deferred_if_due(0U);assert(g_emit_deferred==1U);
    g_emit_deferred=0U;
    /* Short HDD-like requests keep ordinary polling frequent for several
     * milliseconds; the fallback must not run on an independent deadline. */
    const unsigned before_sectors=host_polls;
    const XTime quarter_ms=(frequency+UINT64_C(3999))/UINT64_C(4000);
    const XTime microsecond=(frequency+UINT64_C(999999))/UINT64_C(1000000);
    for(unsigned i=0U;i<20U;++i) {
        fake_now+=quarter_ms;
        usb_hid_service_poll();
        fake_now+=microsecond;
        usb_hid_service_poll_deferred_if_due(1000U);
        assert(host_polls==before_sectors+i+1U);
    }
    fake_now=g_poll_completed+millisecond-1U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==before_sectors+20U);
    ++fake_now;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==before_sectors+21U);
    fake_now=UINT64_MAX-millisecond/2U;usb_hid_service_poll();
    const unsigned wrapped=host_polls;
    fake_now+=millisecond-1U;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==wrapped);
    ++fake_now;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==wrapped+1U);
    const unsigned events=routed_events;
    host_mouse_once=true;
    usb_hid_service_poll_deferred_if_due(0U);
    assert(!host_mouse_once && routed_events==events && g_emit_deferred==0U);
    usb_hid_service_poll();
    assert(routed_events==events+2U && g_iigs_input.mouse_emitted_buttons==1U);
    assert(usb_hid_service_stop()==0 && !g_poll_completed_valid);
    assert(usb_hid_service_start()==0 && !g_poll_completed_valid);
    const unsigned restarted=host_polls;
    usb_hid_service_poll_deferred_if_due(1000U);assert(host_polls==restarted+1U);
    usb_hid_service_init(&link);assert(!g_poll_completed_valid);
    fake_now=0U;
    puts("PASS shared normal/deferred USB completion deadlines, tick-zero/wrap and deferred input delivery");
}

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
    slot.gamepad = 1U;
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

static void test_hid_collection_fields(void)
{
    struct usbh_hid_report_info info;
    mt_usb_hid_descriptor_meta_t meta;
    static const uint8_t composite[] = {
        /* Keyboard auxiliary buttons are not a controller, even with 60 bits. */
        0x05, 1, 0x09, 6, 0xa1, 1, 0x85, 1,
        0x05, 9, 0x09, 1, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 60, 0x81, 2, 0xc0,
        /* A button-only gamepad remains valid and shares report ID1. */
        0x05, 1, 0x09, 5, 0xa1, 1,
        0x05, 9, 0x19, 1, 0x29, 2, 0x95, 2, 0x81, 2,
        0x75, 1, 0x95, 2, 0x81, 1,
        /* Output/Feature offsets must not alter the next input field. */
        0x75, 8, 0x95, 4, 0x91, 2, 0xb1, 2,
        /* Nested Physical collection inherits the gamepad application. */
        0x05, 1, 0x09, 1, 0xa1, 0,
        0x09, 0x30, 0x09, 0x31, 0x75, 8, 0x95, 2, 0x26, 255, 0, 0x81, 2,
        /* Separate report ID offsets start at zero. */
        0x85, 2, 0x09, 0x35, 0x95, 1, 0x81, 2, 0xc0, 0xc0,
        /* Consumer auxiliary button-only application is also filtered. */
        0x05, 0x0c, 0x09, 1, 0xa1, 1, 0x05, 9, 0x09, 1,
        0x75, 1, 0x95, 1, 0x81, 2, 0xc0
    };
    assert(mt_usb_parse_input_descriptor(composite, sizeof(composite), &info, &meta) == 0);
    assert(meta.application_count == 3U && info.using_report_id);
    assert(meta.applications[0].usage_page == 1U && meta.applications[0].usage == 6U);
    assert(meta.applications[0].capabilities ==
        (MT_USB_HID_APPLICATION_KEYBOARD | MT_USB_HID_APPLICATION_BUTTONS));
    assert(meta.applications[1].capabilities ==
        (MT_USB_HID_APPLICATION_GAMEPAD | MT_USB_HID_APPLICATION_BUTTONS | MT_USB_HID_APPLICATION_AXIS));
    assert(meta.applications[2].capabilities == MT_USB_HID_APPLICATION_BUTTONS);
    assert(info.report_item_count == 5U);
    const unsigned offsets[] = {60, 61, 64, 72, 0};
    const unsigned usages[] = {1, 2, 0x30, 0x31, 0x35};
    for (unsigned i = 0; i < 5U; ++i) {
        assert(meta.field_application[i] == 1U);
        assert(info.report_items[i].report_bit_offset == offsets[i]);
        assert(info.report_items[i].attribute.usage_min == usages[i]);
        assert(info.report_items[i].attribute.report_id == (i == 4U ? 2U : 1U));
    }
    static const uint8_t button_gamepad[] = {
        0x05, 1, 0x09, 4, 0xa1, 1, 0x05, 9, 0x19, 1, 0x29, 8,
        0x15, 0, 0x25, 1, 0x75, 1, 0x95, 8, 0x81, 2, 0xc0
    };
    assert(mt_usb_parse_input_descriptor(button_gamepad, sizeof(button_gamepad), &info, &meta) == 0);
    assert(info.report_item_count == 8U &&
        meta.applications[0].capabilities == (MT_USB_HID_APPLICATION_GAMEPAD | MT_USB_HID_APPLICATION_BUTTONS));
    static const uint8_t unowned_buttons[] = {
        0x05, 9, 0x19, 1, 0x29, 8, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 8, 0x81, 2
    };
    assert(mt_usb_parse_input_descriptor(unowned_buttons, sizeof(unowned_buttons), &info, &meta) == 0);
    assert(info.report_item_count == 0U && meta.application_count == 0U);
    static const uint8_t nested_apps[] = {
        0x05, 1, 0x09, 5, 0xa1, 1,
        0x05, 9, 0x09, 1, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 1, 0x81, 2,
        /* A nested Application owns its own fields, even inside a gamepad. */
        0x05, 1, 0x09, 6, 0xa1, 1, 0x05, 9, 0x09, 2, 0x81, 2, 0xc0,
        0x09, 3, 0x81, 2, 0xc0
    };
    assert(mt_usb_parse_input_descriptor(nested_apps, sizeof(nested_apps), &info, &meta) == 0);
    assert(meta.application_count == 2U && info.report_item_count == 2U);
    assert(meta.field_application[0] == 0U && meta.field_application[1] == 0U);
    assert(info.report_items[0].report_bit_offset == 0U && info.report_items[1].report_bit_offset == 2U);
    assert(info.report_items[1].attribute.usage_min == 3U);
    struct usbh_hid_report_info compatibility_info;
    assert(mt_usb_parse_gamepad_descriptor(nested_apps, sizeof(nested_apps), &compatibility_info) == 0);
    assert(memcmp(&compatibility_info, &info, sizeof(info)) == 0);
    puts("PASS HID application ownership, filtered auxiliaries, split IDs and preserved offsets");
}

static void test_hid_axis_applications(void)
{
    struct usbh_hid_report_info info;
    mt_usb_hid_descriptor_meta_t meta;
    static const uint8_t mouse[] = {
        0x05, 1, 0x09, 2, 0xa1, 1, 0x09, 1, 0xa1, 0,
        0x05, 9, 0x19, 1, 0x29, 5, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 5, 0x81, 2,
        0x75, 3, 0x95, 1, 0x81, 1,
        0x05, 1, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
        0x15, 0x81, 0x25, 0x7f, 0x75, 8, 0x95, 3, 0x81, 6, 0xc0, 0xc0
    };
    assert(mt_usb_parse_input_descriptor(mouse, sizeof(mouse), &info, &meta) == 0);
    assert(info.report_item_count == 8U && meta.application_count == 1U);
    assert(meta.applications[0].capabilities == (MT_USB_HID_APPLICATION_MOUSE |
        MT_USB_HID_APPLICATION_AXIS | MT_USB_HID_APPLICATION_RELATIVE | MT_USB_HID_APPLICATION_BUTTONS));
    for (unsigned i = 0; i < 8U; ++i) assert(meta.field_application[i] == 0U);
    for (unsigned i = 5; i < 8U; ++i) {
        assert(info.report_items[i].report_bit_offset == 8U * (i - 4U));
        assert(info.report_items[i].attribute.logical_min == -127);
        assert(info.report_items[i].attribute.logical_max == 127);
        assert(info.report_items[i].report_flags == 6U);
    }
    assert(info.report_items[7].attribute.usage_min == HID_DESKTOP_USAGE_WHEEL);

    static const uint8_t keypad[] = {
        0x05, 1, 0x09, 7, 0xa1, 1, 0x85, 3,
        /* A conventional six-key array is skipped, but still occupies 48 bits. */
        0x05, 7, 0x19, 0, 0x29, 0x65, 0x15, 0, 0x25, 0x65, 0x75, 8, 0x95, 6, 0x81, 0,
        /* Push/Pop restores report ID and usage-page globals, not collection. */
        0xa4, 0x85, 4, 0x05, 1, 0x09, 0x37, 0x15, 0x81, 0x25, 0x7f,
        0x95, 1, 0x81, 6, 0xb4,
        /* 32-bit extended usage supplies Consumer AC Pan's page explicitly. */
        0x0b, 0x38, 0x02, 0x0c, 0x00, 0x95, 1, 0x15, 0x81, 0x25, 0x7f, 0x81, 6,
        0x05, 1, 0x09, 0x36, 0x15, 0, 0x26, 255, 0, 0x81, 2,
        0x05, 9, 0x09, 1, 0x15, 0, 0x25, 1, 0x75, 1, 0x81, 2, 0xc0
    };
    assert(mt_usb_parse_input_descriptor(keypad, sizeof(keypad), &info, &meta) == 0);
    assert(info.report_item_count == 4U && meta.applications[0].usage == 7U);
    assert(meta.applications[0].capabilities == (MT_USB_HID_APPLICATION_KEYBOARD |
        MT_USB_HID_APPLICATION_AXIS | MT_USB_HID_APPLICATION_RELATIVE | MT_USB_HID_APPLICATION_BUTTONS));
    assert(info.report_items[0].attribute.usage_min == HID_DESKTOP_USAGE_DIAL &&
        info.report_items[0].attribute.report_id == 4U && info.report_items[0].report_bit_offset == 0U);
    assert(info.report_items[1].attribute.usage_page == 0x0cU &&
        info.report_items[1].attribute.usage_min == 0x238U && info.report_items[1].report_bit_offset == 48U);
    assert(info.report_items[2].attribute.usage_min == HID_DESKTOP_USAGE_SLIDER &&
        info.report_items[2].report_bit_offset == 56U && info.report_items[2].report_flags == 2U);
    assert(info.report_items[3].report_bit_offset == 64U);

    static const uint8_t siblings[] = {
        /* One top-level mouse owns buttons but lacks axes, so is not bindable. */
        0x05, 1, 0x09, 2, 0xa1, 1, 0x05, 9, 0x09, 1,
        0x15, 0, 0x25, 1, 0x75, 1, 0x95, 1, 0x81, 2, 0xc0,
        /* An independent consumer application owns the wheel and its button. */
        0x05, 0x0c, 0x09, 1, 0xa1, 1, 0x05, 1, 0x09, 0x38,
        0x15, 0x81, 0x25, 0x7f, 0x75, 8, 0x81, 6,
        0x05, 9, 0x09, 2, 0x75, 1, 0x15, 0, 0x25, 1, 0x81, 2, 0xc0
    };
    assert(mt_usb_parse_input_descriptor(siblings, sizeof(siblings), &info, &meta) == 0);
    assert(info.report_item_count == 2U && meta.field_application[0] == 1U && meta.field_application[1] == 1U);
    assert(info.report_items[0].report_bit_offset == 1U && info.report_items[1].report_bit_offset == 9U);
    puts("PASS relative mouse, wheel/dial/slider/pan and axis-bearing keypad applications");
}

static void test_hid_descriptor_bounds(void)
{
    struct usbh_hid_report_info info;
    mt_usb_hid_descriptor_meta_t meta;
    const uint8_t unclosed[] = {0x05, 1, 0x09, 5, 0xa1, 1};
    const uint8_t underflow[] = {0xc0};
    const uint8_t short_long[] = {0xfe, 3, 0, 1};
    const uint8_t too_many_fields[] = {0x05, 1, 0x09, 5, 0xa1, 1,
        0x05, 9, 0x09, 1, 0x75, 1, 0x95, CONFIG_USB_HID_MAX_REPORT_ITEMS + 1, 0x81, 2, 0xc0};
    const uint8_t *bad[] = {unclosed, underflow, short_long, too_many_fields};
    const uint32_t sizes[] = {sizeof(unclosed), sizeof(underflow), sizeof(short_long), sizeof(too_many_fields)};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(&info, 0xa5, sizeof(info)); memset(&meta, 0xa5, sizeof(meta));
        assert(mt_usb_parse_input_descriptor(bad[i], sizes[i], &info, &meta) < 0);
        assert(info.report_item_count == 0U && !info.using_report_id && meta.application_count == 0U);
        assert(meta.field_application[0] == MT_USB_HID_NO_APPLICATION);
    }
    uint8_t too_deep[(MT_USB_HID_MAX_COLLECTION_DEPTH + 1U) * 3U];
    for (unsigned i = 0; i <= MT_USB_HID_MAX_COLLECTION_DEPTH; ++i) {
        too_deep[2U * i] = 0xa1; too_deep[2U * i + 1U] = 0;
        too_deep[2U * (MT_USB_HID_MAX_COLLECTION_DEPTH + 1U) + i] = 0xc0;
    }
    assert(mt_usb_parse_input_descriptor(too_deep, sizeof(too_deep), &info, &meta) < 0);
    uint8_t too_many_apps[(MT_USB_HID_MAX_APPLICATIONS + 1U) * 3U];
    for (unsigned i = 0; i <= MT_USB_HID_MAX_APPLICATIONS; ++i) {
        too_many_apps[3U * i] = 0xa1; too_many_apps[3U * i + 1U] = 1; too_many_apps[3U * i + 2U] = 0xc0;
    }
    assert(mt_usb_parse_input_descriptor(too_many_apps, sizeof(too_many_apps), &info, &meta) < 0);
    puts("PASS HID collection/field bounds and cleared state on malformed descriptors");
}

static void test_hid_keyboard_chunks(void)
{
    struct usbh_hid_report_info info;
    mt_usb_hid_descriptor_meta_t meta;
    static const uint8_t keyboard[] = {
        0x05, 1, 0x09, 6, 0xa1, 1, 0x85, 1,
        0x05, 7, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 8, 0x81, 2,
        0x75, 8, 0x95, 1, 0x81, 1,
        0x19, 0, 0x29, 0xff, 0x15, 0, 0x26, 255, 0, 0x75, 8, 0x95, 6, 0x81, 0,
        /* A full NKRO bitmap costs one keyboard chunk and no axis fields. */
        0x85, 2, 0x19, 4, 0x29, 0x83, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 128, 0x81, 2, 0xc0
    };
    assert(mt_usb_parse_input_descriptor(keyboard, sizeof(keyboard), &info, &meta) == 0);
    assert(info.report_item_count == 0U && meta.keyboard_item_count == 3U && meta.keyboard_usage_count == 0U);
    const mt_usb_hid_keyboard_item_t *modifiers = &meta.keyboard_items[0];
    const mt_usb_hid_keyboard_item_t *array = &meta.keyboard_items[1];
    const mt_usb_hid_keyboard_item_t *bitmap = &meta.keyboard_items[2];
    assert(modifiers->item.attribute.report_count == 8U && modifiers->item.report_bit_offset == 0U);
    assert(modifiers->application == 0U && mt_usb_hid_keyboard_usage(&meta, modifiers, 0) == 0xe0U);
    assert(mt_usb_hid_keyboard_usage(&meta, modifiers, 7) == 0xe7U);
    assert(array->item.attribute.report_count == 6U && array->item.report_bit_offset == 16U);
    assert(array->item.report_flags == 0U && array->item.attribute.report_id == 1U);
    assert(mt_usb_hid_keyboard_usage(&meta, array, 0xffU) == 0xffU);
    assert(mt_usb_hid_keyboard_usage(&meta, array, 0x100U) == 0U);
    assert(bitmap->item.attribute.report_count == 128U && bitmap->item.report_bit_offset == 0U);
    assert(bitmap->item.attribute.report_id == 2U && mt_usb_hid_keyboard_usage(&meta, bitmap, 127U) == 0x83U);

    static const uint8_t explicit_keys[] = {
        0x05, 1, 0x09, 6, 0xa1, 1, 0x05, 7,
        0x09, 0xe0, 0x09, 0xe2, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 4, 0x81, 2,
        0x09, 4, 0x09, 7, 0x15, 1, 0x25, 2, 0x75, 8, 0x95, 6, 0x81, 0,
        /* A single Input can interleave keyboard and axis usage pages. */
        0x09, 0xe1, 0x0b, 0x30, 0, 1, 0, 0x09, 0xe3,
        0x15, 0, 0x26, 255, 0, 0x75, 8, 0x95, 3, 0x81, 2, 0xc0
    };
    assert(mt_usb_parse_input_descriptor(explicit_keys, sizeof(explicit_keys), &info, &meta) == 0);
    assert(meta.keyboard_item_count == 4U && meta.keyboard_usage_count == 6U && info.report_item_count == 1U);
    assert(mt_usb_hid_keyboard_usage(&meta, &meta.keyboard_items[0], 0) == 0xe0U);
    assert(mt_usb_hid_keyboard_usage(&meta, &meta.keyboard_items[0], 1) == 0xe2U);
    assert(mt_usb_hid_keyboard_usage(&meta, &meta.keyboard_items[0], 3) == 0xe2U);
    assert(mt_usb_hid_keyboard_usage(&meta, &meta.keyboard_items[1], 0) == 4U);
    assert(mt_usb_hid_keyboard_usage(&meta, &meta.keyboard_items[1], 1) == 7U);
    assert(mt_usb_hid_keyboard_usage(&meta, &meta.keyboard_items[1], 2) == 0U);
    assert(meta.keyboard_items[2].item.report_bit_offset == 52U);
    assert(meta.keyboard_items[3].item.report_bit_offset == 68U);
    assert(info.report_items[0].report_bit_offset == 60U);
    assert(info.report_items[0].attribute.usage_min == HID_DESKTOP_USAGE_X);

    uint8_t too_many_chunks[7U + (MT_USB_HID_MAX_KEYBOARD_ITEMS + 1U) * 14U];
    const uint8_t prefix[] = {0x05, 1, 0x09, 6, 0xa1, 1};
    const uint8_t chunk[] = {0x05, 7, 0x09, 4, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 1, 0x81, 2};
    memcpy(too_many_chunks, prefix, sizeof(prefix));
    for (unsigned i = 0; i <= MT_USB_HID_MAX_KEYBOARD_ITEMS; ++i)
        memcpy(too_many_chunks + sizeof(prefix) + i * sizeof(chunk), chunk, sizeof(chunk));
    too_many_chunks[sizeof(too_many_chunks) - 1U] = 0xc0;
    assert(mt_usb_parse_input_descriptor(too_many_chunks, sizeof(too_many_chunks), &info, &meta) < 0);
    assert(info.report_item_count == 0U && meta.keyboard_item_count == 0U);

    uint8_t too_many_usages[7U + 2U * (4U + 2U * 33U + 10U)];
    memcpy(too_many_usages, prefix, sizeof(prefix));
    unsigned pos = sizeof(prefix);
    for (unsigned group = 0; group < 2U; ++group) {
        too_many_usages[pos++] = 0x05; too_many_usages[pos++] = 7;
        too_many_usages[pos++] = 0x85; too_many_usages[pos++] = (uint8_t)(group + 1U);
        for (unsigned i = 0; i < 33U; ++i) {
            too_many_usages[pos++] = 0x09; too_many_usages[pos++] = (uint8_t)(i + 4U);
        }
        const uint8_t input[] = {0x15, 0, 0x25, 1, 0x75, 1, 0x95, 33, 0x81, 2};
        memcpy(too_many_usages + pos, input, sizeof(input)); pos += sizeof(input);
    }
    too_many_usages[pos++] = 0xc0;
    assert(pos == sizeof(too_many_usages));
    assert(mt_usb_parse_input_descriptor(too_many_usages, sizeof(too_many_usages), &info, &meta) < 0);
    assert(meta.keyboard_item_count == 0U && meta.keyboard_usage_count == 0U);
    puts("PASS compact keyboard modifiers, arrays, NKRO, explicit usages and independent bounds");
}

/* Exact descriptors captured from the user's hub on M0.6.2-hid1.
 * Capture: build/usb-hid-diagnostic-m062/live-descriptors.log
 * SHA256: 557e61dfa49384714d7c252f5982198b0ee331c066aa5d5c0834d402e9697ede
 * Enumeration evidence only; reports below are deterministic host fixtures. */
static const uint8_t hid_live_descriptor_0[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x03, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02, 0x95, 0x05, 0x75, 0x01, 0x91, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2a, 0xff, 0x00, 0x81, 0x00,
    0xc0,
};
static const uint8_t hid_live_descriptor_1[] = {
    0x06, 0x0c, 0x00, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x01, 0x25, 0x01, 0x15, 0x00, 0x75, 0x01, 0x09,
    0xb5, 0x09, 0xb6, 0x09, 0xb7, 0x09, 0xcd, 0x09, 0xe2, 0x09, 0xe9, 0x09, 0xea, 0x0a, 0x8a, 0x01,
    0x0a, 0x92, 0x01, 0x0a, 0x21, 0x02, 0x1a, 0x23, 0x02, 0x2a, 0x27, 0x02, 0x0a, 0x2a, 0x02, 0x95,
    0x10, 0x81, 0x02, 0x06, 0x00, 0xff, 0x1a, 0x01, 0xff, 0x2a, 0x0b, 0xff, 0x75, 0x01, 0x95, 0x0b,
    0x81, 0x02, 0x0a, 0x0c, 0xff, 0x25, 0x03, 0x75, 0x02, 0x95, 0x01, 0x81, 0x02, 0x75, 0x03, 0x81,
    0x01, 0xc0, 0x05, 0x01, 0x09, 0x80, 0xa1, 0x01, 0x85, 0x02, 0x25, 0x01, 0x15, 0x00, 0x19, 0x81,
    0x29, 0x83, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x95, 0x05, 0x81, 0x03, 0xc0, 0x06, 0x00, 0xff,
    0x0a, 0x00, 0xff, 0xa1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x27, 0xff, 0xff, 0xff, 0x7f, 0x0a, 0x0d,
    0xff, 0x75, 0x20, 0x95, 0x01, 0xb1, 0x02, 0xc0,
};
static const uint8_t hid_live_descriptor_2[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x05, 0x09, 0x15, 0x00, 0x25, 0x01, 0x95, 0x01, 0x75, 0x01,
    0x09, 0x01, 0x81, 0x02, 0x09, 0x02, 0x81, 0x02, 0x09, 0x03, 0x81, 0x02, 0x09, 0x04, 0x81, 0x02,
    0x09, 0x05, 0x81, 0x02, 0x09, 0x06, 0x81, 0x02, 0x09, 0x07, 0x81, 0x02, 0x09, 0x08, 0x81, 0x02,
    0x09, 0x09, 0x81, 0x02, 0x09, 0x0a, 0x81, 0x02, 0x09, 0x0b, 0x81, 0x02, 0x95, 0x05, 0x81, 0x01,
    0xc0,
};
static const uint8_t hid_live_descriptor_3[] = {
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xa1, 0x02, 0x75, 0x08, 0x95, 0x02, 0x15, 0x00, 0x26, 0xff,
    0x00, 0x35, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30, 0x09, 0x31, 0x81, 0x02, 0x95, 0x03, 0x81, 0x01,
    0x75, 0x01, 0x95, 0x04, 0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45, 0x01, 0x81, 0x01, 0x65, 0x00,
    0x75, 0x01, 0x95, 0x0a, 0x25, 0x01, 0x45, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0a, 0x81, 0x02,
    0x06, 0x00, 0xff, 0x75, 0x01, 0x95, 0x0a, 0x25, 0x01, 0x45, 0x01, 0x09, 0x01, 0x81, 0x02, 0xc0,
    0xa1, 0x02, 0x75, 0x08, 0x95, 0x07, 0x46, 0xff, 0x00, 0x26, 0xff, 0x00, 0x09, 0x02, 0x91, 0x02,
    0xc0, 0xc0,
};
static const uint8_t hid_live_descriptor_4[] = {
    0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x03, 0x09, 0x02, 0x15, 0x00, 0x26, 0x00, 0xff,
    0x75, 0x08, 0x95, 0x40, 0x81, 0x06, 0x09, 0x02, 0x15, 0x00, 0x26, 0x00, 0xff, 0x75, 0x08, 0x95,
    0x40, 0x91, 0x06, 0xc0,
};
static const uint8_t hid_live_descriptor_5[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x03,
    0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02, 0x95, 0x05, 0x75, 0x01, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2a, 0xff, 0x00,
    0x81, 0x00, 0xc0, 0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xa1, 0x00, 0x05,
    0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x75,
    0x05, 0x95, 0x01, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25,
    0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xc0, 0xc0, 0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85,
    0x04, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81,
    0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95, 0x08, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29,
    0x05, 0x91, 0x02, 0x95, 0x09, 0x75, 0x08, 0x15, 0x00, 0x25, 0xff, 0x05, 0x07, 0x19, 0x00, 0x29,
    0xff, 0x81, 0x00, 0xc0, 0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x05, 0x95, 0x01, 0x75, 0x10,
    0x16, 0x00, 0x00, 0x26, 0xff, 0x02, 0x1a, 0x00, 0x00, 0x2a, 0xff, 0x02, 0x81, 0x00, 0xc0,
};
static const uint8_t hid_live_descriptor_6[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0xf8, 0x26, 0xff, 0x07, 0x75, 0x0c, 0x95, 0x02,
    0x81, 0x06, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xc0, 0xc0,
};
typedef struct {
    uint16_t vendor, product;
    uint8_t interface_id, subclass, protocol, packet_bytes, interval;
    const uint8_t *descriptor;
    uint32_t length;
    uint8_t input_items, keyboard_items;
} mt_hid_live_fixture_t;
static const mt_hid_live_fixture_t hid_live_fixtures[] = {
    {0x06A3, 0x8000, 0, 1, 1, 8, 10, hid_live_descriptor_0, sizeof(hid_live_descriptor_0), 0, 2},
    {0x06A3, 0x8000, 1, 0, 0, 5, 10, hid_live_descriptor_1, sizeof(hid_live_descriptor_1), 0, 0},
    {0x06A3, 0x8000, 2, 0, 0, 2, 10, hid_live_descriptor_2, sizeof(hid_live_descriptor_2), 11, 0},
    {0x081F, 0xE401, 0, 0, 0, 8, 10, hid_live_descriptor_3, sizeof(hid_live_descriptor_3), 12, 0},
    {0x1189, 0x8840, 0, 0, 0, 64, 1, hid_live_descriptor_4, sizeof(hid_live_descriptor_4), 0, 0},
    {0x1189, 0x8840, 1, 0, 0, 16, 1, hid_live_descriptor_5, sizeof(hid_live_descriptor_5), 6, 4},
    {0x30FA, 0x0400, 0, 1, 2, 5, 10, hid_live_descriptor_6, sizeof(hid_live_descriptor_6), 8, 0},
};

static void test_hid_live_descriptors(void)
{
    struct usbh_hid_report_info info;
    mt_usb_hid_descriptor_meta_t meta;
    for (unsigned i = 0; i < sizeof(hid_live_fixtures) / sizeof(hid_live_fixtures[0]); ++i) {
        const mt_hid_live_fixture_t *fixture = &hid_live_fixtures[i];
        assert(mt_usb_parse_input_descriptor(fixture->descriptor, fixture->length, &info, &meta) == 0);
        assert(info.report_item_count == fixture->input_items);
        assert(meta.keyboard_item_count == fixture->keyboard_items);
        if (i == 2U) { /* Genuine button-only gamepad advertised by the gaming keyboard. */
            assert(meta.applications[0].capabilities & MT_USB_HID_APPLICATION_GAMEPAD);
            assert(info.report_items[10].attribute.usage_min == 11U);
        } else if (i == 3U) { /* SNES-style dpad is absolute X/Y in this descriptor. */
            assert(info.report_items[0].attribute.usage_min == HID_DESKTOP_USAGE_X);
            assert(info.report_items[1].attribute.usage_min == HID_DESKTOP_USAGE_Y);
            assert(info.report_items[2].report_bit_offset == 44U);
            assert((info.report_items[0].report_flags & HID_MAINITEM_RELATIVE) == 0U);
        } else if (i == 5U) {
            assert(meta.application_count == 4U);
            assert(meta.keyboard_items[0].item.attribute.report_id == 1U);
            assert(meta.keyboard_items[2].item.attribute.report_id == 4U);
            assert(meta.keyboard_items[3].item.attribute.report_count == 9U);
            assert(info.report_items[5].attribute.report_id == 2U);
            assert(info.report_items[5].attribute.usage_min == HID_DESKTOP_USAGE_WHEEL);
            assert(meta.field_application[5] == 1U);
        } else if (i == 6U) {
            assert(info.report_items[5].attribute.report_size == 12U);
            assert(info.report_items[5].attribute.logical_min == -2048);
            assert(info.report_items[5].attribute.logical_max == 2047);
            assert(info.report_items[5].report_bit_offset == 8U);
            assert(info.report_items[6].report_bit_offset == 20U);
            assert(info.report_items[7].report_bit_offset == 32U);
            assert(info.report_items[7].attribute.usage_min == HID_DESKTOP_USAGE_WHEEL);
        }
    }
    puts("PASS seven exact live HID descriptors, keyboard/gamepad collections and 12-bit mouse layout");
}

static void hid_fixture_connect(const mt_hid_live_fixture_t *fixture,
    uint8_t slot, struct usbh_hubport *port, struct usb_endpoint_descriptor *endpoint,
    struct usbh_hid *hid)
{
    memset(port, 0, sizeof(*port)); memset(endpoint, 0, sizeof(*endpoint)); memset(hid, 0, sizeof(*hid));
    port->connected = true; port->port = (uint8_t)(slot + 1U);
    port->device_desc.idVendor = fixture->vendor; port->device_desc.idProduct = fixture->product;
    port->config.intf[fixture->interface_id].altsetting[0].intf_desc.bInterfaceSubClass = fixture->subclass;
    endpoint->bEndpointAddress = 0x81U; endpoint->bmAttributes = USB_ENDPOINT_TYPE_INTERRUPT;
    endpoint->wMaxPacketSize = fixture->packet_bytes; endpoint->bInterval = fixture->interval;
    hid->hport = port; hid->intin = endpoint; hid->minor = slot;
    hid->intf = fixture->interface_id; hid->protocol = fixture->protocol;
    hid->report_size = (uint16_t)fixture->length;
    hid_descriptor_data = fixture->descriptor; hid_descriptor_size = fixture->length;
    usbh_hid_run(hid);
    assert(report_length(&g_slots[slot]) == fixture->packet_bytes);
    hid_descriptor_data = NULL; hid_descriptor_size = 0;
}

static void hid_fixture_report(uint8_t slot, const uint8_t *data, size_t length)
{
    assert(length <= MT_HID_REPORT_BYTES);
    memcpy(g_reports[slot], data, length);
    report_complete(&g_slots[slot], (int)length);
}

static bool iigs_key_held(unsigned slot, unsigned usage)
{
    return (g_iigs_input.keyboard.usages[slot][usage / 8U] & (1U << (usage % 8U))) != 0U;
}

static unsigned generic_menu_events;
static void observe_generic_menu(void *context, unsigned action)
{
    (void)context;
    assert(action == MT_CONTROL_MENU);
    ++generic_menu_events;
}

static void test_hid_live_runtime(void)
{
    struct usbh_hubport ports[7];
    struct usb_endpoint_descriptor endpoints[7];
    struct usbh_hid hids[7];
    usb_hid_service_init(NULL); usb_hid_service_set_active_core(MT_IIGS_CORE_ID);
    usb_hid_service_set_keyboard_filter(observe_keys, NULL);
    memset(hid_protocol_calls, 0, sizeof(hid_protocol_calls));
    hid_protocol_reject_mask = 0; fake_now = 0;
    for (unsigned i = 0; i < 7U; ++i)
        hid_fixture_connect(&hid_live_fixtures[i], (uint8_t)i, &ports[i], &endpoints[i], &hids[i]);
    assert(g_slots[0].keyboard && !g_slots[0].report_protocol && !g_slots[0].gamepad);
    assert(!g_slots[1].gamepad && !g_slots[1].keyboard);
    assert(g_slots[2].gamepad && !g_slots[2].generic_axes && !g_slots[2].keyboard);
    assert(g_slots[3].gamepad && !g_slots[3].generic_axes);
    assert(!g_slots[4].gamepad && !g_slots[4].keyboard);
    assert(g_slots[5].keyboard && g_slots[5].mouse && g_slots[5].gamepad && g_slots[5].report_protocol);
    assert(g_slots[6].mouse && g_slots[6].gamepad && g_slots[6].report_protocol && g_slots[6].generic_axes);
    assert(hid_protocol_calls[HID_PROTOCOL_BOOT] == 1U && hid_protocol_calls[HID_PROTOCOL_REPORT] == 1U);
    assert(g_devices[6].buttons_present == 31U &&
        g_devices[6].axis_mask == ((1U << MT_GAMEPAD_AXIS_X) | (1U << MT_GAMEPAD_AXIS_Y) | (1U << MT_INPUT_AXIS_WHEEL)));

    const uint8_t keyboard0[] = {2, 0, 4, 0, 0, 0, 0, 0};
    hid_fixture_report(0, keyboard0, sizeof(keyboard0));
    assert(iigs_key_held(0, 4) && iigs_key_held(0, 0xe1));
    assert(g_gamepads.raw[2].buttons == 0U); /* Main keys are a different interface. */
    const uint8_t keyboard_gamepad[] = {1, 4};
    hid_fixture_report(2, keyboard_gamepad, sizeof(keyboard_gamepad));
    assert(g_gamepads.raw[2].buttons == 0x401U && g_gamepads.raw[2].axis_valid_mask == 0U);
    const uint8_t snes[] = {0, 255, 0, 0, 0, 0x10, 0, 0};
    hid_fixture_report(3, snes, sizeof(snes));
    assert(g_gamepads.raw[3].axis[MT_GAMEPAD_AXIS_X] == INT16_MIN);
    assert(g_gamepads.raw[3].axis[MT_GAMEPAD_AXIS_Y] == INT16_MAX && g_gamepads.raw[3].buttons == 1U);
    assert((mt_gamepad_digital(&g_gamepads.raw[3]) & 15U) == 6U);

    const uint8_t id1[] = {1, 2, 0, 4, 0, 0, 0, 0, 0};
    const uint8_t id4[] = {4, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0};
    hid_fixture_report(5, id1, sizeof(id1)); hid_fixture_report(5, id4, sizeof(id4));
    assert(iigs_key_held(5, 4) && iigs_key_held(5, 5) && iigs_key_held(5, 0xe1));
    const uint8_t id1_release[] = {1, 0, 0, 0, 0, 0, 0, 0, 0};
    hid_fixture_report(5, id1_release, sizeof(id1_release));
    assert(!iigs_key_held(5, 4) && iigs_key_held(5, 5) && !iigs_key_held(5, 0xe1));
    const uint8_t id4_short[] = {4, 2, 0};
    const uint8_t id4_rollover[] = {4, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    hid_fixture_report(5, id4_short, sizeof(id4_short));
    hid_fixture_report(5, id4_rollover, sizeof(id4_rollover));
    assert(iigs_key_held(5, 5) && !iigs_key_held(5, 0xe1));
    const uint8_t consumer[] = {5, 0xe9, 0};
    hid_fixture_report(5, consumer, sizeof(consumer));
    assert(iigs_key_held(5, 5) && g_gamepads.raw[5].buttons == 0U);
    const uint8_t id4_release[] = {4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    hid_fixture_report(5, id4_release, sizeof(id4_release));
    assert(!iigs_key_held(5, 5));
    const uint8_t menu_keys[] = {1, 0, 0, 0x45, 0x46, 0, 0, 0, 0};
    const unsigned screenshots = printscreen_seen;
    hid_fixture_report(5, menu_keys, sizeof(menu_keys));
    assert(menu_down == 1U && printscreen_seen == screenshots + 1U && !iigs_key_held(5, 0x46));

    /* X=-257/Y=513 occupy packed 12-bit fields; wheel is a distinct signed byte. */
    const uint8_t mouse[] = {0x15, 0xff, 0x1e, 0x20, 3};
    hid_fixture_report(6, mouse, sizeof(mouse));
    assert(g_iigs_input.mouse_x == -257 && g_iigs_input.mouse_y == -513);
    assert(g_gamepads.raw[6].buttons == 0x15U && g_iigs_input.mouse_buttons == 5U);
    assert(g_gamepads.raw[6].axis[MT_GAMEPAD_AXIS_X] == INT16_MIN &&
        g_gamepads.raw[6].axis[MT_GAMEPAD_AXIS_Y] == INT16_MAX);
    assert(g_gamepads.raw[6].axis[MT_INPUT_AXIS_WHEEL] == 4096 && g_slots[6].relative_count[MT_INPUT_AXIS_WHEEL] == 2);
    const mt_gamepad_state_t before_short = g_gamepads.raw[6];
    const uint8_t short_mouse[] = {0, 0};
    hid_fixture_report(6, short_mouse, sizeof(short_mouse));
    assert(memcmp(&before_short, &g_gamepads.raw[6], sizeof(before_short)) == 0);
    assert(g_iigs_input.mouse_x == -257 && g_iigs_input.mouse_y == -513);

    /* Every detent in a burst gets a down and an up edge, then stays neutral. */
    const int16_t wheel_sequence[] = {0, 4096, 0, 4096, 0};
    for (unsigned i = 0; i < sizeof(wheel_sequence) / sizeof(wheel_sequence[0]); ++i) {
        fake_now += MT_RELATIVE_PULSE_TICKS; poll_relative_inputs();
        assert(g_gamepads.raw[6].axis[MT_INPUT_AXIS_WHEEL] == wheel_sequence[i]);
    }
    assert(g_slots[6].relative_pending == 0U && g_slots[6].relative_count[MT_INPUT_AXIS_WHEEL] == 0);
    assert(g_gamepads.raw[6].axis[MT_GAMEPAD_AXIS_X] == 0 && g_gamepads.raw[6].axis[MT_GAMEPAD_AXIS_Y] == 0);
    const uint8_t two_up[] = {0x15, 0, 0, 0, 2};
    const uint8_t one_down[] = {0x15, 0, 0, 0, 0xff};
    hid_fixture_report(6, two_up, sizeof(two_up));
    hid_fixture_report(6, one_down, sizeof(one_down));
    assert(g_slots[6].relative_count[MT_INPUT_AXIS_WHEEL] == 2U);
    const int16_t opposite_sequence[] = {0, 4096, 0, -4096, 0};
    for (unsigned i = 0; i < sizeof(opposite_sequence) / sizeof(opposite_sequence[0]); ++i) {
        fake_now += MT_RELATIVE_PULSE_TICKS; poll_relative_inputs();
        assert(g_gamepads.raw[6].axis[MT_INPUT_AXIS_WHEEL] == opposite_sequence[i]);
    }
    assert(g_slots[6].relative_pending == 0U);
    const uint8_t composite_mouse[] = {2, 2, 7, 0xf8, 0xff};
    hid_fixture_report(5, composite_mouse, sizeof(composite_mouse));
    assert(g_gamepads.raw[5].buttons == 2U && g_gamepads.raw[5].axis[MT_INPUT_AXIS_WHEEL] == -4096);
    assert(g_iigs_input.mouse_x == -250 && g_iigs_input.mouse_y == -505 && g_iigs_input.mouse_buttons == 7U);
    assert(iigs_key_held(5, 0x45)); /* Mouse reports preserve the interface's keyboard state. */
    assert(menu_down == 1U); /* No synthetic empty keyboard event from mouse reports. */

    mt_controller_config_t config = g_controller_config[5];
    config.binding[MT_CONTROL_MENU] = MT_CONTROL_SOURCE_BUTTON;
    assert(usb_hid_service_set_controller_config(5, &config));
    generic_menu_events = 0;
    usb_hid_service_set_controller_action_handler(observe_generic_menu, NULL);
    const uint8_t menu_press[] = {2, 1, 0, 0, 0}, menu_release[] = {2, 0, 0, 0, 0};
    hid_fixture_report(5, menu_press, sizeof(menu_press));
    hid_fixture_report(5, menu_press, sizeof(menu_press));
    assert(generic_menu_events == 1U && menu_down == 1U);
    hid_fixture_report(5, menu_release, sizeof(menu_release));
    hid_fixture_report(5, menu_press, sizeof(menu_press));
    assert(generic_menu_events == 2U && menu_down == 1U);
    usb_hid_service_set_controller_action_handler(NULL, NULL);
    hid_fixture_report(5, composite_mouse, sizeof(composite_mouse));

    hid_fixture_report(6, mouse, sizeof(mouse));
    assert(g_slots[6].relative_count[MT_INPUT_AXIS_WHEEL] != 0);
    usb_hid_service_set_menu_capture(true);
    assert(g_slots[6].relative_pending == 0U && g_slots[6].relative_count[MT_INPUT_AXIS_WHEEL] == 0);
    assert(g_gamepads.raw[6].axis[MT_INPUT_AXIS_WHEEL] == 0 && g_iigs_input.mouse_buttons == 0U);
    usb_hid_service_set_menu_capture(false);
    hid_fixture_report(6, mouse, sizeof(mouse));
    usb_hid_service_set_active_core(MT_C64_CORE_ID);
    assert(g_slots[6].relative_pending == 0U && g_gamepads.raw[6].axis[MT_INPUT_AXIS_WHEEL] == 0);
    usb_hid_service_set_active_core(MT_IIGS_CORE_ID);
    usbh_hid_stop(&hids[6]);
    assert(!g_devices[6].connected && !g_gamepads.raw[6].connected && g_iigs_input.mouse_buttons == 2U);
    for (unsigned i = 0; i < 6U; ++i) usbh_hid_stop(&hids[i]);
    assert(g_gamepads.allocated == 0U && !iigs_key_held(0, 4) && !iigs_key_held(5, 0x45));
    puts("PASS live-device enumeration, packed mouse, composite keys/wheel, pulse release and disconnect");
}

static void test_hid_report_protocol_fallback(void)
{
    struct usbh_hubport port;
    struct usb_endpoint_descriptor endpoint;
    struct usbh_hid hid;
    usb_hid_service_init(NULL); usb_hid_service_set_active_core(MT_IIGS_CORE_ID);
    memset(hid_protocol_calls, 0, sizeof(hid_protocol_calls));
    hid_protocol_reject_mask = 1U << HID_PROTOCOL_REPORT;
    hid_fixture_connect(&hid_live_fixtures[6], 0, &port, &endpoint, &hid);
    assert(hid_protocol_calls[HID_PROTOCOL_REPORT] == 1U && hid_protocol_calls[HID_PROTOCOL_BOOT] == 1U);
    assert(hid_last_protocol == HID_PROTOCOL_BOOT && !g_slots[0].report_protocol && g_slots[0].mouse && g_slots[0].gamepad);
    assert(g_devices[0].buttons_present == 7U && g_devices[0].axis_mask == 3U);
    const uint8_t boot_mouse[] = {5, 0xff, 2};
    hid_fixture_report(0, boot_mouse, sizeof(boot_mouse));
    assert(g_iigs_input.mouse_x == -1 && g_iigs_input.mouse_y == -2 && g_gamepads.raw[0].buttons == 5U);
    usbh_hid_stop(&hid); hid_protocol_reject_mask = 0;

    hid_protocol_reject_mask = (1U << HID_PROTOCOL_REPORT) | (1U << HID_PROTOCOL_BOOT);
    hid_fixture_connect(&hid_live_fixtures[6], 0, &port, &endpoint, &hid);
    assert(!g_slots[0].keyboard && !g_slots[0].mouse && !g_slots[0].gamepad && !g_devices[0].connected);
    const mt_gamepad_state_t disabled = g_gamepads.raw[0];
    hid_fixture_report(0, boot_mouse, sizeof(boot_mouse));
    assert(memcmp(&disabled, &g_gamepads.raw[0], sizeof(disabled)) == 0);
    usbh_hid_stop(&hid); hid_protocol_reject_mask = 0;

    const uint8_t malformed[] = {0x05, 1, 0x09, 2, 0xa1, 1};
    mt_hid_live_fixture_t broken = hid_live_fixtures[6];
    broken.descriptor = malformed; broken.length = sizeof(malformed);
    memset(hid_protocol_calls, 0, sizeof(hid_protocol_calls));
    hid_fixture_connect(&broken, 0, &port, &endpoint, &hid);
    assert(g_slots[0].mouse && !g_slots[0].report_protocol && hid_protocol_calls[HID_PROTOCOL_REPORT] == 0U);
    assert(hid_protocol_calls[HID_PROTOCOL_BOOT] == 1U);
    usbh_hid_stop(&hid);

    /* Synthetic boot-keyboard interface sharing the exact composite layout. */
    mt_hid_live_fixture_t shared = hid_live_fixtures[5];
    shared.subclass = HID_SUBCLASS_BOOTIF; shared.protocol = HID_PROTOCOL_KEYBOARD;
    memset(hid_protocol_calls, 0, sizeof(hid_protocol_calls));
    hid_fixture_connect(&shared, 0, &port, &endpoint, &hid);
    assert(g_slots[0].keyboard && g_slots[0].mouse && g_slots[0].gamepad && g_slots[0].report_protocol);
    assert(hid_protocol_calls[HID_PROTOCOL_REPORT] == 1U && hid_protocol_calls[HID_PROTOCOL_BOOT] == 0U);
    const uint8_t keys[] = {1, 2, 0, 4, 0, 0, 0, 0, 0};
    hid_fixture_report(0, keys, sizeof(keys));
    assert(iigs_key_held(0, 4) && iigs_key_held(0, 0xe1));
    usbh_hid_stop(&hid);
    puts("PASS boot/report negotiation, malformed-descriptor fallback and combined keyboard/axis reports");
}

static void test_hid_report_atomicity_and_nkro(void)
{
    struct usbh_hubport port;
    struct usb_endpoint_descriptor endpoint;
    struct usbh_hid hid;
    usb_hid_service_init(NULL); usb_hid_service_set_active_core(MT_IIGS_CORE_ID);
    uint8_t shared_id[sizeof(hid_live_descriptor_5)];
    memcpy(shared_id, hid_live_descriptor_5, sizeof(shared_id));
    unsigned changed = 0;
    for (unsigned i = 0; i + 1U < sizeof(shared_id); ++i)
        if (shared_id[i] == 0x85U && shared_id[i + 1U] == 2U) { shared_id[i + 1U] = 1U; ++changed; }
    assert(changed == 1U);
    mt_hid_live_fixture_t fixture = hid_live_fixtures[5];
    fixture.descriptor = shared_id;
    hid_fixture_connect(&fixture, 0, &port, &endpoint, &hid);
    const uint8_t report[] = {1, 2, 0, 4, 0, 0, 0, 0, 0, 1, 7, 0xf8, 1};
    hid_fixture_report(0, report, 9U); /* Complete keyboard prefix, missing mouse suffix. */
    assert(!iigs_key_held(0, 4) && !iigs_key_held(0, 0xe1) && g_gamepads.raw[0].buttons == 0U);
    hid_fixture_report(0, report, sizeof(report));
    assert(iigs_key_held(0, 4) && iigs_key_held(0, 0xe1) && g_gamepads.raw[0].buttons == 1U);
    assert(g_iigs_input.mouse_x == 7 && g_iigs_input.mouse_y == 8);
    usbh_hid_stop(&hid);

    static const uint8_t nkro[] = {
        0x05, 1, 0x09, 6, 0xa1, 1, 0x05, 7, 0x19, 0xe0, 0x29, 0xe7,
        0x15, 0, 0x25, 1, 0x75, 1, 0x95, 8, 0x81, 2,
        0x19, 4, 0x29, 0x83, 0x95, 128, 0x81, 2, 0xc0
    };
    fixture = hid_live_fixtures[0]; fixture.subclass = 0; fixture.protocol = 0;
    fixture.descriptor = nkro; fixture.length = sizeof(nkro); fixture.packet_bytes = 32U;
    hid_fixture_connect(&fixture, 0, &port, &endpoint, &hid);
    assert(g_slots[0].keyboard && g_slots[0].report_protocol && !g_slots[0].gamepad);
    uint8_t nkro_report[17] = {5, 255, 3};
    hid_fixture_report(0, nkro_report, sizeof(nkro_report));
    for (unsigned key = 4; key < 14U; ++key) assert(iigs_key_held(0, key));
    assert(iigs_key_held(0, 0xe0) && iigs_key_held(0, 0xe2));
    memset(nkro_report, 0, sizeof(nkro_report));
    hid_fixture_report(0, nkro_report, sizeof(nkro_report));
    for (unsigned key = 4; key < 14U; ++key) assert(!iigs_key_held(0, key));
    assert(!iigs_key_held(0, 0xe0) && !iigs_key_held(0, 0xe2));
    usbh_hid_stop(&hid);
    puts("PASS atomic mixed keyboard/mouse reports and report-only NKRO press/release");
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
    identify_controller(slot, &port, 0U, transport, 1U, present, 0U, false);
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
        assert(fixture.menu.page == MT_MENU_PAGE_ABOUT && fixture.tab_presses == 1U);
        buttons(first, 0x10U);
        assert(fixture.menu.page == MT_MENU_PAGE_ABOUT && fixture.tab_presses == 1U);
        buttons(first, 0x20U); /* Direct left-to-right change still has a Tab edge. */
        assert(fixture.menu.page == MT_MENU_PAGE_CORES && fixture.tab_presses == 2U);
        buttons(first, 0x30U); /* Both held cancel; releasing one restores direction. */
        assert(fixture.menu.page == MT_MENU_PAGE_CORES && fixture.tab_presses == 2U);
        buttons(first, 0x10U);
        assert(fixture.menu.page == MT_MENU_PAGE_ABOUT && fixture.tab_presses == 3U);
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
    assert(g_slots[0].mouse && g_slots[0].gamepad && g_slots[0].generic_axes);
    g_reports[0][0]=1U;g_reports[0][1]=4U;g_reports[0][2]=(uint8_t)-5;
    report_complete(&g_slots[0],3);usb_hid_service_poll();
    assert(routed_code==0x4001U && (routed_value&255U)==5U && g_iigs_input.mouse_emitted_buttons==1U);
    assert(usb_hid_service_set_iigs_mouse_sensitivity(50U));
    g_reports[0][0]=1U;g_reports[0][1]=4U;g_reports[0][2]=(uint8_t)-6;g_reports[0][3]=127U;
    report_complete(&g_slots[0],4);usb_hid_service_poll();
    assert(((g_iigs_input.mouse_packet>>8U)&255U)==2U && (routed_value&255U)==3U);
    assert(g_iigs_input.mouse_emitted_buttons==1U); /* Wheel byte and buttons are unchanged. */
    assert(!usb_hid_service_set_iigs_mouse_sensitivity(125U) && g_iigs_input.mouse_scale_quarters==2U);
    assert(usb_hid_service_set_iigs_mouse_sensitivity(25U));
    g_reports[0][0]=1U;g_reports[0][1]=1U;g_reports[0][2]=1U;
    report_complete(&g_slots[0],3);usb_hid_service_poll();
    assert(g_iigs_input.mouse_fraction_x==1 && g_iigs_input.mouse_fraction_y== -1);
    usb_hid_service_set_menu_capture(true);usb_hid_service_poll();
    assert(g_iigs_input.mouse_emitted_buttons==0U);
    assert(g_iigs_input.mouse_fraction_x==0 && g_iigs_input.mouse_fraction_y==0);
    report_complete(&g_slots[0],3);usb_hid_service_poll();
    assert(g_iigs_input.mouse_x==0 && g_iigs_input.mouse_y==0 && g_iigs_input.mouse_fraction_x==0);
    usb_hid_service_set_menu_capture(false);
    g_reports[0][0]=1U;g_reports[0][1]=4U;g_reports[0][2]=(uint8_t)-5;
    report_complete(&g_slots[0],3);usb_hid_service_poll();
    assert(g_iigs_input.mouse_emitted_buttons==1U);
    usbh_hid_stop(&hid);assert(g_iigs_input.mouse_emitted_buttons==0U);
    assert(g_iigs_input.mouse_fraction_x==0 && g_iigs_input.mouse_fraction_y==0);
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
    test_shared_poll_deadline();
    test_transport_slots();
    test_controller_routing();
    test_identity_strings_and_spare();
    test_hid_fields();
    test_hid_collection_fields();
    test_hid_axis_applications();
    test_hid_descriptor_bounds();
    test_hid_keyboard_chunks();
    test_hid_live_descriptors();
    test_hid_live_runtime();
    test_hid_report_protocol_fallback();
    test_hid_report_atomicity_and_nkro();
    test_keyboard_screenshot();
    test_menu_chord();
    test_menu_chord_guards();
    test_shoulder_navigation();
    test_iigs_input_transport();
    test_nes_input_transport();
    return 0;
}
