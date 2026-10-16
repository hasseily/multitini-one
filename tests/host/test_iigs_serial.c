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
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#define MT_EXTENDED_COMMAND_CAPACITY 256U
static bool golden_update_worker_held;
static const char *test_command;
static unsigned forbidden,retries,warnings,statuses;
static unsigned profile_calls,unknown_commands;
static char profile_argument[MT_EXTENDED_COMMAND_CAPACITY];
static uint32_t active_core=MT_IIGS_CORE_ID;
static bool hidden=true;
static mt_iigs_config_t gs_config={1U,4U,0U,0U,0U,0U};
static uint32_t shell_read(uint32_t offset) { (void)offset;return active_core; }
static bool shell_is_c64(void) { return active_core==MT_C64_CORE_ID; }
static int xil_printf(const char *format,...)
{ if(strncmp(format,"unknown extended command",24U)==0)++unknown_commands;return 0; }
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
static void handle_iigs_profile(const char *argument)
{ assert(strlen(argument)<sizeof(profile_argument));strcpy(profile_argument,argument);++profile_calls; }
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
/* Match the selected BSP: division inside this macro is not parenthesized. */
typedef uint64_t XTime;
#define TEST_CPU_FREQ UINT32_C(666666687)
#define TEST_SLEEP_FREQ TEST_CPU_FREQ/2
#define COUNTS_PER_SECOND TEST_SLEEP_FREQ
static XTime clock_ticks;
static unsigned clock_reads,usb_progress_calls;
static bool iigs_progress_paced=true;
static uint64_t iigs_progress_last_us;
static void XTime_GetTime(XTime *value) { ++clock_reads;*value=clock_ticks; }
static void usb_hid_service_poll_deferred(void) { ++usb_progress_calls; }
#include "iigs_serial_handlers.inc"
static void set_clock_us(uint64_t microseconds)
{
    const uint64_t frequency=(uint64_t)COUNTS_PER_SECOND;
    clock_ticks=(microseconds/UINT64_C(1000000))*frequency+
        ((microseconds%UINT64_C(1000000))*frequency+UINT64_C(999999))/UINT64_C(1000000);
}
static void test_clock_and_progress(void)
{
    static const uint64_t microseconds[]={0U,1U,999U,1000U,999999U,1000000U,
        1999999U,2000000U,2000001U,7999999U,8000000U,
        UINT64_C(86400000000),UINT64_C(2000000000000)};
    for(size_t i=0U;i<sizeof(microseconds)/sizeof(microseconds[0]);++i) {
        set_clock_us(microseconds[i]);assert(monotonic_us(NULL)==microseconds[i]);
    }
    iigs_progress_paced=true;iigs_progress_last_us=0U;usb_progress_calls=0U;
    set_clock_us(500U);iigs_transfer_progress(NULL); /* First call always services USB. */
    assert(usb_progress_calls==1U);
    iigs_transfer_progress(NULL);assert(usb_progress_calls==1U);
    set_clock_us(1499U);iigs_transfer_progress(NULL);assert(usb_progress_calls==1U);
    set_clock_us(1500U);iigs_transfer_progress(NULL);assert(usb_progress_calls==2U);
    set_clock_us(2499U);iigs_transfer_progress(NULL);assert(usb_progress_calls==2U);
    set_clock_us(2500U);iigs_transfer_progress(NULL);assert(usb_progress_calls==3U);
    set_clock_us(100000U);iigs_transfer_progress(NULL);assert(usb_progress_calls==4U);
    iigs_progress_paced=false;
    const unsigned before=clock_reads;
    for(unsigned i=0U;i<4U;++i)iigs_transfer_progress(NULL);
    assert(usb_progress_calls==8U && clock_reads==before); /* Baseline polls every time. */
    iigs_progress_paced=true;
    iigs_transfer_progress(NULL);assert(usb_progress_calls==8U);
    set_clock_us(101000U);iigs_transfer_progress(NULL);assert(usb_progress_calls==9U);
}
int main(void)
{
    test_clock_and_progress();
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
    static const struct { const char *command,*argument; } profiling[]={
        {"gsprofile stop","stop"},{"gsprofile start","start"},
        {"gsprofile boot 6","boot 6"},{"gsprofile boot 0","boot 0"},
        {"gsprofile boot 4","boot 4"},{"gsprofile",""}};
    for(unsigned i=0U;i<sizeof(profiling)/sizeof(profiling[0]);++i) {
        test_command=profiling[i].command;const unsigned before=profile_calls;
        handle_extended_command(false);
        assert(profile_calls==before+1U && strcmp(profile_argument,profiling[i].argument)==0);
        assert(forbidden==0U && retries==2U);
    }
    const unsigned dispatched=profile_calls;
    for(unsigned i=0U;i<sizeof(profiling)/sizeof(profiling[0]);++i) {
        test_command=profiling[i].command;const unsigned before=unknown_commands;
        handle_extended_command(true);
        assert(profile_calls==dispatched && unknown_commands==before+1U && forbidden==0U);
    }
    static const uint32_t other_cores[]={MT_C64_CORE_ID,MT_NES_CORE_ID,0U};
    for(unsigned core=0U;core<sizeof(other_cores)/sizeof(other_cores[0]);++core) {
        active_core=other_cores[core];
        for(unsigned i=0U;i<sizeof(profiling)/sizeof(profiling[0]);++i) {
            test_command=profiling[i].command;const unsigned before=unknown_commands;
            handle_extended_command(false);
            assert(profile_calls==dispatched && unknown_commands==before+1U && forbidden==0U);
        }
    }
    active_core=MT_IIGS_CORE_ID;test_command="gsprofiles start";
    const unsigned before=unknown_commands;handle_extended_command(false);
    assert(profile_calls==dispatched && unknown_commands==before+1U && forbidden==0U);
    puts("PASS Apple IIgs serial routing, exact BSP diagnostic clock boundaries and live/baseline USB stream pacing");return 0;
}
