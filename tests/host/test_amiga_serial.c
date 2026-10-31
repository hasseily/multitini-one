/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_menu.h"
#include "multitini_amiga_interface.h"
#include "core_catalog_sd.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static mt_core_catalog_sd_t core_catalog;
static bool usb_sd_modal,external,amiga_service_initialized=true;
static unsigned switches,exports,media_statuses,actions;
static unsigned shots;
static bool shot_busy,shot_available=true,shot_output;
static mt_menu_action_t last_action;
static uint32_t active_core=MT_AMIGA_CORE_ID;
static uint32_t shell_read(uint32_t reg) { assert(reg==MT_REG_CORE_ID);return active_core; }
bool mt_sd_storage_external_active(void) { return external; }
static int xil_printf(const char *format,...) { (void)format;return 0; }
static void mount_sd_over_usb(bool serial_entry) { assert(serial_entry);++exports; }
static void print_status(void) {}
static bool mt_screenshot_service_busy(void) { return shot_busy; }
static bool mt_screenshot_service_request(bool output)
{ ++shots;shot_output=output;return shot_available; }
static const char *mt_screenshot_service_status(void) { return "Capturing screenshot..."; }
static void print_amiga_media_status(void) { ++media_statuses; }
static bool legacy_c64_media_command(const char *line) { return strncmp(line,"mount ",6U)==0; }
static void handle_menu_action(mt_menu_action_t action)
{ last_action=action;++actions;if(action.kind==MT_MENU_ACTION_SWITCH_CORE)++switches; }
#include "amiga_serial.inc"
int main(void)
{
    core_catalog.count=1U;
    strcpy(core_catalog.cores[0].package.id,"amiga");
    core_catalog.cores[0].package.variant_count=1U;
    strcpy(core_catalog.cores[0].package.variants[0].id,"native");
    assert(supervisor_serial_command("cores"));
    assert(supervisor_serial_command("core amiga native"));
    assert(switches==1U && last_action.core_index==0U && last_action.variant_index==0U);
    assert(supervisor_serial_command("core amiga"));assert(switches==2U);
    const char *const invalid[]={"core ami","core amiga bad","core amiga native extra","core ",
        "core aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for(unsigned i=0U;i<sizeof(invalid)/sizeof(invalid[0]);++i)assert(supervisor_serial_command(invalid[i]));
    assert(switches==2U && !supervisor_serial_command("coreamiga"));
    assert(supervisor_serial_command("shot") && shots==1U && !shot_output);
    assert(supervisor_serial_command("shot source") && shots==2U && !shot_output);
    assert(supervisor_serial_command("shot output") && shots==3U && shot_output);
    assert(supervisor_serial_command("shot output extra") && shots==3U);
    assert(supervisor_serial_command("shot ") && shots==3U && !supervisor_serial_command("shotty"));
    shot_busy=true;assert(supervisor_serial_command("shot") && shots==3U);shot_busy=false;
    shot_available=false;assert(supervisor_serial_command("shot") && shots==4U);shot_available=true;
    external=true;assert(supervisor_serial_command("core amiga native"));assert(supervisor_serial_command("usbsd"));
    assert(supervisor_serial_command("shot output") && shots==4U);
    assert(switches==2U && exports==0U);external=false;
    usb_sd_modal=true;assert(supervisor_serial_command("usbsd") && exports==0U);usb_sd_modal=false;
    assert(supervisor_serial_command("usbsd") && exports==1U);
    assert(amiga_serial_command("disk") && media_statuses==1U);
    assert(amiga_serial_command("romload") && last_action.kind==MT_MENU_ACTION_AMIGA_RELOAD);
    assert(amiga_serial_command("diskretry") && last_action.kind==MT_MENU_ACTION_AMIGA_RETRY);
    const unsigned before=actions;assert(amiga_serial_command("mount disk.adf") && actions==before);
    active_core=0U;assert(!amiga_serial_command("romload") && actions==before);
    puts("PASS bounded exact core/variant serial selection, USB lease refusal and Amiga lifecycle dispatch");return 0;
}
