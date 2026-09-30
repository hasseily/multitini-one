/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute transport callbacks, routing and menu capture with real adapters. */
#include <assert.h>
#include <stdio.h>
#include "../../firmware/platform/xilinx/usb_hid_service.c"
static XTime fake_now;
static unsigned submits;
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
int usbh_submit_urb(struct usbh_urb *urb) { (void)urb; ++submits; return 0; }
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

int main(void)
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

    mt_gamepad_report_t report = {0};
    report.buttons_valid = 1U; report.buttons = 1U;
    usb_gamepad_input_report((uint8_t)vendor, &report, 2U, 0U);
    assert(g_gameboy_input.gamepads[vendor] == 0x18U);
    assert(g_vectrex_input.pad_buttons[vendor] == 1U && g_vectrex_input.pad_x[vendor] == INT16_MAX);
    assert(g_genesis_input.gamepad[vendor] == (MT_GENESIS_A | MT_GENESIS_RIGHT));
    usb_hid_service_set_menu_capture(true);
    assert(g_gameboy_input.gamepads[vendor] == 0U && g_vectrex_input.pad_buttons[vendor] == 0U);
    assert(g_genesis_input.gamepad[vendor] == 0U);
    assert(g_gamepads.raw[vendor].buttons == 1U); /* preview remains live */
    usb_hid_service_set_menu_capture(false);
    assert(g_gameboy_input.gamepads[vendor] == 0U); /* held menu button blocked */
    report.buttons = 0U;
    usb_gamepad_input_report((uint8_t)vendor, &report, 8U, 0U);
    report.buttons = 2U;
    usb_gamepad_input_report((uint8_t)vendor, &report, 8U, 0U);
    assert(g_gameboy_input.gamepads[vendor] == 0x20U);
    assert(g_genesis_input.gamepad[vendor] == MT_GENESIS_B);
    {
        mt_core_link_t link;
        mt_core_link_init(&link, NULL, input_read, input_write);
        g_genesis_input.context = &link;
        usb_hid_service_set_active_core(MT_GENESIS_CORE_ID);
        usb_hid_service_set_core_ready(true);
        assert(routed_events == 2U && routed_code == 0x3001U && routed_value == 0U);
        report.buttons = 0x24U; /* X and RB must remain separate C and Z. */
        usb_gamepad_input_report((uint8_t)vendor, &report, 8U, 0U);
        assert(routed_code == 0x3000U && routed_value == (MT_GENESIS_C | MT_GENESIS_Z));
        assert(emit_active_input(&link, 0x1000U, 1U) == -1);
        usb_hid_service_set_active_core(MT_GBC_CORE_ID);
        assert(emit_active_input(&link, 0x3000U, 1U) == -1);
        usb_hid_service_set_active_core(MT_GENESIS_CORE_ID);
        usb_hid_service_set_core_ready(true);
        usb_hid_service_set_menu_capture(true);
        assert(routed_value == 0U);
        usb_hid_service_set_menu_capture(false);
        report.buttons = 0U;
        usb_gamepad_input_report((uint8_t)vendor, &report, 8U, 0U);
        report.buttons = 1U;
        usb_gamepad_input_report((uint8_t)vendor, &report, 8U, 0U);
        assert(routed_value == MT_GENESIS_A);
        usb_gamepad_input_disconnect((uint8_t)vendor);
        assert(routed_value == 0U);
        usb_hid_service_set_core_ready(false);
        g_genesis_input.context = NULL;
    }
    /* Shared slot disconnect cannot release either neighboring transport. */
    usb_gamepad_input_disconnect((uint8_t)vendor);
    assert(!g_gamepads.raw[vendor].connected && (g_gamepads.allocated & 0x102U) == 0x102U);
    usbh_hid_stop(&hid); usbh_xbox_stop(&xbox);
    assert(g_gamepads.allocated == 0U);
    puts("PASS USB HID/XInput backoff, mixed slots, release guard and core routing");
    return 0;
}
