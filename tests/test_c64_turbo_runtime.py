# SPDX-License-Identifier: GPL-3.0-only
"""Execute actual menu/controller turbo transactions with mocked MMIO and SD."""
from pathlib import Path
import os, shutil, subprocess, sys, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]

def function(text, signature):
    start = text.rindex(signature)
    return text[start:text.index('\n}', start)+2]

PREAMBLE = r'''
#include "multitini_supervisor.h"
#include "multitini_c64_interface.h"
#include "multitini_c64_debug.h"
#include "supervisor_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MT_SHELL_BASE_ADDRESS UINT32_C(0x40000000)
static mt_supervisor_t supervisor;
static mt_supervisor_settings_t persistent_settings,settings_before_update;
static mt_c64_debug_t c64_debug;
static uint32_t config=0,status=0,cap=MT_C64_CPU_CAPS_RELEASE;
static unsigned writes,saves,legacy_calls,new_reads;
static bool save_fail,supervisor_menu_dirty;
static char message[128];
static uint32_t shell_read(uint32_t reg) {
    switch(reg) {
    case MT_REG_ABI_VERSION:return 24U;
    case MT_REG_C64_EXPANSION_CAPS:return MT_C64_EXPANSION_CAPS_VALID;
    case MT_REG_C64_CPU_CAPS:++new_reads;return cap;
    case MT_REG_C64_CPU_CAPS2:++new_reads;return MT_C64_CPU_CAPS2_RELEASE;
    case MT_REG_DEBUG_STATUS:++new_reads;return 0;
    case MT_REG_C64_CPU_CONFIG:++new_reads;return config;
    case MT_REG_C64_CPU_STATUS:++new_reads;return status;
    default:assert(0);return 0;
    }
}
static uint32_t read32(void *p,uint32_t reg) {(void)p;return shell_read(reg);}
static mt_core_link_t shell_link={.read32=read32};
static void Xil_Out32(uint32_t address,uint32_t value) {
    assert(address==MT_SHELL_BASE_ADDRESS+MT_REG_C64_CPU_CONFIG);config=value;++writes;
}
static void set_c64_cpu_turbo(bool enabled) {(void)enabled;++legacy_calls;}
static void set_supervisor_status(const char *s) {snprintf(message,sizeof(message),"%s",s);}
static void set_supervisor_warning(const char *s) {set_supervisor_status(s);}
static bool save_persistent_settings(void) {++saves;return !save_fail;}
mt_sd_result_t mt_sd_storage_journal_recover(const char*p,const char*j,uint8_t*b,size_t n) {
    (void)p;(void)j;(void)b;(void)n;return (mt_sd_result_t){MT_SD_OK,0,0};
}
mt_sd_result_t mt_sd_storage_read_file_optional_exact(const char*p,uint8_t*b,size_t n,bool*f) {
    (void)p;(void)b;(void)n;*f=false;return (mt_sd_result_t){MT_SD_OK,0,0};
}
mt_sd_result_t mt_sd_storage_journal_replace(const char*p,const char*j,const uint8_t*b,size_t n) {
    (void)p;(void)j;(void)b;(void)n;return (mt_sd_result_t){MT_SD_OK,0,0};
}
'''
SCENARIOS = r'''
static void reset(void) {
    memset(&supervisor,0,sizeof(supervisor));supervisor.has_active=true;
    supervisor.active.package.core_id=MT_C64_CORE_ID;supervisor.active.package.abi_minor=24;
    strcpy(supervisor.active.package.id,"c64");strcpy(supervisor.active.package.variants[0].id,"pal");
    mt_supervisor_settings_defaults(&persistent_settings);memset(&c64_debug,0,sizeof(c64_debug));
    assert(ensure_active_settings_profile());
    persistent_settings.profiles[0].c64_cpu_turbo=false;
    persistent_settings.c64_turbo_detail=0x41;config=0;status=0;saves=writes=legacy_calls=new_reads=0;
    save_fail=supervisor_menu_dirty=false;cap=MT_C64_CPU_CAPS_RELEASE;
    configure_c64_turbo(&supervisor.active,&persistent_settings.profiles[0],false);
    writes=legacy_calls=new_reads=0;
}
static void override(void) {
    c64_debug.session=c64_debug.override_active=true;c64_debug.override_index=15;
    c64_debug.caps.cpu=cap;
    configure_c64_turbo(&supervisor.active,&persistent_settings.profiles[0],true);
    writes=legacy_calls=0;
}
int main(void) {
    reset();supervisor.active.package.abi_minor=23;change_c64_turbo(9);
    assert(new_reads==0 && writes==0 && saves==0);
    reset();change_c64_turbo(9);assert((config&0x7f)==0x51 && saves==1 && writes==1);
    assert((config&MT_C64_CPU_CONFIG_APPLY_MASK)==0x1000);
    for(unsigned i=0;i<16;++i)change_c64_turbo(12);
    assert((config&15)==15 && persistent_settings.profiles[0].c64_cpu_turbo);
    uint32_t counter=config&MT_C64_CPU_CONFIG_APPLY_MASK;change_c64_turbo(12);
    assert((config&MT_C64_CPU_CONFIG_APPLY_MASK)==counter);
    change_c64_turbo(13);assert((config&15)==14);
    change_c64_turbo(9);assert((config&MT_C64_CPU_CONFIG_MODE_MASK)==0);
    unsigned count=saves;change_c64_turbo(13);assert(saves==count && strcmp(message,"Turbo is off")==0);
    reset();override();change_c64_turbo(13);
    assert(!c64_debug.override_active && (config&MT_C64_CPU_CONFIG_MODE_MASK)==0 && saves==0 && writes==1);
    reset();override();save_fail=true;change_c64_turbo(9);
    assert(!c64_debug.override_active && !persistent_settings.profiles[0].c64_cpu_turbo);
    assert((config&MT_C64_CPU_CONFIG_MODE_MASK)==0 && writes==1 && saves==1);
    reset();save_fail=true;uint32_t before=config;change_c64_turbo(9);
    assert(config==before && writes==0 && !persistent_settings.profiles[0].c64_cpu_turbo);
    reset();override();change_c64_turbo(7);change_c64_turbo(10);change_c64_turbo(99);
    assert(c64_debug.override_active && writes==0 && saves==0);
    reset();status=MT_C64_CPU_STATUS_DISK|(2U<<6);change_c64_turbo(9);
    assert(strstr(message,"2 MHz saved") && strstr(message,"now 1 MHz") && strstr(message,"cable held"));
    reset();cap=(cap&~MT_C64_CPU_CAPS_INDEX_MASK)|5U;change_c64_turbo(4);
    assert((persistent_settings.c64_turbo_detail&15)==2 && (config&15)==2);
    puts("PASS C64 actual turbo runtime: gates, actions, limits, apply counter, override termination and save failures");
    return 0;
}
'''

class TurboRuntime(unittest.TestCase):
    def test_transactions(self):
        compiler=os.environ.get('CC') or shutil.which('gcc') or shutil.which('cc')
        self.assertIsNotNone(compiler,'A C compiler is required')
        with tempfile.TemporaryDirectory(prefix='c64-turbo-runtime-',dir=ROOT/'build') as d:
            d=Path(d);source=(ROOT/'firmware/platform/xilinx/main.c').read_text()
            names=('static bool ensure_active_settings_profile(void)',
                   'static mt_c64_capabilities_t selection_c64_features(',
                   'static void configure_c64_turbo(', 'static void change_c64_turbo(')
            c=d/'runtime.c';c.write_text(PREAMBLE+'\n'.join(function(source,n) for n in names)+SCENARIOS)
            exe=d/('runtime.exe' if os.name=='nt' else 'runtime')
            files=['firmware/platform/xilinx/supervisor_settings.c']+['firmware/common/'+n+'.c' for n in
                ('gamepad','controls','video_effects','c64_features','c64_debug','c64_reu_host')]
            wrapper=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
            command=wrapper+[compiler,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(ROOT/'firmware/include'),
                '-I',str(ROOT/'firmware/platform/xilinx'),str(c),*[str(ROOT/f) for f in files],'-lm','-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run(wrapper+[str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS C64 actual turbo runtime',result.stdout)

if __name__=='__main__':unittest.main()
