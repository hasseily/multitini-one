# SPDX-License-Identifier: GPL-3.0-only
"""Execute production USB teardown/cancel ordering with explicit service owners."""
from pathlib import Path
import os
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
#include "multitini_usb_storage_mode.h"
#include "multitini_regs.h"
#include "sd_storage.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
static mt_supervisor_t supervisor;
static bool usb_sd_modal, usb_sd_core_stopped, usb_sd_previously_reset;
static bool c64_uci_initialized, c64_title_initialized, c64_debug_pause_active;
static bool network_running, external_owned, acquire_failed, drain_failed, reset_failed;
static uint32_t control, temporary_ip;
static unsigned title_overlay, shutdowns, uci_resets, resumes, connections;
static int c64_debug;
static uint32_t shell_read(uint32_t reg) {assert(reg==MT_REG_CONTROL);return control;}
static bool mt_screenshot_service_cancel(void) {return true;}
static void mt_c64_debug_deactivate(void *p) {assert(p==&c64_debug);}
static int set_core_reset_request(bool on) {
    if(reset_failed)return -1;
    if(on)control|=MT_CONTROL_CORE_RESET_REQUEST;
    else {
        assert(c64_uci_initialized && c64_title_initialized && network_running);
        assert(title_overlay==42 && temporary_ip==0x0a010203U);
        control&=~MT_CONTROL_CORE_RESET_REQUEST;++resumes;
    }
    return 0;
}
static void c64_uci_service_reset(void) {++uci_resets;}
static bool shell_is_c64(void) {return true;}
static bool c64_uci_service_reload_required(void) {return false;}
static int quiesce_c64_for_reload(void) {assert(0);return -1;}
static void mt_net_service_shutdown(void) {network_running=false;++shutdowns;}
static void c64_title_service_clear(void) {title_overlay=0;}
static void set_audio(bool mute,bool test) {(void)mute;(void)test;}
static void usb_hid_service_set_core_ready(bool ready) {(void)ready;}
static bool mt_video_worker_resume(void) {return true;}
static void configure_selection_effects(const mt_core_selection_t *p) {assert(p==&supervisor.active);}
static mt_sd_result_t mt_sd_storage_external_acquire_fixture(void) {
    if(acquire_failed)return (mt_sd_result_t){MT_SD_FILE_BUSY,0,0};
    external_owned=true;return (mt_sd_result_t){MT_SD_OK,0,0};
}
#define mt_sd_storage_external_acquire mt_sd_storage_external_acquire_fixture
'''

SCENARIOS = r'''
static void setup(void) {
    usb_sd_modal=true;usb_sd_core_stopped=usb_sd_previously_reset=false;
    c64_uci_initialized=c64_title_initialized=network_running=true;
    acquire_failed=drain_failed=reset_failed=external_owned=false;
    control=shutdowns=uci_resets=resumes=connections=0;
    title_overlay=42;temporary_ip=0x0a010203U;supervisor.has_active=true;
}
static int quiesce(void *p) {
    usb_sd_previously_reset=(control&MT_CONTROL_CORE_RESET_REQUEST)!=0;
    if(supervisor_quiesce(p))return -1;
    usb_sd_core_stopped=true;
    return drain_failed?-1:0;
}
static int connect_usb(void *p) {
    (void)p;assert(external_owned && shutdowns==1 && !network_running);
    assert(!c64_uci_initialized && !c64_title_initialized && !title_overlay);
    ++connections;return 0;
}
static void enter(void) {
    mt_usb_sd_ops_t ops={0};ops.quiesce=quiesce;ops.acquire=usb_sd_acquire;
    ops.connect=connect_usb;ops.cancel_quiesce=usb_sd_cancel_quiesce;
    mt_usb_sd_mode_t mode;mt_usb_sd_mode_init(&mode,&ops);
    int result=mt_usb_sd_mode_enter(&mode);
    assert(result==((acquire_failed||drain_failed||reset_failed)?-1:0));
}
int main(void) {
    setup();usb_sd_modal=false;assert(supervisor_quiesce(NULL)==0);
    assert(shutdowns==1 && !network_running && !c64_uci_initialized && !c64_title_initialized);
    setup();acquire_failed=true;enter();
    assert(resumes==1 && shutdowns==0 && uci_resets==1 && network_running && !external_owned);
    setup();drain_failed=true;enter();
    assert(resumes==1 && shutdowns==0 && c64_uci_initialized && c64_title_initialized);
    setup();control=MT_CONTROL_CORE_RESET_REQUEST;acquire_failed=true;enter();
    assert(!resumes && control==MT_CONTROL_CORE_RESET_REQUEST && network_running);
    setup();reset_failed=true;enter();
    assert(!resumes && !shutdowns && !usb_sd_core_stopped && title_overlay==42);
    setup();enter();
    assert(connections==1 && external_owned && !resumes && shutdowns==1 && uci_resets==1);
    puts("PASS USB cancellation: six production lifecycle scenarios preserve session and effective IP");
    return 0;
}
'''


class UsbCancellation(unittest.TestCase):
    def test_production_service_ownership(self):
        cc = shutil.which("gcc") or ("C:/msys64/ucrt64/bin/gcc.exe" if os.name == "nt" else "cc")
        with tempfile.TemporaryDirectory(prefix="c64-usb-cancel-", dir=ROOT / "build") as directory:
            out = Path(directory)
            source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
            bodies = "\n".join(function(source, sig) for sig in (
                "static int supervisor_quiesce(void *context)",
                "static void usb_sd_cancel_quiesce(void *context)",
                "static int usb_sd_acquire(void *context)",
            ))
            harness = out / "test.c"
            harness.write_text(HARNESS + bodies + SCENARIOS)
            exe = out / ("test.exe" if os.name == "nt" else "test")
            args = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I"+str(ROOT/"firmware/include"),
                    "-I"+str(ROOT/"firmware/platform/xilinx"), str(harness),
                    str(ROOT/"firmware/common/usb_storage_mode.c"), "-o", str(exe)]
            for command in (args, [str(exe)]):
                run = subprocess.run([sys.executable, str(ROOT/"scripts/run_native_tool.py"), "--", *command],
                                     capture_output=True, text=True, timeout=60)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS USB cancellation", run.stdout)


if __name__ == "__main__":
    unittest.main()
