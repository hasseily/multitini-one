/* SPDX-License-Identifier: GPL-3.0-only */
#include "cherryusb_ulpi.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint8_t otg;

    assert(cherryusb_ulpi_wakeup_command() == UINT32_C(0xA0000000));
    assert(cherryusb_ulpi_read_command(UINT8_C(0x0A)) ==
           UINT32_C(0x400A0000));
    assert(cherryusb_ulpi_write_command(CHERRYUSB_ULPI_REG_OTG_SET,
                                         UINT8_C(0x66)) ==
           UINT32_C(0x600B0066));

    otg = cherryusb_usb3300_host_otg_control(UINT8_C(0x00));
    assert(otg == UINT8_C(0x66));
    assert(cherryusb_usb3300_vbus_is_enabled(otg) == UINT8_C(1));

    otg = cherryusb_usb3300_host_otg_control(UINT8_C(0xFF));
    assert(otg == UINT8_C(0x66));
    assert((otg & CHERRYUSB_ULPI_OTG_HOST_CLEAR) == 0U);
    assert(cherryusb_usb3300_vbus_is_enabled(UINT8_C(0x20)) == UINT8_C(1));
    assert(cherryusb_usb3300_host_interface_control(UINT8_C(0x00)) ==
           UINT8_C(0x10));
    assert(cherryusb_usb3300_host_interface_control(UINT8_C(0x70)) ==
           UINT8_C(0x10));
    assert(cherryusb_usb3300_vbus_is_enabled(UINT8_C(0x00)) == UINT8_C(0));
    assert(CHERRYUSB_ULPI_FUNCTION_HOST == UINT8_C(0x41));
    assert(CHERRYUSB_ULPI_OTG_HOST_CLEAR == UINT8_C(0x99));

    puts("cherryusb_ulpi: ok");
    return 0;
}
