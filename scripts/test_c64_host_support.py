#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check C64 host requests, real XPM word crossing and audio FIFO/inference."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-host-support')
    parser.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/2025.2/Vivado'))
    parser.add_argument('--simulation-only',action='store_true')
    args=parser.parse_args();build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT/'build') or build==ROOT/'build' or build.exists():
        raise ValueError('Require a fresh dedicated repository build/ directory')
    build.mkdir(parents=True)
    files=['hdl/common/multitini_audio_capture.sv','hdl/common/multitini_word_mailbox.sv',
        'hdl/common/multitini_async_mailbox.sv','third_party/appletini/hdl/cdc_bit_sync.sv',
        'hdl/core/c64/multitini_c64_host.sv','hdl/core/c64/multitini_c64_island_memory.sv',
        'hdl/sim/tb_multitini_audio_capture.sv','hdl/sim/tb_multitini_word_mailbox.sv','hdl/sim/tb_c64_host.sv',
        'scripts/run_native_tool.py','scripts/check_c64_audio_capture_synthesis.tcl',
        'scripts/test_c64_host_support.py']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    before={name:sha(ROOT/name) for name in files}
    xpm=args.vivado/'data/ip/xpm/xpm_cdc/hdl/xpm_cdc.sv';glbl=args.vivado/'data/verilog/src/glbl.v'
    receipt={'schema':1,'status':'RUNNING','source_sha256':before,'checks':[],
        'vendor_simulation_sources':{str(p):sha(p) for p in (xpm,glbl)},
        'limits':'Simulation with actual XPM library and production endpoints. Synthesis checks RAM resource inference only; no routed timing or board acceptance.'}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'}
    env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    def run(name,argv):
        command=[str(a) for a in argv]
        if os.name=='nt':command=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*command]
        result=subprocess.run(command,cwd=build,env=env,capture_output=True,text=True,errors='replace',timeout=600,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        output=result.stdout+result.stderr;(build/(name+'.log')).write_text(output)
        if result.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',output,re.I):
            raise RuntimeError(name+'\n'+output[-8000:])
        return output
    def expect(output,prefix):
        lines=[line for line in output.splitlines() if line.startswith(prefix)]
        if len(lines)!=1:raise RuntimeError('Missing unique pass marker '+prefix)
        receipt['checks'].append(lines[0]);print(lines[0],flush=True)
    try:
        for name,top,sources in [
            ('audio','tb_multitini_audio_capture',[files[0],files[6]]),
            ('host','tb_c64_host',[files[4],files[5],files[8]])]:
            executable=build/(name+'.vvp')
            run(name+'-compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s',top,'-o',executable,*[ROOT/p for p in sources]])
            output=run(name+'-simulate',['C:/msys64/ucrt64/bin/vvp.exe',executable])
            expect(output,'PASS audio_capture ' if name=='audio' else 'PASS c64_host ')
        tool=lambda name:args.vivado/'bin'/(name+('.bat' if os.name=='nt' else ''))
        run('xvlog-vendor',[tool('xvlog'),'--nolog','--sv',xpm,glbl])
        run('xvlog-mailbox',[tool('xvlog'),'--nolog','--sv',*[ROOT/files[i] for i in (1,2,3,7)]])
        for width in (32,102,192):
            label='mailbox_'+str(width)
            wrapper=build/(label+'.sv')
            wrapper.write_text(f'module {label}; tb_multitini_word_mailbox #(.WIDTH({width})) dut(); endmodule\n')
            run('xvlog-'+label,[tool('xvlog'),'--nolog','--sv',wrapper])
            run('xelab-'+label,[tool('xelab'),'--nolog','--relax','--timescale','1ns/1ps',
                '--snapshot',label+'_sim','work.'+label,'work.glbl'])
            output=run(label+'-simulate',[tool('xsim'),'--nolog',label+'_sim','--runall'])
            expect(output,'PASS word_mailbox ')
        if not args.simulation_only:
            output=run('audio-synthesis',[tool('vivado'),' -mode'.strip(),'batch','-nolog','-nojournal','-source',ROOT/'scripts/check_c64_audio_capture_synthesis.tcl'])
            expect(output,'PASS: audio capture ')
        receipt['sources_unchanged']=before=={name:sha(ROOT/name) for name in files}
        if not receipt['sources_unchanged']:raise RuntimeError('Host support sources changed')
        receipt['status']='PASS'
    except Exception as error:
        receipt['status']='FAIL';receipt['error']=str(error);raise
    finally:(build/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0


if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
