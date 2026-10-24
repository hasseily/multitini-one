#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""IEC9 protocol at release clocks and MMIO/FIFO transport with real XPM CDC."""
import argparse, hashlib, json, os, re, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-iec9')
    p.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/2025.2/Vivado'))
    a=p.parse_args();b=a.build_dir.resolve()
    if not b.is_relative_to(ROOT/'build') or b==ROOT/'build' or b.exists():
        raise ValueError('Use a fresh dedicated repository build directory')
    b.mkdir(parents=True)
    files=['hdl/core/c64/multitini_c64_iec9.sv','hdl/core/c64/multitini_c64_iec_transport.sv',
        'hdl/sim/tb_c64_iec9.sv','hdl/sim/tb_c64_iec_transport.sv','hdl/common/multitini_control_regs.sv',
        'hdl/common/multitini_word_mailbox.sv','hdl/common/multitini_async_mailbox.sv',
        'third_party/appletini/hdl/cdc_bit_sync.sv','hdl/generated/multitini_regs_pkg.sv',
        'spec/interface_v0.json','scripts/test_c64_iec9.py','scripts/run_native_tool.py',
        'hdl/core/c64/multitini_c64_adapter.sv','hdl/core/c64/multitini_c64_1541.sv',
        'hdl/core/c64/multitini_c64_cpu_turbo_guard.sv','hdl/core/c64/multitini_c64_defs_pkg.sv',
        'hdl/sim/tb_c64_iec_wiring.sv','hdl/board/multitini_one_top.sv']
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    hashes={f:sha(ROOT/f) for f in files}
    receipt={'status':'RUNNING','source_sha256':hashes,'checks':[],
        'limits':'Protocol and production MMIO transport simulation; actual-ROM integration, routed timing, and board acceptance are separate gates.'}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'}
    env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    def run(name,args):
        cmd=[str(x) for x in args]
        if os.name=='nt':cmd=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*cmd]
        r=subprocess.run(cmd,cwd=b,env=env,capture_output=True,text=True,errors='replace',timeout=600,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        output=r.stdout+r.stderr;(b/(name+'.log')).write_text(output)
        if r.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',output,re.I):
            raise RuntimeError(name+'\n'+output[-7000:])
        return output
    def passed(output,prefix):
        hits=[line for line in output.splitlines() if line.startswith(prefix)]
        if len(hits)!=1:raise RuntimeError('Missing unique '+prefix)
        receipt['checks'].append(hits[0]);print(hits[0],flush=True)
    try:
        for hz in (1000000,31524725,32727273):
            label='protocol-'+str(hz);exe=b/(label+'.vvp')
            run(label+'-compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s','tb_c64_iec9',
                '-P',f'tb_c64_iec9.CLOCK_HZ={hz}','-o',exe,ROOT/files[0],ROOT/files[2]])
            passed(run(label,['C:/msys64/ucrt64/bin/vvp.exe',exe]),'PASS IEC9 protocol ')
        # Reintroduce the real KERNAL defect: treating the initial CLK-high
        # at ATN as listener-ready shifts the first command byte by one bit.
        original=(ROOT/files[0]).read_text()
        mutant=b/'iec9_without_atn_low.sv'
        if original.count('state<=RX_ATN_DELAY')!=2:raise RuntimeError('ATN negative-control boundary changed')
        mutant.write_text(original.replace('state<=RX_ATN_DELAY','state<=RX_READY'))
        exe=b/'negative-atn.vvp'
        run('negative-atn-compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s','tb_c64_iec9',
            '-o',exe,mutant,ROOT/files[2]])
        try:run('negative-atn',['C:/msys64/ucrt64/bin/vvp.exe',exe])
        except RuntimeError as e:
            if 'DATA timeout' not in str(e):raise
            receipt['checks'].append('PASS negative control rejects missing ATN CLK-low qualification')
            receipt['negative_source_sha256']=sha(mutant)
        else:raise RuntimeError('ATN negative control incorrectly passed')
        # Icarus cannot bind these existing later declarations in net initializers;
        # relocate declarations only, preserving every production expression.
        adapter=(ROOT/files[12]).read_text()
        declarations='    logic [17:0] raw_audio_l;\n    logic [17:0] raw_audio_r;\n    integer unsigned audio_phase;\n'
        if adapter.count(declarations)!=1:raise RuntimeError('Adapter declaration boundary changed')
        adapter=adapter.replace(declarations,'').replace('    import multitini_c64_defs_pkg::*;',
            '    import multitini_c64_defs_pkg::*;\n'+declarations)
        ordered=b/'adapter_declarations_ordered.sv';ordered.write_text(adapter)
        receipt['wiring_adapter_sha256']=sha(ordered)
        receipt['wiring_limits']='Production adapter/1541 ports/endpoint/guard; unrelated leaves are black boxes. Driver outputs forced across64 combinations. Only declaration order adjusted for Icarus.'
        exe=b/'wiring.vvp'
        run('wiring-compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-i','-s','tb_c64_iec_wiring',
            '-o',exe,*[ROOT/files[i] for i in (8,15,0,14,13)],ordered,ROOT/files[16]])
        passed(run('wiring',['C:/msys64/ucrt64/bin/vvp.exe',exe]),'PASS IEC9 production wiring')
        xpm=a.vivado/'data/ip/xpm/xpm_cdc/hdl/xpm_cdc.sv';glbl=a.vivado/'data/verilog/src/glbl.v'
        receipt['vendor_source_sha256']={str(x):sha(x) for x in (xpm,glbl)}
        tool=lambda n:a.vivado/'bin'/(n+('.bat' if os.name=='nt' else ''))
        run('xvlog-vendor',[tool('xvlog'),'--nolog','--sv',xpm,glbl])
        run('xvlog-transport',[tool('xvlog'),'--nolog','--sv',*[ROOT/files[i] for i in (8,4,5,6,7,1,3)]])
        run('xelab-transport',[tool('xelab'),'--nolog','--relax','--timescale','1ns/1ps',
            '--snapshot','iec_transport_sim','work.tb_c64_iec_transport','work.glbl'])
        passed(run('transport',[tool('xsim'),'--nolog','iec_transport_sim','--runall']),'PASS IEC9 transport ')
        if hashes!={f:sha(ROOT/f) for f in files}:raise RuntimeError('Sources changed during suite')
        receipt['status']='PASS'
    except Exception as e:receipt.update(status='FAIL',error=str(e));raise
    finally:(b/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0
if __name__=='__main__':
    try:raise SystemExit(main())
    except (ValueError,OSError,RuntimeError,subprocess.SubprocessError) as e:
        print('ERROR: '+str(e),file=sys.stderr);raise SystemExit(1)
