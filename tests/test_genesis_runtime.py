# SPDX-License-Identifier: GPL-3.0-only
"""Run production Genesis reset, switch and controller-setting transactions."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}", start) + 2]


FIXTURE = r'''
#include "genesis_content_service.h"
#include "supervisor_settings.h"
#include "multitini_supervisor.h"
#include "multitini_c64_interface.h"
#include "multitini_c64_debug.h"
#include "multitini_gameboy_interface.h"
#include "multitini_vectrex_interface.h"
#include "multitini_nes_interface.h"
#include "multitini_iigs_interface.h"
#include "multitini_ui.h"
#include "pl_loader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t current_core, control, status;
static bool genesis_service_initialized, gameboy_service_initialized;
static bool nes_service_initialized, iigs_service_initialized;
static bool c64_services_initialized, c64_core_reset_active;
static bool c64_iec_initialized;
static mt_c64_debug_t c64_debug;
static bool c64_debug_pause_active;
void mt_c64_debug_deactivate(mt_c64_debug_t *debug) { assert(debug == &c64_debug); }
static bool hide_allowed, prepare_fail, resume_fail, command_fail, save_fail;
static bool input_ready, attached;
static bool screenshot_cancel_allowed;
static unsigned screenshot_cancels;
static unsigned prepares, resumes, commands, loads, settings_saves;
static bool effects_quiet, source_quiet, supervisor_menu_dirty;
static unsigned drain_polls, source_release_poll, source_status_reads;
static unsigned pcap_loads, invalidations, effect_restores;
static uint64_t fake_ms;
static mt_ui_presenter_t supervisor_presenter;
static mt_supervisor_t supervisor;
static mt_supervisor_settings_t persistent_settings, settings_before_update;
static mt_genesis_config_t options, loaded_options[8];
static mt_genesis_content_service_status_t load_statuses[8];
static char trace[32], last_error[80];
static size_t trace_size;
static void mark(char value) {assert(trace_size < sizeof(trace)-1U); trace[trace_size++]=value; trace[trace_size]=0;}
static uint32_t shell_read(uint32_t offset) {
    if(offset==MT_REG_CORE_ID) return current_core;
    if(offset==MT_REG_ABI_VERSION) return 23U;
    if(offset==MT_REG_C64_EXPANSION_CAPS) return 0U;
    if(offset==MT_REG_STATUS) return status;
    if(offset==MT_REG_VIDEO_FRAME_STATUS) {
        ++source_status_reads;
        return source_quiet ? MT_VIDEO_FRAME_STATUS_QUIESCENT : 0U;
    }
    assert(offset==MT_REG_CONTROL); return control;
}
static bool shell_is_c64(void) {return current_core==MT_C64_CORE_ID;}
static bool hide_menu_for_core_action(void) {return hide_allowed;}
static bool mt_screenshot_service_cancel(void) {++screenshot_cancels;return screenshot_cancel_allowed;}
static int xil_printf(const char *format, ...) {(void)format; return 0;}
static int c64_d64_service_prepare_reset(void) {assert(0); return -1;}
static int c64_d64_service_resume_after_reset(void) {assert(0); return -1;}
static void c64_d64_service_notify_core_reset(void) {assert(0);}
static int quiesce_c64_directory(void) {assert(0); return -1;}
static int resume_c64_directory(void) {assert(0); return -1;}
static int c64_iec_service_reset(void) {assert(0); return -1;}
static int gameboy_content_service_prepare_reset(void) {assert(0); return -1;}
static int gameboy_content_service_resume_after_reset(void) {assert(0); return -1;}
static int nes_content_service_prepare_reset(void) {assert(0); return -1;}
static int nes_content_service_resume_after_reset(void) {assert(0); return -1;}
static int iigs_content_service_prepare_reset(void) {assert(0); return -1;}
static int iigs_content_service_resume_after_reset(void) {assert(0); return -1;}
int genesis_content_service_prepare_reset(void) {++prepares; mark('P'); return prepare_fail ? -1:0;}
int genesis_content_service_resume_after_reset(void) {++resumes; mark('R'); return resume_fail ? -1:0;}
static int command(uint32_t op,uint32_t arg0,uint32_t arg1) {
    assert(op==MT_COMMAND_CORE_RESET); (void)arg1; ++commands; mark('C');
    if(command_fail) return -1;
    if(arg0) {control|=MT_CONTROL_CORE_RESET_REQUEST; status=0U;}
    else {control&=~MT_CONTROL_CORE_RESET_REQUEST; status=MT_STATUS_CORE_READY;}
    return 0;
}
static void usb_hid_service_set_core_ready(bool ready) {input_ready=ready; mark('I');}
static void set_audio(bool muted,bool tone) {assert(muted);(void)tone;mark('M');}
const mt_genesis_config_t *genesis_content_service_options(void) {return &options;}
int genesis_content_service_set_options(const mt_genesis_config_t *value) {options=*value;return 0;}
const char *genesis_content_service_cartridge_name(void) {return attached ? "Original.md":NULL;}
const char *genesis_content_service_phase_name(mt_genesis_transfer_phase_t phase) {(void)phase;return "test";}
const char *mt_link_result_name(mt_link_result_t result) {(void)result;return "test";}
bool genesis_content_service_cartridge_attached(void) {return attached;}
mt_genesis_content_service_result_t genesis_content_service_load(const char *name) {
    mt_genesis_content_service_result_t result={0};
    assert(strcmp(name,"Original.md")==0 && loads<8U);
    loaded_options[loads]=options;
    result.status=load_statuses[loads++];
    if(result.status==MT_GENESIS_CONTENT_SERVICE_OK) {
        attached=true;control=0U;status=MT_STATUS_CORE_READY;
    } else if(result.status==MT_GENESIS_CONTENT_SERVICE_TRANSFER_FAILED) {
        attached=false;control=MT_CONTROL_CORE_RESET_REQUEST;status=0U;
    }
    return result;
}
static bool ensure_active_settings_profile(void) {return true;}
bool mt_supervisor_settings_remember_genesis_pads(mt_supervisor_settings_t *settings,
    const char *id,uint8_t mask) {assert(strcmp(id,"genesis")==0);settings->profiles[0].genesis_three_button_mask=mask;return true;}
static bool save_persistent_settings(void) {++settings_saves;return !save_fail;}
static void set_supervisor_error(const char *text) {snprintf(last_error,sizeof(last_error),"%s",text);}
static void set_supervisor_warning(const char *text) {set_supervisor_error(text);}
static void set_supervisor_status(const char *text) {(void)text;}
static void mt_video_effect_service_suspend(void) {}
static bool mt_video_effect_service_quiescent(void) {return effects_quiet;}
static uint64_t monotonic_ms(void *context) {(void)context;fake_ms+=100U;return fake_ms;}
static void usb_hid_service_poll_deferred(void) {
    ++drain_polls;
    if(source_release_poll!=0U && drain_polls>=source_release_poll) source_quiet=true;
}
static void configure_selection_effects(const mt_core_selection_t *selection) {
    assert(selection==&supervisor.active);++effect_restores;
}
mt_pl_load_result_t mt_pl_load_selection(const char *directory,const mt_core_variant_t *variant) {
    (void)directory;(void)variant;
    assert(effects_quiet && (current_core!=MT_GENESIS_CORE_ID || source_quiet));
    ++pcap_loads;return (mt_pl_load_result_t){0};
}
const char *mt_pl_load_status_name(mt_pl_load_status_t value) {(void)value;return "ok";}
void mt_ui_presenter_invalidate(mt_ui_presenter_t *presenter) {
    assert(presenter==&supervisor_presenter);++invalidations;
}
'''

CHECKS = r'''
static void reset_fixture(void) {
    current_core=MT_GENESIS_CORE_ID;control=0U;status=MT_STATUS_CORE_READY;
    genesis_service_initialized=true;gameboy_service_initialized=false;c64_services_initialized=false;
    hide_allowed=true;prepare_fail=resume_fail=command_fail=save_fail=false;
    input_ready=true;attached=true;
    screenshot_cancel_allowed=true;screenshot_cancels=0U;
    prepares=resumes=commands=loads=settings_saves=0U;trace_size=0U;trace[0]=last_error[0]=0;
    memset(&supervisor,0,sizeof(supervisor));supervisor.has_active=true;
    strcpy(supervisor.active.package.id,"genesis");supervisor.active.package.core_id=MT_GENESIS_CORE_ID;
    memset(&persistent_settings,0,sizeof(persistent_settings));
    memset(load_statuses,0,sizeof(load_statuses));memset(loaded_options,0,sizeof(loaded_options));
    options=(mt_genesis_config_t){MT_GENESIS_REGION_AUTO,3U,0U,1U};
    effects_quiet=true;source_quiet=false;supervisor_menu_dirty=false;
    drain_polls=source_release_poll=source_status_reads=0U;
    pcap_loads=invalidations=effect_restores=0U;fake_ms=0U;
}
int main(void) {
    reset_fixture();screenshot_cancel_allowed=false;
    assert(supervisor_quiesce(NULL)==-1 && screenshot_cancels==1U && prepares==0U && input_ready);
    reset_fixture();prepare_fail=true;
    assert(set_core_reset_request(true)==-1 && prepares==1U && commands==0U);
    assert(input_ready && control==0U);
    reset_fixture();command_fail=true;
    assert(set_core_reset_request(true)==-1 && prepares==1U && commands==1U && resumes==1U);
    assert(control==0U && input_ready);
    reset_fixture();assert(set_core_reset_request(true)==0 && prepares==1U && !input_ready);
    resume_fail=true;
    assert(set_core_reset_request(false)==-1 && resumes==1U && commands==1U);
    assert((control&MT_CONTROL_CORE_RESET_REQUEST)!=0U);
    resume_fail=false;
    assert(set_core_reset_request(false)==0 && resumes==2U && commands==2U);
    reset_fixture();prepare_fail=true;
    assert(supervisor_quiesce(NULL)==-1 && input_ready && strcmp(trace,"P")==0);
    reset_fixture();assert(supervisor_quiesce(NULL)==0);
    assert(trace[0]=='P' && trace[1]=='C' && strchr(trace,'M')>strchr(trace,'C'));
    assert(!input_ready);
    reset_fixture();load_statuses[0]=MT_GENESIS_CONTENT_SERVICE_REGION_MISMATCH;
    assert(load_genesis_content_with_input("Original.md").status==MT_GENESIS_CONTENT_SERVICE_REGION_MISMATCH);
    assert(input_ready && attached);
    reset_fixture();load_statuses[0]=MT_GENESIS_CONTENT_SERVICE_TRANSFER_FAILED;
    (void)load_genesis_content_with_input("Original.md");assert(!input_ready && !attached);
    reset_fixture();change_genesis_controller(0U);
    assert(loads==1U && options.six_button_mask==2U && input_ready);
    assert(persistent_settings.profiles[0].genesis_three_button_mask==1U && settings_saves==1U);
    reset_fixture();save_fail=true;change_genesis_controller(1U);
    assert(loads==2U && loaded_options[0].six_button_mask==1U && loaded_options[1].six_button_mask==3U);
    assert(options.six_button_mask==3U && persistent_settings.profiles[0].genesis_three_button_mask==0U && input_ready);
    assert(strstr(last_error,"previous controller restored")!=NULL);
    reset_fixture();load_statuses[0]=MT_GENESIS_CONTENT_SERVICE_TRANSFER_FAILED;change_genesis_controller(0U);
    assert(loads==2U && options.six_button_mask==3U && attached && input_ready && settings_saves==0U);
    reset_fixture();load_statuses[0]=MT_GENESIS_CONTENT_SERVICE_SAVE_FAILED;change_genesis_controller(0U);
    assert(loads==1U && options.six_button_mask==3U && input_ready && settings_saves==0U);
    reset_fixture();attached=false;change_genesis_controller(1U);
    assert(loads==0U && settings_saves==1U && options.six_button_mask==1U);
    reset_fixture();change_genesis_controller(2U);assert(loads==0U && settings_saves==0U);
    /* Even an already-OFF effects service must wait for the independent HP2
     * master; the raw status includes it after supervisor_quiesce resets MD. */
    reset_fixture();control=MT_CONTROL_CORE_RESET_REQUEST;source_release_poll=4U;
    assert(supervisor_load(NULL,&supervisor.active)==0);
    assert(drain_polls==4U && source_status_reads>=5U && pcap_loads==1U);
    assert(invalidations==1U && supervisor_menu_dirty && effect_restores==0U);
    reset_fixture();control=MT_CONTROL_CORE_RESET_REQUEST;
    assert(supervisor_load(NULL,&supervisor.active)==-1);
    assert(drain_polls==10U && pcap_loads==0U && invalidations==0U && effect_restores==1U);
    reset_fixture();current_core=MT_VECTREX_CORE_ID;
    assert(supervisor_load(NULL,&supervisor.active)==0);
    assert(source_status_reads==0U && drain_polls==0U && pcap_loads==1U);
    return 0;
}
'''


class GenesisRuntimeTests(unittest.TestCase):
    def test_production_transactions(self):
        compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
        self.assertIsNotNone(compiler, "host C compiler is required")
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        signatures = [
            "static bool core_requires_ddr_drain(void)",
            "static int set_core_reset_request(bool asserted)",
            "static int supervisor_quiesce(void *context)",
            "static int supervisor_load(void *context,\n                           const mt_core_selection_t *selection)",
            "static mt_genesis_content_service_result_t load_genesis_content_with_input(const char *name)",
            "static void change_genesis_controller(size_t player)",
        ]
        with tempfile.TemporaryDirectory(prefix="genesis-runtime-") as directory:
            c_file = Path(directory) / "runtime.c"
            binary = Path(directory) / "runtime"
            c_file.write_text(FIXTURE + "\n".join(function(source, s) for s in signatures) + CHECKS)
            compiled = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Ifirmware/include", "-Ifirmware/platform/xilinx", str(c_file), "-o", str(binary)],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0,
                             f"Runtime fixture compilation failed:\n{compiled.stdout}{compiled.stderr}")
            executed = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0,
                             f"Runtime fixture failed:\n{executed.stdout}{executed.stderr}")


if __name__ == "__main__":
    unittest.main()
