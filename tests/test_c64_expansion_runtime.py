# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production C64 expansion settings transactions with mocked MMIO/SD."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def production_function(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


PREAMBLE = r'''
#include "multitini_supervisor.h"
#include "multitini_c64_interface.h"
#include "multitini_regs.h"
#include "supervisor_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MT_SHELL_BASE_ADDRESS UINT32_C(0x40000000)
static mt_supervisor_t supervisor;
static mt_supervisor_settings_t persistent_settings, settings_before_update, saved_settings;
static uint32_t caps, config;
static unsigned cap_reads, config_reads, writes, saves;
static bool attached, save_fail, supervisor_menu_dirty;
static char message[128];
static uint32_t c64_content_service_expansion_caps(void) { ++cap_reads; return caps; }
static bool c64_content_service_crt_attached(void) { return attached; }
static uint32_t shell_read(uint32_t reg) {
    assert(reg == MT_REG_C64_EXPANSION_CONFIG); ++config_reads; return config;
}
static void Xil_Out32(uint32_t address, uint32_t value) {
    assert(address == MT_SHELL_BASE_ADDRESS + MT_REG_C64_EXPANSION_CONFIG);
    ++writes; config = value;
}
static void set_supervisor_warning(const char *text) { snprintf(message,sizeof(message),"%s",text); }
static void set_supervisor_status(const char *text) { set_supervisor_warning(text); }
static bool save_persistent_settings(void) {
    ++saves;
    if (save_fail) return false;
    saved_settings = persistent_settings;
    return true;
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *p,const char *j,uint8_t *b,size_t n) {
    (void)p;(void)j;(void)b;(void)n; mt_sd_result_t r={MT_SD_OK,0U,0U};return r;
}
mt_sd_result_t mt_sd_storage_read_file_optional_exact(const char *p,uint8_t *b,size_t n,bool *f) {
    (void)p;(void)b;(void)n;*f=false;mt_sd_result_t r={MT_SD_OK,0U,0U};return r;
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *p,const char *j,const uint8_t *b,size_t n) {
    (void)p;(void)j;(void)b;(void)n;mt_sd_result_t r={MT_SD_OK,0U,0U};return r;
}
'''

SCENARIOS = r'''
static void reset_fixture(void) {
    memset(&supervisor,0,sizeof(supervisor));
    supervisor.has_active=true;supervisor.active.package.core_id=MT_C64_CORE_ID;
    supervisor.active.package.abi_minor=23U;
    strcpy(supervisor.active.package.id,"c64");
    strcpy(supervisor.active.package.variants[0].id,"pal");
    caps=MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_DUAL_SID|MT_C64_EXPANSION_CAPS_REU_16MB;
    config=MT_C64_EXPANSION_CONFIG_DEFAULT;
    cap_reads=config_reads=writes=saves=0U;attached=save_fail=supervisor_menu_dirty=false;
    message[0]='\0';mt_supervisor_settings_defaults(&persistent_settings);
}
int main(int argc,char **argv) {
    assert(argc==2);reset_fixture();
    if(strcmp(argv[1],"defaults")==0) {
        configure_c64_expansion(&supervisor.active,NULL);
        assert(config==11U && writes==1U && saves==0U);
        assert(ensure_active_settings_profile());
        assert(mt_supervisor_settings_remember_c64_expansion(&persistent_settings,"c64",2U,false));
        configure_c64_expansion(&supervisor.active,&persistent_settings.profiles[0]);
        assert(config==2U && writes==2U);
        caps=MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_REU_16MB;
        configure_c64_expansion(&supervisor.active,NULL);assert(config==8U);
        caps=MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_DUAL_SID;
        configure_c64_expansion(&supervisor.active,NULL);assert(config==3U);
    } else if(strcmp(argv[1],"gates")==0) {
        for(unsigned abi=0U;abi<23U;++abi) {
            supervisor.active.package.abi_minor=(uint16_t)abi;
            configure_c64_expansion(&supervisor.active,NULL);change_c64_expansion(2U);change_c64_expansion(3U);
        }
        assert(cap_reads==0U && config_reads==0U && writes==0U && saves==0U);
        supervisor.active.package.abi_minor=23U;supervisor.active.package.core_id=0U;
        change_c64_expansion(2U);assert(cap_reads==0U);
        supervisor.active.package.core_id=MT_C64_CORE_ID;supervisor.active.package.abi_major=1U;
        change_c64_expansion(3U);assert(cap_reads==0U);
        supervisor.active.package.abi_major=0U;caps=0U;
        configure_c64_expansion(&supervisor.active,NULL);change_c64_expansion(2U);change_c64_expansion(3U);
        assert(config_reads==0U && writes==0U && saves==0U);
        supervisor.has_active=false;change_c64_expansion(2U);assert(writes==0U);
    } else if(strcmp(argv[1],"cycle")==0) {
        const uint32_t expected[]={8U,10U,11U};
        for(unsigned i=0U;i<3U;++i) {
            change_c64_expansion(2U);assert(config==expected[i] && writes==i+1U && saves==i+1U);
            assert(saved_settings.profiles[0].c64_sid_mode==(expected[i]&7U));
            assert(saved_settings.profiles[0].c64_reu_enabled && supervisor_menu_dirty);
        }
        change_c64_expansion(3U);assert(config==3U && !saved_settings.profiles[0].c64_reu_enabled);
        change_c64_expansion(3U);assert(config==11U && saved_settings.profiles[0].c64_reu_enabled);
        assert(strcmp(message,"16 MB REU enabled")==0);
        change_c64_expansion(99U);assert(writes==5U && saves==5U);
    } else if(strcmp(argv[1],"conflict")==0) {
        config=3U;attached=true;change_c64_expansion(3U);
        assert(config==3U && writes==0U && saves==0U && persistent_settings.profile_count==0U);
        assert(strstr(message,"Eject the cartridge")!=NULL);
        change_c64_expansion(2U);assert(config==0U && writes==1U);
        attached=false;change_c64_expansion(3U);assert(config==8U && writes==2U);
    } else if(strcmp(argv[1],"save-failure")==0) {
        save_fail=true;change_c64_expansion(2U);
        assert(config==11U && writes==0U && saves==1U && persistent_settings.profile_count==0U);
        assert(strstr(message,"not saved")!=NULL);
        assert(ensure_active_settings_profile());
        assert(mt_supervisor_settings_remember_c64_expansion(&persistent_settings,"c64",2U,false));
        const mt_supervisor_settings_t before=persistent_settings;
        change_c64_expansion(3U);
        assert(config==11U && writes==0U && memcmp(&persistent_settings,&before,sizeof(before))==0);
    } else if(strcmp(argv[1],"partial")==0) {
        assert(ensure_active_settings_profile());
        assert(mt_supervisor_settings_remember_c64_expansion(&persistent_settings,"c64",2U,false));
        caps=MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_REU_16MB;config=0U;
        change_c64_expansion(2U);assert(writes==0U && saves==0U);
        change_c64_expansion(3U);assert(config==8U && saved_settings.profiles[0].c64_sid_mode==2U);
        caps=MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_DUAL_SID;config=3U;
        change_c64_expansion(3U);assert(writes==1U && saves==1U);
        change_c64_expansion(2U);assert(config==0U && saved_settings.profiles[0].c64_reu_enabled);
    } else assert(0);
    printf("PASS: C64 expansion %s\n",argv[1]);return 0;
}
'''


class C64ExpansionRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
        if compiler is None:
            raise unittest.SkipTest("A host C compiler is required")
        (ROOT / "build").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="c64-expansion-runtime-", dir=ROOT / "build")
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        signatures = (
            "static bool ensure_active_settings_profile(void)",
            "static uint32_t selection_c64_expansion_caps(",
            "static void configure_c64_expansion(",
            "static void change_c64_expansion(",
        )
        harness = folder / "runtime.c"
        harness.write_text(PREAMBLE + "\n".join(production_function(source, s) for s in signatures) + SCENARIOS)
        cls.executable = folder / ("runtime.exe" if os.name == "nt" else "runtime")
        command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--", compiler,
                   "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                   "-I", str(ROOT / "firmware/include"), "-I", str(ROOT / "firmware/platform/xilinx"),
                   str(harness), str(ROOT / "firmware/platform/xilinx/supervisor_settings.c"),
                   *[str(ROOT / "firmware/common" / name) for name in
                     ("gamepad.c", "controls.c", "video_effects.c")], "-lm", "-o", str(cls.executable)]
        result = subprocess.run(command, text=True, capture_output=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_scenario(self, name):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
                                 str(self.executable), name], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS: C64 expansion {name}", result.stdout)

    def test_default_and_saved_configuration(self): self.run_scenario("defaults")
    def test_older_abi_and_missing_capabilities(self): self.run_scenario("gates")
    def test_mode_cycle_and_reu_toggle(self): self.run_scenario("cycle")
    def test_cartridge_conflict_precedes_writes(self): self.run_scenario("conflict")
    def test_save_failure_preserves_hardware_and_settings(self): self.run_scenario("save-failure")
    def test_partial_capability_preserves_other_saved_preferences(self): self.run_scenario("partial")


if __name__ == "__main__":
    unittest.main()
