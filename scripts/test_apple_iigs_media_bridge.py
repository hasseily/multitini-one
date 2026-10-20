#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the exact production IIgs media request/command CDC bridge blocks."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.dont_write_bytecode=True
TOP=ROOT/'hdl/board/multitini_one_top.sv'
BENCH=ROOT/'hdl/sim/tb_apple_iigs_media_bridge.sv'
INCLUDE='apple_iigs_media_bridge_production.svh'

def require(condition,message):
    if not condition:raise ValueError(message)

def extract(source):
    def block(start,end):
        require(source.count(start)==1 and source.count(end)==1,'Production bridge extraction anchors changed')
        a=source.index(start);b=source.index(end,a)
        return source[a:b]
    return '\n'.join((block('    // A request is captured once and held coherently in the control domain',
                             '    // Writeback bytes remain stable in the drive domain'),
        block('    always_ff @(posedge control_clk) begin\n        if (!control_aresetn) begin\n            media_request_valid_control <=',
              '    // Shell media commands and every associated field cross as one atomic'),
        block('    // Shell media commands and every associated field cross as one atomic',
              '    // Dense-menu visibility is a shared pause request')))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/iigs-floppy-idle/media-bridge-tests')
    parser.add_argument('--negative-controls',action='store_true',help='Prove old snapshot cancellation and held-VALID retry policies fail')
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    source_files=[TOP,BENCH,Path(__file__).resolve(),ROOT/'hdl/common/multitini_async_mailbox.sv',
                  ROOT/'hdl/sim/xilinx_primitives.sv',ROOT/'hdl/generated/multitini_regs_pkg.sv']
    hashes=lambda:{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    before=hashes();source=TOP.read_text(encoding='utf-8');production=extract(source)
    require('.IIGS_ACCEL_CAPS(ACTIVE_MACHINE==5 ? 32\'h8000037f : 32\'d0)' in source,
            'IIgs target-drain bridge capability is not advertised exclusively by the IIgs image')
    (build/INCLUDE).write_text(production,encoding='utf-8',newline='\n')
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
    iverilog='C:/msys64/ucrt64/bin/iverilog.exe';vvp='C:/msys64/ucrt64/bin/vvp.exe'
    def run(command,label,*,expected_failure=None):
        result=subprocess.run(guard+list(map(str,command)),cwd=ROOT,capture_output=True,text=True)
        out=result.stdout+result.stderr;(build/(label+'.log')).write_text(out,encoding='utf-8')
        if expected_failure is None:
            require(result.returncode==0,label+' failed: '+out[-7000:])
        else:
            require(result.returncode!=0 and expected_failure in out,
                    label+' did not fail the intended assertion: '+out[-7000:])
        return out,result.returncode
    def compile_case(machine,directory,label):
        directory.mkdir(exist_ok=True)
        executable=directory/'bridge.vvp'
        run([iverilog,'-g2012','-s','tb_apple_iigs_media_bridge','-P',f'tb_apple_iigs_media_bridge.ACTIVE_MACHINE={machine}',
            '-I',directory,'-o',executable,ROOT/'hdl/generated/multitini_regs_pkg.sv',
            ROOT/'hdl/sim/xilinx_primitives.sv',ROOT/'hdl/common/multitini_async_mailbox.sv',BENCH],label+'-compile')
        return executable
    cases=[]
    for machine in (5,0,1,2,3,4,6):
        directory=build/f'machine-{machine}';directory.mkdir(exist_ok=True)
        (directory/INCLUDE).write_text(production,encoding='utf-8',newline='\n')
        executable=compile_case(machine,directory,f'machine-{machine}')
        out,_=run([vvp,executable],f'machine-{machine}-simulate')
        count=6 if machine==5 else 1
        require(out.count('PASS:')==count and f'CHECKS completed={count} machine={machine}' in out,
                f'machine{machine} coverage incomplete: '+out)
        cases.append({'machine':machine,'checks':count,'log_sha256':hashlib.sha256(out.encode()).hexdigest()})
        print(out.strip(),flush=True)
    negatives=[]
    if args.negative_controls:
        snapshot="if (media_complete || (ACTIVE_MACHINE != 5 &&\n                (media_mount || (media_eject && !media_request_write_control))))"
        fresh="(!c64_media_request_sent ||\n            (ACTIVE_MACHINE == 5 &&\n             (c64_media_request_id != c64_media_request_sent_id ||\n              c64_media_request_generation != c64_media_request_sent_generation)))"
        reset_gate="c64_media_request_valid && (ACTIVE_MACHINE != 5 || !c64_core_reset_request) &&"
        mutations=(
            ('old_eject_read_clear',snapshot,"if (media_mount || media_complete ||\n                (media_eject && !media_request_write_control))",None,'unrelated EJECT cleared a retained read/write request'),
            ('mount_clears_unrelated_write',snapshot,"if (media_mount || media_complete ||\n                (ACTIVE_MACHINE != 5 && media_eject && !media_request_write_control))",'+ONLY_MOUNT_WRITE','unrelated MOUNT write cleared a retained read/write request'),
            ('old_valid_only_retry',fresh,'!c64_media_request_sent','+ONLY_RETRY','fresh retained retry never crossed: request snapshot did not arrive'),
            ('retry_ignores_generation',"c64_media_request_id != c64_media_request_sent_id ||\n              c64_media_request_generation != c64_media_request_sent_generation",'c64_media_request_id != c64_media_request_sent_id','+ONLY_RETRY','fresh retained retry never crossed: request snapshot did not arrive'),
            ('retry_identity_not_latched',"            c64_media_request_sent_id <= c64_media_request_id;\n            c64_media_request_sent_generation <= c64_media_request_generation;",'',None,'completion in flight lost source retention or replayed its snapshot'),
            ('reset_republishes_retained_request',reset_gate,'c64_media_request_valid &&','+ONLY_RESET_RETAINED','guest reset replayed stale LBA0 header over subsequent LBA1 metadata'),
        )
        for name,old,new,plusarg,message in mutations:
            require(production.count(old)==1,'Mutation anchor changed: '+name)
            directory=build/('negative-'+name);directory.mkdir(exist_ok=True)
            (directory/INCLUDE).write_text(production.replace(old,new),encoding='utf-8',newline='\n')
            executable=compile_case(5,directory,'negative-'+name)
            command=[vvp,executable]+([plusarg] if plusarg else [])
            out,code=run(command,'negative-'+name,expected_failure=message)
            negatives.append({'mutation':name,'status':'FAIL','expected_failure':message,'exit_code':code,
                              'log_sha256':hashlib.sha256(out.encode()).hexdigest()})
            print('negative control '+name+': failed as intended',flush=True)
    require(hashes()==before,'Production inputs changed during bridge regression; rerun current sources')
    captured=[]
    for source_file in source_files:
        target=build/'source'/source_file.relative_to(ROOT)
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source_file,target)
        captured.append({'path':str(target.relative_to(ROOT)).replace('\\','/'),
                         'source':source_file.relative_to(ROOT).as_posix(),
                         'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    (build/'validation.json').write_text(json.dumps({'schema_version':1,'status':'PASS','files':before,
        'captured_files':captured,
        'production_block_sha256':hashlib.sha256(production.encode()).hexdigest(),
        'extracted_from':'hdl/board/multitini_one_top.sv','exact_production_blocks':True,
        'machines':cases,'negative_controls':negatives,'negative_controls_run':args.negative_controls,
        'checks':['IIgs target and unrelated reads remain visible across MOUNT/EJECT until completion',
                  'IIgs reverse writes remain visible across unrelated MOUNT/EJECT',
                  'completion in flight clears the MMIO snapshot exactly once while core VALID remains high',
                  'MOUNT/EJECT at twelve asynchronous publication phases do not orphan or duplicate a new read',
                  'failed read/write operations retry with held VALID and fresh ID or generation, including ID wrap and retained tuple/buffer',
                  'source sent identity is reset and a future repeated identity can cross',
                  'guest reset retains an existing or newly mounted IIgs LBA0 read, with 64 asynchronous reset/release phases, and publishes it once after release before LBA1',
                  'all six other ACTIVE_MACHINE selections retain their old snapshot cancellation and retry policy',
                  'IIgs alone advertises the target-drain capability bit'],
        'limits':['Portable behavioral XPM model with asynchronous clocks, not routed FPGA CDC/timing or hardware acceptance.',
                  'The bridge blocks are exact production text; a small request-source model implements the IIgs retained-operation contract.',
                  'No CPU, file service, guest disk image or physical media writes are part of this unit regression.']},indent=2)+'\n',encoding='utf-8')
    return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
