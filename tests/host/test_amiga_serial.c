/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_menu.h"
#include "multitini_amiga_interface.h"
#include "multitini_amiga_boot.h"
#include "amiga_display_service.h"
#include "amiga_content_service.h"
#include "core_catalog_sd.h"
#include "multitini_ui.h"
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
static mt_menu_action_t pending_menu_action;
static bool supervisor_menu_action_active,shortcut_pending;
static bool usb_hid_service_take_amiga_reset(void) { const bool next=shortcut_pending;shortcut_pending=false;return next; }
static uint32_t active_core=MT_AMIGA_CORE_ID;
static uint32_t host_status=MT_AMIGA_HOST_DISPLAY;
static unsigned benches,last_iterations;
static bool bench_ok=true,bench_ready=true,bench_fault;
static uint32_t shell_control,shell_ui=MT_UI_STATUS_DENSE_LAYOUT,shell_status=MT_STATUS_CORE_READY;
static bool display_pending;
static mt_amiga_ide_profile_t ide_profile;
static unsigned profile_starts,profile_stops,profile_start_messages;
static bool profile_start_fails;
static char profile_output[16000];
static size_t profile_output_length;
static mt_amiga_config_t cpu_config={MT_AMIGA_CPU_68020_TURBO,0U,0U,0U,8U,MT_AMIGA_IDE_AUTO};
static bool cpu14_available;
static bool fast_ide_available;
static unsigned cpu_changes;
static unsigned config_reads;
static uint32_t config_readback=0x166bU;
static char ide_output[4096];
static size_t ide_output_length;
const mt_amiga_config_t *amiga_content_service_config(void) { return &cpu_config; }
bool amiga_content_service_cpu14_available(void) { return cpu14_available; }
bool amiga_content_service_cpu14_fallback(void) { return false; }
bool amiga_content_service_fast_ide_available(void) { return fast_ide_available; }
bool amiga_content_service_fast_ide_expected(void)
{ return fast_ide_available && cpu_config.cpu!=MT_AMIGA_CPU_STOCK && cpu_config.ide_compat==MT_AMIGA_IDE_AUTO; }
static int apply_amiga_config(const mt_amiga_config_t *c) { ++cpu_changes;cpu_config=*c;return 0; }
/* Match the BSP's unparenthesized, two-level clock expression. */
#define XPAR_CPU_CORE_CLOCK_FREQ_HZ UINT64_C(666666687)
#define XSLEEPTIMER_FREQ XPAR_CPU_CORE_CLOCK_FREQ_HZ/2
#define COUNTS_PER_SECOND XSLEEPTIMER_FREQ
static unsigned bench_reports;
static unsigned bench_input_releases,bench_display_restarts;
static void usb_hid_service_set_core_ready(bool ready) { if(!ready)++bench_input_releases; }
static void amiga_display_guest_restarted(void) { ++bench_display_restarts; }
static uint64_t amiga_bench_ticks(void *context) { assert(context==NULL);return 42U; }
int amiga_content_service_profile_start(uint64_t (*timer)(void *),void *context)
{
    assert(timer==amiga_bench_ticks && context==NULL && !ide_profile.running);
    assert(profile_start_messages>profile_starts);++profile_starts;
    if(profile_start_fails)return -1;
    memset(&ide_profile,0,sizeof(ide_profile));
    ide_profile.running=true;ide_profile.valid=true;ide_profile.reason=MT_AMIGA_PROFILE_RUNNING;
    return 0;
}
void amiga_content_service_profile_stop(void)
{ assert(ide_profile.running);++profile_stops;ide_profile.running=false;ide_profile.reason=MT_AMIGA_PROFILE_STOPPED; }
void amiga_content_service_profile_snapshot(mt_amiga_ide_profile_t *out) { *out=ide_profile; }
int amiga_content_service_ide_bench(unsigned iterations,uint64_t (*timer)(void *),void *context,mt_amiga_ide_bench_t *out)
{
    assert(timer==amiga_bench_ticks && context==NULL);++benches;last_iterations=iterations;
    memset(out,0,sizeof(*out));out->bytes_per_leg=UINT32_C(131072);
    const uint64_t counts=(uint64_t)(COUNTS_PER_SECOND);
    out->ticks[0][0]=counts;out->ticks[0][1]=counts*2U;out->ticks[1][0]=counts*4U;
    return bench_ok ? 0:-1;
}
bool amiga_content_service_ready(void) { return bench_ready; }
bool amiga_content_service_faulted(void) { return bench_fault; }
const char *amiga_content_service_error(void) { return "stub benchmark failure"; }
static uint32_t shell_read(uint32_t reg)
{
    if(reg==MT_AMIGA_HOST_PORT)return host_status;
    if(reg==MT_REG_STATUS)return shell_status;
    if(reg==MT_REG_CONTROL)return shell_control;
    if(reg==MT_REG_UI_STATUS)return shell_ui;
    assert(reg==MT_REG_CORE_ID);return active_core;
}
static mt_core_link_t shell_link;
static mt_menu_t supervisor_menu;
static bool supervisor_menu_dirty,menu_capture=true,hide_ok=true,display_accept=true,display_paused=true;
static unsigned hide_calls,display_requests,host_reads,host_clears,order,last_hide,last_resume,last_request;
static uint8_t display_modes=4U,last_display_mode;
static mt_link_result_t read_result=MT_LINK_OK;
static const mt_amiga_display_info_t display_info={1280U,512U,2U,0x29024U,0,0,0,0,0,0,111U};
static bool hide_menu_for_core_action(void)
{ ++hide_calls;last_hide=++order;return hide_ok; }
static void usb_hid_service_set_menu_capture(bool capture) { menu_capture=capture; }
static uint64_t monotonic_ms(void *context) { assert(context==NULL);return 60000U; }
static void set_supervisor_warning(const char *text) { assert(text!=NULL && !ide_profile.running); }
static void set_supervisor_status(const char *text) { assert(text!=NULL); }
void amiga_display_service_set_paused(bool paused,uint32_t now)
{ assert(!paused && now==60000U && !supervisor_menu.visible && !menu_capture);display_paused=paused;last_resume=++order; }
bool amiga_display_service_request(uint8_t mode,uint32_t now)
{ assert(!display_paused && now==60000U && !supervisor_menu.visible && !menu_capture);
  last_display_mode=mode;++display_requests;last_request=++order;return display_accept; }
bool amiga_display_service_available(void) { return true; }
uint8_t amiga_display_service_mode_count(void) { return display_modes; }
bool amiga_display_service_mode_available(uint8_t mode) { return mode<display_modes; }
bool amiga_display_service_pending(void) { return display_pending; }
uint8_t amiga_display_service_requested(void) { return last_display_mode; }
uint8_t amiga_display_service_applied(void) { return 0U; }
uint8_t amiga_display_service_saved(void) { return 0U; }
const char *amiga_display_service_status(void) { return "Ready"; }
const mt_amiga_display_info_t *amiga_display_service_info(void) { return &display_info; }
mt_link_result_t mt_amiga_host_access(mt_core_link_t *link,uint8_t opcode,uint32_t payload,
                                    uint32_t polls,uint32_t *response)
{
    assert(link==&shell_link && polls==100000U);
    if(opcode==MT_AMIGA_HOST_READ_CONFIG) { assert((cpu14_available || fast_ide_available) && payload==0U && response!=NULL);++config_reads;*response=config_readback;return read_result; }
    if(opcode==MT_AMIGA_HOST_CLEAR_ERROR) { assert(response==NULL && payload==0U);++host_clears;return MT_LINK_OK; }
    assert(opcode==MT_AMIGA_HOST_DISPLAY_READ && payload==host_reads%18U && response!=NULL);
    ++host_reads;*response=payload;return read_result;
}
const char *mt_link_result_name(mt_link_result_t result) { (void)result;return "stub result"; }
bool mt_sd_storage_external_active(void) { return external; }
static int xil_printf(const char *format,...)
{
    if(strncmp(format,"[Amiga IDE]",sizeof("[Amiga IDE]")-1U)==0) {
        va_list args;va_start(args,format);
        const int count=vsnprintf(ide_output+ide_output_length,sizeof(ide_output)-ide_output_length,format,args);va_end(args);
        assert(count>=0 && (size_t)count<sizeof(ide_output)-ide_output_length);ide_output_length+=(size_t)count;
    }
    if(strncmp(format,"[Amiga IDE profile]",sizeof("[Amiga IDE profile]")-1U)==0) {
        assert(!ide_profile.running);
        if(strstr(format,"starting after")!=NULL)++profile_start_messages;
        va_list args;va_start(args,format);
        const int count=vsnprintf(profile_output+profile_output_length,
            sizeof(profile_output)-profile_output_length,format,args);va_end(args);
        assert(count>=0 && (size_t)count<sizeof(profile_output)-profile_output_length);
        profile_output_length+=(size_t)count;
    }
    if(strncmp(format,"[Amiga IDE transport] %s %s bytes=",33U)==0) {
        static const uint32_t expected_us[]={1000000U,2000000U,4000000U,0U};
        static const uint32_t expected_kib[]={128U,64U,32U,0U};
        const unsigned leg=bench_reports%4U;
        va_list args;va_start(args,format);
        assert(strcmp(va_arg(args,const char *),leg<2U ? "legacy":"bulk")==0);
        assert(strcmp(va_arg(args,const char *),(leg&1U)==0U ? "ARM->IDE":"IDE->ARM")==0);
        assert(va_arg(args,unsigned)==131072U);
        assert(va_arg(args,unsigned)==expected_us[leg]);
        assert(va_arg(args,unsigned)==expected_kib[leg]);
        va_end(args);++bench_reports;
    }
    return 0;
}
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
static void profile_cli_tests(void)
{
    assert(!ide_profile.running && profile_starts==0U && profile_stops==0U);
    assert(amiga_serial_command("amigaideprof status"));
    assert(strstr(profile_output,"stopped valid=0")!=NULL);
    profile_output_length=0U;profile_output[0]='\0';
    const unsigned old_benches=benches,old_hide=hide_calls,old_releases=bench_input_releases;
    for(unsigned gate=0U;gate<10U;++gate) {
        usb_sd_modal=gate==0U;external=gate==1U;shot_busy=gate==2U;display_pending=gate==3U;
        supervisor_menu.visible=gate==4U;
        shell_ui=gate==5U ? MT_UI_STATUS_VISIBLE|MT_UI_STATUS_DENSE|MT_UI_STATUS_DENSE_LAYOUT:0x0fU;
        shell_control=gate==6U ? MT_CONTROL_CORE_RESET_REQUEST:0U;
        shell_status=gate==7U ? 0U:MT_STATUS_CORE_READY;
        bench_ready=gate!=8U;bench_fault=gate==9U;
        assert(amiga_serial_command("amigaideprof start") && profile_starts==0U);
    }
    usb_sd_modal=false;external=false;shot_busy=false;display_pending=false;supervisor_menu.visible=false;
    shell_ui=0x0fU;shell_control=0U;shell_status=MT_STATUS_CORE_READY;bench_ready=true;bench_fault=false;
    assert(!mt_ui_status_menu_shown(shell_ui)); /* Activity badge is not the dense menu. */
    assert(amiga_serial_command("amigaideprof start") && ide_profile.running && profile_starts==1U);
    ide_profile.read_bytes=77U;
    assert(amiga_serial_command("amigaideprof start") && !ide_profile.running && profile_stops==1U &&
           profile_starts==1U && ide_profile.read_bytes==77U);
    assert(amiga_serial_command("amigaideprof start") && ide_profile.running && profile_starts==2U);
    const uint64_t frequency=(uint64_t)(COUNTS_PER_SECOND);
    ide_profile.elapsed_ticks=frequency*2U;ide_profile.service_ticks=frequency;
    ide_profile.read_window_started=true;ide_profile.first_read_ticks=frequency/2U;
    ide_profile.last_read_ticks=frequency*3U/2U;ide_profile.active_read_ticks=frequency;
    ide_profile.read_window_bytes=1024U;
    ide_profile.outside_ticks=frequency;ide_profile.max_gap_ticks=frequency/2U;
    ide_profile.sd_read_ticks=frequency/4U;ide_profile.sd_read_calls=2U;ide_profile.read_bytes=1024U;
    ide_profile.fifo_ticks=frequency/2U;ide_profile.fifo_calls=2U;ide_profile.fifo_bytes=UINT64_MAX;
    ide_profile.req4=UINT64_C(4294967297);ide_profile.read_command_hist[8]=UINT64_MAX;
    ide_profile.drq_to_request_ticks=frequency;ide_profile.drq_intervals=1U;
    assert(amiga_serial_command("amigaideprof status") && !ide_profile.running && profile_stops==2U);
    assert(strstr(profile_output,"wall ticks=666666686 us=2000000")!=NULL);
    assert(strstr(profile_output,"active_read_window ticks=333333343 us=1000000 count=2 bytes=1024")!=NULL);
    assert(strstr(profile_output,"REQ4 ticks=0 us=0 count=4294967297")!=NULL);
    assert(strstr(profile_output,"bytes=18446744073709551615")!=NULL);
    assert(strstr(profile_output,"command_sectors=129..256 count=18446744073709551615")!=NULL);
    assert(strstr(profile_output,"DRQ_to_REQ_combined_NON_ADDITIVE")!=NULL &&
           strstr(profile_output,"not pure guest PIO time")!=NULL);
    assert(amiga_profile_microseconds(0U)==0U && amiga_profile_microseconds(frequency)==1000000U &&
           amiga_profile_microseconds(UINT64_MAX)==UINT64_C(55340230616261966));
    char decimal[21];assert(strcmp(amiga_profile_decimal(decimal,UINT64_MAX),"18446744073709551615")==0);
    assert(strcmp(amiga_profile_decimal(decimal,0U),"0")==0);
    assert(amiga_serial_command("amigaideprof stop") && profile_stops==2U);
    profile_start_fails=true;
    assert(amiga_serial_command("amigaideprof start") && profile_starts==3U && !ide_profile.running);
    profile_start_fails=false;
    const char *const invalid[]={"amigaideprof ","amigaideprof xyz","amigaideprof start extra","amigaideprof stop extra"};
    for(unsigned i=0U;i<sizeof(invalid)/sizeof(invalid[0]);++i)assert(amiga_serial_command(invalid[i]));
    assert(profile_starts==3U && profile_stops==2U && !amiga_serial_command("amigaideprofiler"));
    assert(amiga_serial_command("amigaideprof start") && ide_profile.running && profile_starts==4U);
    assert(amiga_serial_command("amigaideprof invalid") && !ide_profile.running && profile_stops==3U);
    assert(benches==old_benches && hide_calls==old_hide && bench_input_releases==old_releases);
    amiga_service_initialized=false;assert(!amiga_serial_command("amigaideprof start"));amiga_service_initialized=true;
}
int main(void)
{
    shortcut_pending=true;queue_amiga_reset_shortcut();
    assert(pending_menu_action.kind==MT_MENU_ACTION_RESET_CORE && !shortcut_pending && actions==0U);
    pending_menu_action.kind=MT_MENU_ACTION_NONE;supervisor_menu_action_active=true;shortcut_pending=true;
    queue_amiga_reset_shortcut();assert(pending_menu_action.kind==MT_MENU_ACTION_NONE && shortcut_pending);
    supervisor_menu_action_active=false;supervisor_menu.visible=true;queue_amiga_reset_shortcut();assert(shortcut_pending);
    supervisor_menu.visible=false;queue_amiga_reset_shortcut();assert(pending_menu_action.kind==MT_MENU_ACTION_RESET_CORE);
    pending_menu_action.kind=MT_MENU_ACTION_NONE;
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
    assert(amiga_serial_command("amigacpu status") && cpu_changes==0U && config_reads==0U);
    assert(amiga_serial_command("amigacpu 14") && cpu_changes==0U && cpu_config.cpu==2U);
    cpu14_available=true;assert(amiga_serial_command("amigacpu 14") && cpu_changes==1U && cpu_config.cpu==3U);
    assert(amiga_serial_command("amigacpu status") && config_reads==1U);
    assert(amiga_serial_command("amigacpu stock") && cpu_config.cpu==0U);
    assert(amiga_serial_command("amigacpu 28") && cpu_config.cpu==2U && cpu_changes==3U);
    assert(amiga_serial_command("amigacpu 14 extra") && amiga_serial_command("amigacpu 140") && cpu_changes==3U);
    external=true;assert(amiga_serial_command("amigacpu stock") && cpu_changes==3U);external=false;
    assert(!amiga_serial_command("amigacpustock"));
    /* Status must distinguish ARM policy/expectation from actual native
     * readback; FAST capability works independently of CPU14. */
    cpu14_available=false;const unsigned old_config_reads=config_reads;
    assert(amiga_serial_command("amigaide status") && config_reads==old_config_reads);
    assert(strstr(ide_output,"native fast readback unsupported")!=NULL);
    fast_ide_available=true;config_readback=0x066bU;ide_output_length=0U;ide_output[0]='\0';
    assert(amiga_serial_command("amigaide status") && config_reads==old_config_reads+1U &&
        strstr(ide_output,"expected=Fast")!=NULL && strstr(ide_output,"native_actual=Classic")!=NULL);
    config_readback|=0x1000U;read_result=MT_LINK_TIMEOUT;
    ide_output_length=0U;ide_output[0]='\0';
    assert(amiga_serial_command("amigaide status") && strstr(ide_output,"native readback failed")!=NULL &&
        strstr(ide_output,"native_actual=")==NULL);read_result=MT_LINK_OK;
    const unsigned before_ide=cpu_changes;
    assert(amiga_serial_command("amigaide classic") && cpu_changes==before_ide+1U && cpu_config.ide_compat==1U);
    assert(amiga_serial_command("amigaide auto") && cpu_changes==before_ide+2U && cpu_config.ide_compat==0U);
    const unsigned ide_hides=hide_calls;
    assert(amiga_serial_command("amigaide auto") && cpu_changes==before_ide+2U && hide_calls==ide_hides);
    assert(!amiga_serial_command("amigaideauto") && !amiga_serial_command("amigaideprofiler"));
    assert(amiga_serial_command("amigaide fast") && amiga_serial_command("amigaide auto extra") && cpu_changes==before_ide+2U);
    for(unsigned gate=0U;gate<5U;++gate) {
        usb_sd_modal=gate==0U;external=gate==1U;shot_busy=gate==2U;display_pending=gate==3U;hide_ok=gate!=4U;
        assert(amiga_serial_command("amigaide classic") && cpu_changes==before_ide+2U && cpu_config.ide_compat==0U);
    }
    usb_sd_modal=false;external=false;shot_busy=false;display_pending=false;hide_ok=true;
    media_statuses=1U;
    assert(amiga_serial_command("amigaidebench") && benches==0U);
    host_status|=MT_AMIGA_HOST_BULK;
    assert(amiga_serial_command("amigaidebench") && benches==1U && last_iterations==32U);
    assert(amiga_serial_command("amigaidebench 256") && benches==2U && last_iterations==256U);
    assert(bench_reports==8U);
    assert(bench_input_releases==2U && bench_display_restarts==2U);
    const char *invalid_bench[]={"amigaidebench 0","amigaidebench 257","amigaidebench 9999","amigaidebench -1","amigaidebench 1 extra","amigaidebench "};
    for(unsigned i=0U;i<sizeof(invalid_bench)/sizeof(invalid_bench[0]);++i)assert(amiga_serial_command(invalid_bench[i]));
    assert(benches==2U);external=true;assert(amiga_serial_command("amigaidebench") && benches==2U);external=false;
    bench_ready=false;assert(amiga_serial_command("amigaidebench") && benches==2U);bench_ready=true;
    bench_ok=false;assert(amiga_serial_command("amigaidebench 1") && benches==3U);bench_ok=true;
    assert(bench_reports==8U);
    assert(bench_input_releases==3U && bench_display_restarts==2U);
    profile_cli_tests();
    assert(amiga_serial_command("romload") && last_action.kind==MT_MENU_ACTION_AMIGA_RELOAD);
    assert(amiga_serial_command("diskretry") && last_action.kind==MT_MENU_ACTION_AMIGA_RETRY);
    assert(amiga_serial_command("amigadisplay") && host_reads==18U && host_clears==0U);
    host_status|=MT_AMIGA_HOST_ERROR;
    assert(amiga_serial_command("amigadisplay") && host_reads==18U && host_clears==0U);
    host_status=MT_AMIGA_HOST_BUSY|MT_AMIGA_HOST_DISPLAY;
    assert(amiga_serial_command("amigadisplay") && host_reads==18U && host_clears==0U);
    host_status=0U;assert(amiga_serial_command("amigadisplay") && host_reads==18U);
    host_status=MT_AMIGA_HOST_DISPLAY;read_result=MT_LINK_REMOTE_ERROR;
    assert(amiga_serial_command("amigadisplay") && host_reads==19U && host_clears==1U);
    read_result=MT_LINK_OK;
    supervisor_menu.visible=true;
    assert(amiga_serial_command("amigadisplay 3") && display_requests==1U && last_display_mode==3U);
    assert(!supervisor_menu.visible && supervisor_menu_dirty && !menu_capture);
    assert(last_hide<last_resume && last_resume<last_request);
    display_modes=3U;supervisor_menu.visible=true;menu_capture=true;display_paused=true;
    const unsigned old_hides=hide_calls;
    assert(amiga_serial_command("amigadisplay 3") && display_requests==1U && hide_calls==old_hides);
    assert(supervisor_menu.visible && menu_capture && display_paused);
    shot_busy=true;assert(amiga_serial_command("amigadisplay 0") && hide_calls==old_hides);shot_busy=false;
    hide_ok=false;assert(amiga_serial_command("amigadisplay 1") && display_requests==1U);hide_ok=true;
    display_accept=false;
    assert(amiga_serial_command("amigadisplay 2") && display_requests==2U);
    assert(supervisor_menu.visible && menu_capture);
    assert(!amiga_serial_command("amigadisplay 4") && !amiga_serial_command("amigadisplay 3 extra") &&
           !amiga_serial_command("amigadisplay ") && display_requests==2U);
    const unsigned before=actions;assert(amiga_serial_command("mount disk.adf") && actions==before);
    active_core=0U;assert(!amiga_serial_command("romload") && actions==before);
    puts("PASS exact serial dispatch, display diagnostics/UI ordering, profiler gates/freeze-before-output/full64 counts/BSP time/active-read window");return 0;
}
