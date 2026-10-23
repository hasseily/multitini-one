# SPDX-License-Identifier: GPL-3.0-only
"""Execute live-image DDR drain gates and PCAP/USB transitions from main.c."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def production_function(source, signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}", start) + 2]


PREAMBLE = r'''
#include "multitini_supervisor.h"
#include "multitini_c64_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_iigs_interface.h"
#include "multitini_nes_interface.h"
#include "multitini_regs.h"
#include "pl_loader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static mt_supervisor_t supervisor;
static uint32_t core, abi, caps, control;
static unsigned abi_reads, caps_reads, source_reads, polls, release_poll;
static unsigned pcap_calls, invalidations, restores, resets, reader_closes;
static bool quiet, effects_quiet, worker_quiet, screenshot_ok, reset_ok;
static bool supervisor_menu_dirty, usb_sd_core_stopped, usb_sd_previously_reset;
static int supervisor_presenter, document_reader;
static uint64_t milliseconds;
static uint32_t shell_read(uint32_t offset) {
    if(offset==MT_REG_CORE_ID) return core;
    if(offset==MT_REG_ABI_VERSION) {++abi_reads;return abi;}
    if(offset==MT_REG_C64_EXPANSION_CAPS) {++caps_reads;return caps;}
    if(offset==MT_REG_CONTROL) return control;
    assert(offset==MT_REG_VIDEO_FRAME_STATUS); ++source_reads;
    return quiet ? MT_VIDEO_FRAME_STATUS_QUIESCENT : 0U;
}
static void mt_video_effect_service_suspend(void) {}
static bool mt_video_effect_service_quiescent(void) {return effects_quiet;}
static bool mt_video_worker_quiesce(void) {return worker_quiet;}
static bool mt_screenshot_service_cancel(void) {return screenshot_ok;}
static void mt_text_reader_close(void *reader) {assert(reader==&document_reader);++reader_closes;}
static int supervisor_quiesce(void *context) {
    assert(context==NULL);++resets;
    if(!reset_ok) return -1;
    control|=MT_CONTROL_CORE_RESET_REQUEST;return 0;
}
static uint64_t monotonic_ms(void *context) {assert(context==NULL);milliseconds+=100U;return milliseconds;}
static void usb_hid_service_poll_deferred(void) {
    ++polls;
    if(release_poll!=0U && polls>=release_poll)quiet=true;
}
static void configure_selection_effects(const mt_core_selection_t *selection) {
    assert(selection==&supervisor.active);++restores;
}
static int xil_printf(const char *format,...) {(void)format;return 0;}
mt_pl_load_result_t mt_pl_load_selection(const char *directory,const mt_core_variant_t *variant) {
    assert(directory==supervisor.active.directory && variant==&supervisor.active.package.variants[0]);
    assert(effects_quiet);
    if(core==MT_C64_CORE_ID && abi==23U &&
       (caps&(MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_REU_DDR))==
             (MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_REU_DDR)) assert(quiet);
    ++pcap_calls;return (mt_pl_load_result_t){0};
}
const char *mt_pl_load_status_name(mt_pl_load_status_t status) {(void)status;return "ok";}
static void mt_ui_presenter_invalidate(void *presenter) {assert(presenter==&supervisor_presenter);++invalidations;}
'''

SCENARIOS = r'''
static void initial_state(void) {
    memset(&supervisor,0,sizeof(supervisor));supervisor.has_active=true;
    core=MT_C64_CORE_ID;abi=23U;control=MT_CONTROL_CORE_RESET_REQUEST;
    caps=MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_REU_16MB|MT_C64_EXPANSION_CAPS_REU_DDR;
    abi_reads=caps_reads=source_reads=polls=release_poll=0U;
    pcap_calls=invalidations=restores=resets=reader_closes=0U;milliseconds=0U;
    quiet=supervisor_menu_dirty=usb_sd_core_stopped=usb_sd_previously_reset=false;
    effects_quiet=worker_quiet=screenshot_ok=reset_ok=true;
}
int main(int argc,char **argv) {
    assert(argc==2);initial_state();
    if(strcmp(argv[1],"compatibility")==0) {
        for(unsigned minor=0U;minor<23U;++minor) {
            initial_state();abi=minor;
            assert(!core_requires_ddr_drain() && caps_reads==0U);
            assert(supervisor_load(NULL,&supervisor.active)==0);
            assert(caps_reads==0U && source_reads==0U && polls==0U && pcap_calls==1U);
        }
        initial_state();abi=0x10017U;
        assert(!core_requires_ddr_drain() && caps_reads==0U);
        const uint32_t legacy_caps[]={0U,MT_C64_EXPANSION_CAPS_REU_DDR,
            MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_REU_16MB|MT_C64_EXPANSION_CAPS_DUAL_SID};
        for(unsigned i=0U;i<sizeof(legacy_caps)/sizeof(legacy_caps[0]);++i) {
            initial_state();caps=legacy_caps[i];
            assert(supervisor_load(NULL,&supervisor.active)==0);
            assert(caps_reads==1U && source_reads==0U && polls==0U && pcap_calls==1U);
            assert(usb_sd_quiesce(NULL)==0 && source_reads==0U);
        }
        initial_state();core=0U;
        assert(!core_requires_ddr_drain() && abi_reads==0U && caps_reads==0U);
        const uint32_t existing[]={MT_GENESIS_CORE_ID,MT_NES_CORE_ID,MT_IIGS_CORE_ID};
        for(unsigned i=0U;i<sizeof(existing)/sizeof(existing[0]);++i) {
            core=existing[i];assert(core_requires_ddr_drain());
        }
        assert(abi_reads==0U && caps_reads==0U);
    } else if(strcmp(argv[1],"pcap")==0) {
        /* Live capability alone owns the contract, even without a REU-enable
         * or REU-capacity bit and before any content service was initialized. */
        caps=MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_REU_DDR;
        release_poll=4U;
        assert(supervisor_load(NULL,&supervisor.active)==0);
        assert(polls==4U && source_reads==5U && pcap_calls==1U && invalidations==1U);
        initial_state();
        assert(supervisor_load(NULL,&supervisor.active)==-1);
        assert(polls==10U && source_reads==10U && pcap_calls==0U && invalidations==0U && restores==1U);
    } else if(strcmp(argv[1],"usb")==0) {
        control=0U;release_poll=3U;
        assert(usb_sd_quiesce(NULL)==0);
        assert(polls==3U && source_reads==4U && resets==1U && reader_closes==1U);
        assert(usb_sd_core_stopped && !usb_sd_previously_reset);
        initial_state();
        assert(usb_sd_quiesce(NULL)==-1 && usb_sd_core_stopped && usb_sd_previously_reset);
        assert(polls==10U && source_reads==10U);
        initial_state();reset_ok=false;
        assert(usb_sd_quiesce(NULL)==-1 && !usb_sd_core_stopped && caps_reads==0U && source_reads==0U);
        initial_state();screenshot_ok=false;
        assert(usb_sd_quiesce(NULL)==-1 && resets==0U && reader_closes==0U && caps_reads==0U);
    } else assert(0);
    printf("PASS: C64 DDR drain %s\n",argv[1]);return 0;
}
'''


class C64DdrDrainRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            raise AssertionError("A host C compiler is required")
        (ROOT / "build").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="c64-ddr-drain-", dir=ROOT / "build")
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        signatures = (
            "static bool core_requires_ddr_drain(void)",
            "static int supervisor_load(void *context,\n                           const mt_core_selection_t *selection)",
            "static int usb_sd_quiesce(void *context)",
        )
        harness = folder / "drain.c"
        harness.write_text(PREAMBLE + "\n".join(production_function(source, s) for s in signatures) + SCENARIOS)
        cls.executable = folder / ("drain.exe" if os.name == "nt" else "drain")
        command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--", compiler,
                   "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                   "-I", str(ROOT / "firmware/include"), "-I", str(ROOT / "firmware/platform/xilinx"),
                   str(harness), "-o", str(cls.executable)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_scenario(self, name):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
                                 str(self.executable), name], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS: C64 DDR drain {name}", result.stdout)

    def test_live_capability_and_legacy_images(self): self.run_scenario("compatibility")
    def test_pcap_waits_and_cancels_on_timeout(self): self.run_scenario("pcap")
    def test_usb_storage_waits_and_cancels_on_timeout(self): self.run_scenario("usb")


if __name__ == "__main__":
    unittest.main()
