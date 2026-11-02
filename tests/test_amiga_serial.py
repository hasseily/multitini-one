#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute production headless core-selection and Amiga serial dispatch."""
from pathlib import Path
import argparse,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def extract(source,name,arguments="const char *line",return_type="bool",is_static=True):
    pattern=(r"static\s+" if is_static else "")+re.escape(return_type)+r"\s*"+re.escape(name)+r"\(([^;{}]*)\)\n\{"
    match=re.search(pattern,source);assert match is not None,name
    begin=match.start()
    assert " ".join(match[1].split())==" ".join(arguments.split()),name
    return source[begin:source.index("\n}",begin)+2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',required=True)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/amiga-serial-tests')
    args=parser.parse_args();out=args.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'firmware/platform/xilinx/main.c').read_text()
    ui=(ROOT/'firmware/common/ui.c').read_text()
    (out/'amiga_serial.inc').write_text(extract(ui,'mt_ui_status_menu_shown','uint32_t status','bool',False)+'\n\n'+
        extract(source,'amiga_request_display','uint8_t mode')+'\n\n'+
        extract(source,'amiga_profile_decimal','char text[21],uint64_t value','const char *')+'\n\n'+
        extract(source,'amiga_profile_microseconds','uint64_t ticks','uint64_t')+'\n\n'+
        extract(source,'amiga_profile_metric','const char *name,uint64_t ticks,uint64_t count, uint64_t bytes,uint64_t maximum','void')+'\n\n'+
        extract(source,'amiga_profile_native_microseconds','uint64_t cycles,uint32_t hz','uint64_t')+'\n\n'+
        extract(source,'amiga_profile_native_report','const mt_amiga_ide_profile_t *p','void')+'\n\n'+
        extract(source,'amiga_ide_profile_command')+'\n\n'+
        extract(source,'amiga_ide_bench_command')+'\n\n'+
        extract(source,'amiga_cpu_command')+'\n\n'+
        extract(source,'amiga_ide_command')+'\n\n'+
        extract(source,'queue_amiga_reset_shortcut','void','void')+'\n\n'+
        '\n\n'.join(extract(source,n) for n in ('amiga_serial_command','supervisor_serial_command')))
    binary=out/('test_amiga_serial.exe' if os.name=='nt' else 'test_amiga_serial')
    environment=os.environ.copy()
    if os.name=='nt':environment['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+environment.get('PATH','')
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
    compile=[args.cc,'-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(out),
             '-I'+str(ROOT/'firmware/include'),'-I'+str(ROOT/'firmware/platform/xilinx'),
             str(ROOT/'tests/host/test_amiga_serial.c'),'-o',str(binary)]
    for command in (compile,[str(binary)]):
        result=subprocess.run(guard+command,cwd=ROOT,env=environment)
        if result.returncode:return result.returncode
    return 0
if __name__=='__main__':raise SystemExit(main())
