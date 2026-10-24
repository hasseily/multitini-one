#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise C64 PSRAM ownership, expansion CDC and capability registers."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-expansion-infrastructure')
    parser.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/2025.2/Vivado'))
    args=parser.parse_args()
    build=args.build_dir.resolve();base=(ROOT/'build').resolve()
    if build==base or not build.is_relative_to(base):parser.error('Use a dedicated build/ child')
    build.mkdir(parents=True,exist_ok=True)
    receipt=build/'validation.json';receipt.unlink(missing_ok=True)
    names=['hdl/generated/multitini_regs_pkg.sv','hdl/common/multitini_control_regs.sv',
        'third_party/appletini/hdl/cdc_bit_sync.sv','hdl/common/multitini_async_mailbox.sv',
        'hdl/core/c64/multitini_c64_memory_arbiter.sv','hdl/core/c64/multitini_c64_expansion_config.sv',
        'hdl/sim/tb_c64_memory_arbiter.sv','hdl/sim/tb_c64_expansion_config.sv','hdl/sim/tb_control_regs.sv']
    sources=[ROOT/name for name in names]
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    hashes=lambda:{p.relative_to(ROOT).as_posix():sha(p) for p in [Path(__file__).resolve(),*sources]}
    initial=hashes()
    xpm=args.vivado/'data/ip/xpm/xpm_cdc/hdl/xpm_cdc.sv'
    glbl=args.vivado/'data/verilog/src/glbl.v'
    evidence={'status':'FAIL','source_sha256':initial,'checks':[],
        'vendor_simulation_sources':{str(p):sha(p) for p in (xpm,glbl)},
        'limits':'Behavioral simulation with installed Vivado XPM CDC implementation; no physical metastability, routed timing or board claim.'}
    environment={k:v for k,v in os.environ.items() if k.upper()!='PATH'}
    environment['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+os.environ.get('PATH',os.environ.get('Path',''))
    guard=[sys.executable,ROOT/'scripts/run_native_tool.py','--']
    vendor=lambda name:args.vivado/'bin'/(name+('.bat' if os.name=='nt' else ''))
    def run(name,command):
        child=subprocess.run([str(a) for a in [*guard,*command]],cwd=build,env=environment,
            capture_output=True,text=True,errors='replace',timeout=300,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        text=child.stdout+child.stderr;(build/(name+'.log')).write_text(text)
        if child.returncode or re.search(r'\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b',text,re.I):
            raise RuntimeError(name+' failed: '+text[-16000:])
        return text
    try:
        run('xvlog-vendor',[vendor('xvlog'),'--nolog','--sv',xpm,glbl])
        run('xvlog',[vendor('xvlog'),'--nolog','--sv',*sources])
        for label,top,parameters,marker in (
            ('arbiter','tb_c64_memory_arbiter',[],'PASS: C64 memory arbiter '),
            ('config','tb_c64_expansion_config',[],'PASS: C64 expansion config '),
            ('registers-no-caps','tb_control_regs',[],'PASS: control registers'),
            ('registers-c64-caps','tb_control_regs_c64_caps',[],'PASS: control registers'),
            ('registers-c64-turbo','tb_control_regs_c64_turbo',[],'PASS: control registers')):
            snapshot=label.replace('-','_')+'_sim'
            run(label+'-elaborate',[vendor('xelab'),'--nolog','--timescale','1ns/1ps',
                '--snapshot',snapshot,*parameters,top,'glbl'])
            output=run(label,[vendor('xsim'),'--nolog',snapshot,'--runall'])
            passes=[line for line in output.splitlines() if line.startswith(marker)]
            if len(passes)!=1:raise RuntimeError(label+' lacks a unique pass marker')
            evidence['checks'].append({'name':label,'status':'PASS','summary':passes[0],
                'log_sha256':sha(build/(label+'.log'))})
            print(passes[0],flush=True)
        if hashes()!=initial:raise RuntimeError('Source changed during infrastructure tests')
        evidence.update(status='PASS',sources_unchanged=True)
    except Exception as error:
        evidence['error']=str(error);raise
    finally:receipt.write_text(json.dumps(evidence,indent=2)+'\n')
    return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
