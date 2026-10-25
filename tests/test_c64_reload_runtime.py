# SPDX-License-Identifier: GPL-3.0-only
"""Run the real main recovery helpers and supervisor transition with mock owners."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}", start) + 2]


HARNESS = r'''
#include "multitini_supervisor.h"
#include "multitini_regs.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static mt_supervisor_t supervisor;
static bool usb_sd_modal, c64_uci_initialized, c64_title_initialized, c64_iec_initialized;
static bool c64_debug_pause_active, c64_services_initialized;
static bool is_c64, reload_required, hold, softiec_lease, cleaned;
static bool screenshot_ok, video_ready, core_ready, network_running;
static int disk_error, reset_error, close_error, directory_error, retry_error;
static unsigned prepare_pending, retry_pending;
static unsigned disk_calls, reset_calls, prepare_calls, directory_calls;
static unsigned normal_reset_calls, ordinary_uci_resets, shutdowns, title_clears;
static unsigned loads, probes, activations, retries, polls, directory_resets;
static uint32_t control;
static uint64_t now_ms;
static struct { struct { bool reload_required; } reu_host; } c64_debug;
static char events[128];
static size_t event_count;
static void event(char c) { assert(event_count+1<sizeof(events));events[event_count++]=c;events[event_count]=0; }
static bool shell_is_c64(void) { return is_c64; }
static bool c64_uci_service_reload_required(void) { return reload_required; }
static uint32_t shell_read(uint32_t reg) { assert(reg==MT_REG_CONTROL);return control; }
static uint64_t monotonic_ms(void *p) { assert(!p);return now_ms; }
static void usb_hid_service_poll_deferred(void) { ++polls;now_ms+=100U; }
static int c64_d64_service_prepare_reset(void) { ++disk_calls;event('D');return disk_error; }
static int command(uint32_t op,uint32_t a,uint32_t b) {
    assert(op==MT_COMMAND_CORE_RESET && a==1U && b==0U);++reset_calls;event('R');
    if(reset_error)return reset_error;
    control|=MT_CONTROL_CORE_RESET_REQUEST;return 0;
}
static void usb_hid_service_set_core_ready(bool ready) { assert(!ready);core_ready=ready; }
static void mt_video_effect_service_suspend(void) { event('V'); }
static bool mt_video_effect_service_quiescent(void) { return video_ready; }
static int c64_uci_service_prepare_reload(void) {
    assert(reload_required && hold && (control&MT_CONTROL_CORE_RESET_REQUEST));
    assert(!core_ready);++prepare_calls;event('C');
    if(prepare_pending) { --prepare_pending;return 1; }
    if(close_error)return close_error;
    cleaned=true;softiec_lease=false;return 0;
}
static int quiesce_c64_directory(void) {
    ++directory_calls;event('I');
    /* Real device9 cannot quiesce while a UCI SoftIEC channel owns its lease. */
    assert(cleaned && !softiec_lease);
    return directory_error;
}
static bool mt_screenshot_service_cancel(void) { return screenshot_ok; }
static void mt_c64_debug_deactivate(void *p) { assert(p==&c64_debug); }
static int set_core_reset_request(bool on) {
    assert(on && !reload_required);++normal_reset_calls;
    control|=MT_CONTROL_CORE_RESET_REQUEST;return 0;
}
static void c64_uci_service_reset(void) { assert(!reload_required);++ordinary_uci_resets; }
static void mt_net_service_shutdown(void) { ++shutdowns;network_running=false;event('N'); }
static void c64_title_service_clear(void) { ++title_clears;event('T'); }
static void set_audio(bool mute,bool test) { assert(mute && !test);event('M'); }
static int c64_uci_service_retry(void) {
    ++retries;
    if(retry_pending) { --retry_pending;return 1; }
    if(!retry_error)softiec_lease=false;
    return retry_error;
}
static bool c64_iec_service_available(void) { return true; }
static int c64_iec_service_reset(void) {
    assert(!softiec_lease);++directory_resets;return directory_error;
}
/* PRODUCTION_FUNCTIONS */
static mt_supervisor_result_t selected_reload(mt_supervisor_t *p,const mt_core_selection_t *s) {
    assert(p==&supervisor && s);return MT_SUPERVISOR_OK;
}
static mt_supervisor_result_t selected_switch(mt_supervisor_t *p,const mt_core_selection_t *s) {
    assert(p==&supervisor && s);return MT_SUPERVISOR_NO_CHANGE;
}
#define mt_supervisor_reload selected_reload
#define mt_supervisor_switch selected_switch
static mt_supervisor_result_t menu_decision(void) {
    mt_core_selection_t selection=supervisor.active;mt_supervisor_result_t result;
    /* PRODUCTION_MENU_DECISION */
    return result;
}
#undef mt_supervisor_reload
#undef mt_supervisor_switch
static int load(void *p,const mt_core_selection_t *s) {
    (void)p;assert(s->package.core_id==0x43363420U);
    assert(cleaned && !softiec_lease && directory_calls==1U);
    assert(control&MT_CONTROL_CORE_RESET_REQUEST);
    assert(hold && reload_required && !core_ready);
    assert(!network_running && !c64_uci_initialized && !c64_title_initialized);
    ++loads;event('L');
    /* Only successful physical replacement clears the retained hardware fault. */
    hold=false;reload_required=false;return 0;
}
static int probe(void *p,uint32_t core,uint16_t major,uint16_t minor) {
    (void)p;assert(core==0x43363420U && major==MT_ABI_MAJOR && minor==MT_ABI_MINOR);
    assert(loads==1U && !hold && !reload_required);++probes;event('P');return 0;
}
static int activate(void *p,const mt_core_selection_t *s) {
    (void)p;(void)s;assert(probes==1U);++activations;event('A');
    control&=~MT_CONTROL_CORE_RESET_REQUEST;return 0;
}
static void setup(void) {
    usb_sd_modal=false;c64_uci_initialized=c64_title_initialized=c64_iec_initialized=true;
    c64_services_initialized=is_c64=reload_required=hold=softiec_lease=true;
    screenshot_ok=video_ready=core_ready=network_running=true;cleaned=false;
    c64_debug.reu_host.reload_required=false;
    disk_error=reset_error=close_error=directory_error=retry_error=0;
    prepare_pending=retry_pending=0;
    disk_calls=reset_calls=prepare_calls=directory_calls=0;
    normal_reset_calls=ordinary_uci_resets=shutdowns=title_clears=0;
    loads=probes=activations=retries=polls=control=directory_resets=0;now_ms=0;event_count=0;events[0]=0;
    mt_supervisor_ops_t ops={supervisor_quiesce,load,probe,activate,NULL};
    mt_supervisor_init(&supervisor,&ops,NULL);
    mt_core_selection_t s={0};strcpy(s.directory,"0:/multitini/cores/c64");
    strcpy(s.package.id,"c64");strcpy(s.package.name,"Commodore 64");
    s.package.core_id=0x43363420U;s.package.variant_count=1;
    s.package.abi_major=MT_ABI_MAJOR;s.package.abi_minor=MT_ABI_MINOR;
    strcpy(s.package.variants[0].id,"pal");
    assert(mt_supervisor_set_initial(&supervisor,&s)==MT_SUPERVISOR_OK);
}
static void failed(bool reset_expected) {
    assert(mt_supervisor_reload(&supervisor,&supervisor.active)==MT_SUPERVISOR_QUIESCE_FAILED);
    assert(!loads && !probes && !activations);
    assert(reload_required && hold && !normal_reset_calls && !ordinary_uci_resets);
    assert(!shutdowns && !title_clears && network_running && c64_uci_initialized && c64_title_initialized);
    assert(((control&MT_CONTROL_CORE_RESET_REQUEST)!=0)==reset_expected);
    assert(supervisor.has_active && supervisor.state==MT_SUPERVISOR_ACTIVE && !supervisor.recovered);
}
int main(int argc,char **argv) {
    assert(argc==2);setup();const char *name=argv[1];
    if(!strcmp(name,"reload") || !strcmp(name,"already-reset") || !strcmp(name,"delayed-drain")) {
        if(!strcmp(name,"already-reset"))control=MT_CONTROL_CORE_RESET_REQUEST;
        if(!strcmp(name,"delayed-drain"))prepare_pending=3;
        assert(mt_supervisor_switch(&supervisor,&supervisor.active)==MT_SUPERVISOR_NO_CHANGE);
        assert(!event_count);
        assert(mt_supervisor_reload(&supervisor,&supervisor.active)==MT_SUPERVISOR_OK);
        assert(disk_calls==1 && reset_calls==1 && directory_calls==1 && loads==1 && activations==1);
        assert(prepare_calls==(!strcmp(name,"delayed-drain")?4U:1U));
        assert(!hold && !reload_required && !(control&MT_CONTROL_CORE_RESET_REQUEST));
        assert(!normal_reset_calls && !ordinary_uci_resets);
        assert(strcmp(events,!strcmp(name,"delayed-drain")?"DRVCCCCINTMLPA":"DRVCINTMLPA")==0);
    } else if(!strcmp(name,"file-close")) {
        close_error=-7;failed(true);assert(softiec_lease && !directory_calls && prepare_calls==1);
    } else if(!strcmp(name,"drive8")) {
        disk_error=-8;failed(false);assert(disk_calls==1 && !reset_calls && !prepare_calls);
    } else if(!strcmp(name,"drive9")) {
        directory_error=-9;failed(true);assert(directory_calls==1 && cleaned && !softiec_lease);
    } else if(!strcmp(name,"reset-failure")) {
        reset_error=-10;failed(false);assert(disk_calls==1 && reset_calls==1 && !prepare_calls);
    } else if(!strcmp(name,"hp-timeout")) {
        prepare_pending=100;failed(true);assert(prepare_calls==10 && polls==10 && softiec_lease && !directory_calls);
    } else if(!strcmp(name,"video-timeout")) {
        video_ready=false;failed(true);assert(prepare_calls==10 && polls==10 && !directory_calls);
    } else if(!strcmp(name,"usb-fault")) {
        usb_sd_modal=true;failed(false);assert(!disk_calls && !reset_calls && !prepare_calls);
    } else if(!strcmp(name,"screenshot")) {
        screenshot_ok=false;failed(false);assert(!disk_calls && !reset_calls && !prepare_calls);
    } else if(!strcmp(name,"scope")) {
        is_c64=false;assert(quiesce_c64_for_reload()==-1);is_c64=true;
        reload_required=false;assert(quiesce_c64_for_reload()==-1);reload_required=true;
        usb_sd_modal=true;assert(quiesce_c64_for_reload()==-1);assert(!event_count);
    } else if(!strcmp(name,"ordinary")) {
        reload_required=false;hold=false;assert(supervisor_quiesce(NULL)==0);
        assert(normal_reset_calls==1 && ordinary_uci_resets==1 && !prepare_calls);
        assert(shutdowns==1 && title_clears==1 && !network_running);
    } else if(!strcmp(name,"retry")) {
        retry_pending=3;assert(retry_c64_uci()==0);assert(retries==4 && polls==3);
    } else if(!strcmp(name,"retry-failure")) {
        retry_error=-1;assert(retry_c64_uci()==-1);assert(retries==1 && !polls);
    } else if(!strcmp(name,"retry-timeout")) {
        retry_pending=100;assert(retry_c64_uci()==-1);assert(retries==10 && polls==10);
    } else if(!strcmp(name,"retry-uninitialized")) {
        c64_uci_initialized=false;assert(retry_c64_uci()==0);assert(!retries && !polls);
    } else if(!strcmp(name,"menu-decision")) {
        assert(menu_decision()==MT_SUPERVISOR_OK);
        reload_required=false;assert(menu_decision()==MT_SUPERVISOR_NO_CHANGE);
        c64_debug.reu_host.reload_required=true;assert(menu_decision()==MT_SUPERVISOR_OK);
        is_c64=false;assert(menu_decision()==MT_SUPERVISOR_NO_CHANGE);
        reload_required=true;assert(menu_decision()==MT_SUPERVISOR_NO_CHANGE);
    } else if(!strcmp(name,"user-reset")) {
        reload_required=false;retry_pending=2;
        assert(retry_c64_storage_for_reset()==0);
        assert(retries==3 && directory_resets==1 && !softiec_lease && !reset_calls);
    } else if(!strcmp(name,"user-reset-uci-failure")) {
        retry_error=-1;
        assert(retry_c64_storage_for_reset()==1);
        assert(retries==1 && !directory_resets && softiec_lease && reload_required && hold);
    } else if(!strcmp(name,"user-reset-directory-failure")) {
        reload_required=false;directory_error=25;
        assert(retry_c64_storage_for_reset()==2);
        assert(retries==1 && directory_resets==1 && !softiec_lease && !reset_calls);
    } else if(!strcmp(name,"user-reset-nonc64")) {
        is_c64=false;
        assert(retry_c64_storage_for_reset()==0);
        assert(!retries && !directory_resets && !reset_calls);
    } else assert(0);
    printf("PASS C64 reload %s\n",name);return 0;
}
'''


class C64ReloadRuntime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
        if compiler is None and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if compiler is None:
            raise unittest.SkipTest("A host C compiler is required")
        (ROOT / "build").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="c64-reload-runtime-", dir=ROOT / "build")
        cls.addClassCleanup(cls.temporary.cleanup)
        out = Path(cls.temporary.name)
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        bodies = "\n".join(function(source, signature) for signature in (
            "static int retry_c64_uci(void)",
            "static int retry_c64_storage_for_reset(void)",
            "static int quiesce_c64_for_reload(void)",
            "static int supervisor_quiesce(void *context)",
        ))
        decisions = re.findall(r"result = shell_is_c64\(\) &&.*?;", source, re.S)
        if len(decisions) != 1:
            raise AssertionError("Expected one actual C64 menu switch/reload decision")
        reset_action = source[source.index("} else if (action.kind == MT_MENU_ACTION_RESET_CORE)"):]
        reset_action = reset_action[:reset_action.index("} else if (action.kind == MT_MENU_ACTION_TOGGLE_AUTO_BOOT)")]
        if reset_action.count("retry_c64_storage_for_reset()") != 1 or reset_action.index(
                "retry_c64_storage_for_reset()") > reset_action.index("set_core_reset_request(true)"):
            raise AssertionError("Menu Reset must invoke the tested storage retry before hardware reset")
        if "c64_iec_service_reset()" in reset_action:
            raise AssertionError("Menu Reset must not bypass ordered UCI/device9 retry")
        harness = out / "test.c"
        harness.write_text(HARNESS.replace("/* PRODUCTION_FUNCTIONS */", bodies).replace(
            "/* PRODUCTION_MENU_DECISION */", decisions[0]))
        cls.executable = out / ("test.exe" if os.name == "nt" else "test")
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-I"+str(ROOT / "firmware/include"), str(harness),
                   str(ROOT / "firmware/common/supervisor.c"), "-o", str(cls.executable)]
        result = cls.native(command)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    @staticmethod
    def native(command):
        # Windows environment keys are case insensitive; do not pass duplicate
        # PATH/Path entries through Python's child process environment.
        environment = {k: v for k, v in os.environ.items() if k.upper() != "PATH"}
        environment["PATH"] = os.environ.get("PATH", "")
        return subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"),
                               "--", *command], capture_output=True, text=True,
                              timeout=60, env=environment)

    def run_case(self, name):
        result = self.native([str(self.executable), name])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS C64 reload " + name, result.stdout)

    def test_same_image_full_reload(self): self.run_case("reload")
    def test_already_reset_still_cleans_all_owners(self): self.run_case("already-reset")
    def test_pending_drain_then_lease_release(self): self.run_case("delayed-drain")
    def test_file_close_failure_retains_fault_hold_and_lease(self): self.run_case("file-close")
    def test_drive8_flush_failure_precedes_reset(self): self.run_case("drive8")
    def test_drive9_failure_prevents_reconfiguration(self): self.run_case("drive9")
    def test_reset_failure_prevents_cleanup_and_reconfiguration(self): self.run_case("reset-failure")
    def test_hp_drain_timeout_retains_hold_and_reset(self): self.run_case("hp-timeout")
    def test_video_drain_timeout_prevents_reconfiguration(self): self.run_case("video-timeout")
    def test_faulted_usb_export_stays_refused(self): self.run_case("usb-fault")
    def test_screenshot_failure_prevents_reset(self): self.run_case("screenshot")
    def test_special_cleanup_only_accepts_faulted_c64_reload(self): self.run_case("scope")
    def test_ordinary_quiesce_still_uses_normal_reset(self): self.run_case("ordinary")
    def test_retry_waits_for_cleanup(self): self.run_case("retry")
    def test_retry_failure_is_not_success(self): self.run_case("retry-failure")
    def test_retry_timeout_is_bounded(self): self.run_case("retry-timeout")
    def test_retry_uninitialized_is_noop(self): self.run_case("retry-uninitialized")
    def test_menu_forces_reload_for_uci_and_debug_faults_only(self): self.run_case("menu-decision")
    def test_user_reset_releases_softiec_before_device9(self): self.run_case("user-reset")
    def test_user_reset_rejects_uncertain_uci_completion(self): self.run_case("user-reset-uci-failure")
    def test_user_reset_preserves_device9_failure(self): self.run_case("user-reset-directory-failure")
    def test_user_reset_other_cores_never_touch_c64_services(self): self.run_case("user-reset-nonc64")


if __name__ == "__main__":
    unittest.main()
