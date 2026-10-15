/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute extracted production command handlers. Every unrelated machine
 * mutation fails this fixture, including retained C64 state after a switch. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "c64_d64_service.h"
#include "c64_content_service.h"
#include "genesis_content_service.h"
#include "multitini_nes_interface.h"
#include "gameboy_content_service.h"
#include "iigs_content_service.h"
#include "multitini_gameboy_interface.h"
#define MT_EXTENDED_COMMAND_CAPACITY 256U
static bool golden_update_worker_held;
static const char *test_command;
static unsigned forbidden,retries,warnings,statuses;
static bool hidden=true;
static mt_iigs_config_t gs_config={1U,4U,0U,0U,0U,0U};
static uint32_t shell_read(uint32_t offset) { (void)offset;return MT_IIGS_CORE_ID; }
static bool shell_is_c64(void) { return false; }
static int xil_printf(const char *format,...) { (void)format;return 0; }
static bool read_extended_command(char *out,size_t n,bool recovery)
{ (void)recovery;assert(strlen(test_command)<n);strcpy(out,test_command);return true; }
static void leave_core_in_reset(void) { ++forbidden; }
static void reboot_to_golden(bool recovery) { (void)recovery;++forbidden; }
static void self_update_golden(bool recovery) { (void)recovery;++forbidden; }
static void change_display_mode(size_t mode) { (void)mode;++forbidden; }
static void print_read_only_diagnostics(const char *where) { (void)where; }
static void set_supervisor_error(const char *message) { (void)message;++warnings; }
static void set_supervisor_warning(const char *message)
{ assert(strcmp(message,"Use the Apple IIgs Media menu")==0);++warnings; }
static void set_supervisor_status(const char *message) { (void)message;++statuses; }
static void set_supervisor_outcome(bool success,const char *yes,const char *no)
{ (void)success;(void)yes;(void)no;++forbidden; }
static bool hide_menu_for_core_action(void) { return hidden; }
static void handle_c64_rx_command(const char *text) { (void)text;++forbidden; }
static void handle_nes_cart_detach(void) { ++forbidden; }
static void handle_nes_content_load(const char *text) { (void)text;++forbidden; }
int nes_content_service_retry_save(void) { ++forbidden;return 0; }
static void handle_genesis_cart_detach(void) { ++forbidden; }
static void handle_c64_cart_detach(void) { ++forbidden; }
static void handle_genesis_content_load(const char *text) { (void)text;++forbidden; }
static void handle_c64_content_load(const char *text) { (void)text;++forbidden; }
static bool remember_active_drive8(const char *text) { (void)text;++forbidden;return true; }
static void print_c64_content_result(const char *operation,const char *name,const mt_c64_content_service_result_t *result)
{ (void)operation;(void)name;(void)result;++forbidden; }
static void print_help(void) {}
void c64_d64_service_list(void) { ++forbidden; }
void c64_d64_service_dump_status(void) { ++forbidden; }
int c64_d64_service_retry(void) { ++forbidden;return 0; }
int c64_d64_service_resume_after_reset(void) { ++forbidden;return 0; }
int c64_d64_service_eject(void) { ++forbidden;return 0; }
int c64_d64_service_mount(const char *name) { (void)name;++forbidden;return 0; }
bool c64_content_service_prg_staged(void) { ++forbidden;return true; }
mt_c64_content_service_result_t c64_content_service_run_staged_prg(void)
{ mt_c64_content_service_result_t r={0};++forbidden;return r; }
int genesis_content_service_retry_save(void) { ++forbidden;return 0; }
int gameboy_content_service_retry_save(void) { ++forbidden;return 0; }
int iigs_content_service_retry(void) { ++retries;return 0; }
bool iigs_content_service_ready(void) { return true; }
bool iigs_content_service_faulted(void) { return false; }
const mt_iigs_config_t *iigs_content_service_config(void) { return &gs_config; }
const char *iigs_content_service_error(void) { return ""; }
const char *iigs_content_service_name(unsigned d) { assert(d<4U);return "disk"; }
static bool c64_drive_ready=true; /* Retained state must not be used by GS. */
#include "iigs_serial_handlers.inc"
int main(void)
{
    static const char *const legacy[]={"mount OLD.D64","mount\tOLD.G64","mount","eject","cartoff",
        "load OLD.PRG","load\tOLD.CRT","load","c64rx 20480 0x1234","c64rx"};
    for(unsigned i=0U;i<sizeof(legacy)/sizeof(legacy[0]);++i) {
        test_command=legacy[i];const unsigned before=warnings;
        handle_extended_command(false);assert(warnings==before+1U && forbidden==0U && retries==0U);
    }
    test_command="disk";handle_extended_command(false);assert(forbidden==0U);
    test_command="dir";handle_extended_command(false);assert(forbidden==0U);
    test_command="diskretry";handle_extended_command(false);assert(retries==1U && statuses==1U && forbidden==0U);
    test_command="saveretry";handle_extended_command(false);assert(retries==2U && statuses==2U && forbidden==0U);
    hidden=false;test_command="diskretry";handle_extended_command(false);assert(retries==2U && forbidden==0U);
    puts("PASS Apple IIgs serial routing excludes retained C64 services and retries only after menu release");return 0;
}
