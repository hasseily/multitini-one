# SPDX-License-Identifier: GPL-3.0-only
"""Execute real turbo/title/apply/debug transactions with mocked MMIO and SD."""
from pathlib import Path
import os, re, shutil, subprocess, sys, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tests.test_c64_expansion_runtime import production_function
SCENARIOS = r'''
static void reset_scenario(void) {
    memset(&c64_debug,0,sizeof c64_debug);memset(&c64_live_sid_table,0,sizeof c64_live_sid_table);
    held=hold_fail=attached=save_fail=map_save_fail=c64_sid_commit_fault=false;
    writes=saves=map_saves=cpu_writes=legacy_calls=new_reads=hold_count=table_writes=commits=commit_mode=0;
    cpu_config=sid_status=ua_fault=status_report=0;ticks=0;cpu_caps=MT_C64_CPU_CAPS_RELEASE;
    initialize();supervisor.active.package.abi_minor=24;
    assert(ensure_active_settings_profile());persistent_settings.profiles[0].c64_cpu_turbo=false;
    persistent_settings.c64_turbo_detail=0x41;refresh_c64_title(&supervisor.active);
    configure_c64_expansion(&supervisor.active,NULL);
    writes=saves=cpu_writes=legacy_calls=new_reads=hold_count=0;
}
static void override_speed(void) {
    c64_debug.session=c64_debug.override_active=true;c64_debug.override_index=15;c64_debug.caps.cpu=cpu_caps;
    configure_c64_turbo(&supervisor.active,&persistent_settings.profiles[0],true);
    cpu_writes=legacy_calls=0;
}
int main(void) {
    reset_scenario();supervisor.active.package.abi_minor=23;change_c64_turbo(9);
    assert(new_reads==0&&cpu_writes==0&&saves==0);
    reset_scenario();change_c64_turbo(9);
    assert((cpu_config&0x7f)==0x51&&saves==1&&cpu_writes==1);
    assert((cpu_config&MT_C64_CPU_CONFIG_APPLY_MASK)==0x1000);
    for(unsigned i=0;i<16;i++)change_c64_turbo(12);
    assert((cpu_config&15)==15&&persistent_settings.profiles[0].c64_cpu_turbo);
    uint32_t counter=cpu_config&MT_C64_CPU_CONFIG_APPLY_MASK;change_c64_turbo(12);
    assert((cpu_config&MT_C64_CPU_CONFIG_APPLY_MASK)==counter);
    change_c64_turbo(13);assert((cpu_config&15)==14);change_c64_turbo(9);
    assert((cpu_config&MT_C64_CPU_CONFIG_MODE_MASK)==0);
    unsigned count=saves;change_c64_turbo(13);assert(saves==count&&!strcmp(message,"Turbo is off"));
    reset_scenario();override_speed();change_c64_turbo(13);
    assert(!c64_debug.override_active&&(cpu_config&MT_C64_CPU_CONFIG_MODE_MASK)==0&&saves==0&&cpu_writes==1);
    reset_scenario();override_speed();save_fail=true;change_c64_turbo(9);
    assert(!c64_debug.override_active&&!persistent_settings.profiles[0].c64_cpu_turbo);
    assert((cpu_config&MT_C64_CPU_CONFIG_MODE_MASK)==0&&saves==1&&!held);
    reset_scenario();save_fail=true;uint32_t before=cpu_config;change_c64_turbo(9);
    assert((cpu_config&~MT_C64_CPU_CONFIG_APPLY_MASK)==(before&~MT_C64_CPU_CONFIG_APPLY_MASK));
    assert(!persistent_settings.profiles[0].c64_cpu_turbo&&!held&&strstr(message,"previous value restored"));
    reset_scenario();override_speed();change_c64_turbo(7);change_c64_turbo(10);change_c64_turbo(99);
    assert(c64_debug.override_active&&cpu_writes==0&&saves==0);
    reset_scenario();status_report=MT_C64_CPU_STATUS_DISK|(2U<<6);change_c64_turbo(9);
    assert(strstr(message,"2 MHz saved")&&strstr(message,"now 1 MHz")&&strstr(message,"cable held"));
    reset_scenario();cpu_caps=(cpu_caps&~MT_C64_CPU_CAPS_INDEX_MASK)|5U;
    refresh_c64_title(&supervisor.active);change_c64_turbo(4);
    assert((persistent_settings.c64_turbo_detail&15)==2&&(cpu_config&15)==2);
    reset_scenario();override_speed();before=cpu_config;hold_fail=true;change_c64_turbo(13);
    assert(c64_debug.override_active&&cpu_config==before&&!saves&&!held);
    reset_scenario();override_speed();before=cpu_config;hold_fail=true;change_c64_turbo(9);
    assert(c64_debug.override_active&&cpu_config==before&&!saves&&!held);
    puts("PASS C64 actual turbo runtime: gates, actions, limits, apply counter, override termination, failed HOLD and save rollback");
    return 0;
}
'''
class TurboRuntime(unittest.TestCase):
    def test_transactions(self):
        compiler=os.environ.get('CC') or shutil.which('gcc') or shutil.which('cc')
        self.assertIsNotNone(compiler,'A C compiler is required')
        with tempfile.TemporaryDirectory(prefix='c64-turbo-runtime-',dir=ROOT/'build') as directory:
            directory=Path(directory)
            source=(ROOT/'firmware/platform/xilinx/main.c').read_text()
            fixture=(ROOT/'tests/host/c64_title_runtime.inc').read_text()
            for signature in ('void mt_c64_debug_clear_override(', 'uint32_t mt_c64_debug_effective_overlay('):
                fixture=fixture.replace(production_function(fixture,signature),'')
            fixture=fixture.replace('static uint32_t shell_read(',
                'static unsigned cpu_writes,legacy_calls,new_reads;\nstatic uint32_t cpu_caps=MT_C64_CPU_CAPS_RELEASE,status_report;\nstatic uint32_t shell_read(')
            fixture=fixture.replace('return held ? MT_C64_CPU_STATUS_PAUSE : 0;',
                'return status_report | (held ? MT_C64_CPU_STATUS_PAUSE : 0);')
            fixture=fixture.replace('return MT_C64_CPU_CAPS_RELEASE;', 'return (++new_reads,cpu_caps);')
            fixture=fixture.replace('cpu_config = value;', '{cpu_config = value;++cpu_writes;}')
            fixture=fixture.replace('legacy_enabled = enabled;', 'legacy_enabled = enabled;++legacy_calls;')
            signatures=('static bool ensure_active_settings_profile(void)',
                'static mt_c64_capabilities_t selection_c64_features(', 'static mt_c64_title_values_t c64_stored_values(',
                'static void refresh_c64_title(', 'static mt_c64_title_values_t c64_requested_values(',
                'static int apply_c64_title(', 'static int uci_apply_config(', 'static void configure_c64_turbo(', 'static void configure_c64_expansion(',
                'static bool commit_c64_edit(', 'static void change_c64_expansion(', 'static void change_c64_turbo(',
                'static unsigned cycle_setting(', 'static void change_c64_sid_setup(',
                'static void c64_title_launched(', 'static mt_c64_content_service_result_t c64_title_load(',
                'static mt_c64_content_service_result_t c64_title_disk(',
                'static const char *c64_content_failure_message(')
            content=(ROOT/'firmware/common/content_type.c').read_text()
            classifier='\n'.join(production_function(content,s) for s in (
                'static char ascii_lower(', 'static bool extension_is(',
                'mt_content_type_t mt_content_classify(const char *path)'))
            fixture=fixture.replace('/* PRODUCTION_FUNCTIONS */',classifier+'\n'+
                '\n'.join(production_function(source,s) for s in signatures))
            media_failure=re.findall(
                r'set_supervisor_error\(c64_content_failure_message\(result\.status, remounted\)\);', source)
            self.assertEqual(len(media_failure),1,'Expected actual Media failure call')
            fixture=fixture.replace('/* PRODUCTION_MENU_FAILURE */',media_failure[0])
            fixture=fixture.replace('int main(int argc, char **argv)','int fixture_main(int argc, char **argv)')
            c=directory/'runtime.c';c.write_text(fixture+SCENARIOS)
            exe=directory/('runtime.exe' if os.name=='nt' else 'runtime')
            files=['firmware/platform/xilinx/supervisor_settings.c','firmware/platform/xilinx/c64_title_service.c']+[
                'firmware/common/'+n+'.c' for n in ('gamepad','controls','video_effects','c64_features','c64_debug',
                'c64_reu_host','c64_sid','c64_title','media_browser')]
            guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
            env={k:v for k,v in os.environ.items() if k.upper()!='PATH'}
            env['PATH']=str(Path(compiler).resolve().parent)+os.pathsep+os.environ.get('PATH',os.environ.get('Path',''))
            env['TEMP']=env['TMP']=str(directory)
            command=guard+[compiler,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(ROOT/'firmware/include'),
                '-I',str(ROOT/'firmware/platform/xilinx'),str(c),*[str(ROOT/f) for f in files],'-lm','-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True,timeout=60,env=env)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run(guard+[str(exe)],capture_output=True,text=True,timeout=30,env=env)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS C64 actual turbo runtime',result.stdout)
if __name__=='__main__': unittest.main()
