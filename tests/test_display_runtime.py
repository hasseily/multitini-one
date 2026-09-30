# SPDX-License-Identifier: GPL-3.0-only
"""Run the board display transaction with real register API and mocked services."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DisplayRuntimeTests(unittest.TestCase):
    def test_clock_settings_and_effects_transaction(self):
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        functions = []
        for signature in ("static bool apply_display_mode(uint8_t mode)",
                          "static void change_display_mode(size_t mode)"):
            start = source.index(signature + "\n{")
            functions.append(source[start:source.index("\n}", start) + 2])
        harness = r'''
#include "multitini_display.h"
#include "multitini_regs.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
static struct {uint8_t display_mode;} persistent_settings;
static struct {bool has_active; int active;} supervisor;
static int supervisor_presenter;
static bool supervisor_menu_dirty, save_fails, clock_fails, drain_stalls;
static uint32_t version=16,status,now;
static unsigned writes,saves,suspends,configures,invalidates,polls,warnings;
static bool outcome;
static uint32_t shell_read(uint32_t reg) {
    if(reg==MT_REG_ABI_VERSION)return version;
    assert(version>=16); /* No touching new registers on an old core. */
    if(reg==MT_REG_DISPLAY_MODES)return 63;
    assert(reg==MT_REG_DISPLAY_STATUS);return status;
}
static void video_effect_write(uint32_t reg,uint32_t value) {
    assert(reg==MT_REG_DISPLAY_REQUEST);assert(suspends);
    ++writes;status=clock_fails ? MT_DISPLAY_ERROR : value;
}
static uint32_t video_effect_milliseconds(void){return ++now;}
static uint64_t monotonic_ms(void *context){(void)context;return ++now;}
static const mt_display_ops_t display_bus={shell_read,video_effect_write,video_effect_milliseconds};
static void mt_video_effect_service_suspend(void){++suspends;}
static bool mt_video_effect_service_quiescent(void){return !drain_stalls;}
static void usb_hid_service_poll_deferred(void){++polls;}
static void mt_ui_presenter_invalidate(int *presenter){assert(presenter==&supervisor_presenter);++invalidates;}
static void xil_printf(const char *format,...){(void)format;}
static bool save_persistent_settings(void){
    assert(status==persistent_settings.display_mode);++saves;return !save_fails;
}
static void configure_selection_effects(const int *active){
    assert(active==(supervisor.has_active ? &supervisor.active : NULL));++configures;
}
static void set_supervisor_warning(const char *text){(void)text;++warnings;}
static void set_supervisor_outcome(bool ok,const char *success,const char *failure){
    (void)success;(void)failure;outcome=ok;
}
'''
        cases = r'''
int main(void) {
    supervisor.has_active=true;
    change_display_mode(2);
    assert(outcome && status==2 && persistent_settings.display_mode==2);
    assert(saves==1 && writes==1 && configures==1 && invalidates==1 && supervisor_menu_dirty);
    /* Same mode needs no reset, but saving remains an explicit operation. */
    change_display_mode(2);
    assert(outcome && writes==1 && saves==2 && suspends==1 && configures==2);
    save_fails=true;
    change_display_mode(5);
    assert(!outcome && status==2 && persistent_settings.display_mode==2);
    assert(saves==3 && writes==3 && invalidates==3 && configures==3);
    save_fails=false;clock_fails=true;
    change_display_mode(3);
    assert(!outcome && saves==3 && writes==4 && persistent_settings.display_mode==2 && configures==4);
    clock_fails=false;status=2;drain_stalls=true;
    change_display_mode(1);
    assert(!outcome && writes==4 && saves==3 && polls>=999 && configures==5);
    drain_stalls=false;version=15;
    change_display_mode(1);
    assert(warnings==1 && writes==4 && saves==3 && configures==5);
    assert(apply_display_mode(0) && !apply_display_mode(1));
    version=16;
    change_display_mode(6);
    assert(warnings==2 && writes==4 && saves==3);
    supervisor.has_active=false;
    change_display_mode(0);
    assert(outcome && status==0 && persistent_settings.display_mode==0 && configures==6);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "display.c").write_text(harness + "\n".join(functions) + cases)
            executable = path / ("display.exe" if os.name == "nt" else "display")
            guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
            for command in ([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                             "-Ifirmware/include", str(path / "display.c"),
                             "firmware/common/display.c", "-o", str(executable)], [str(executable)]):
                subprocess.run(guard + command, cwd=ROOT, check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
