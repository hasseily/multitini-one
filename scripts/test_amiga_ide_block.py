#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify sector-block host transport against the actual pinned native IDE."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['third_party/appletini/hdl/cdc_bit_sync.sv','hdl/sim/amiga_cdc_model.sv','hdl/common/multitini_async_mailbox.sv',
 'hdl/core/amiga/multitini_amiga_ide_block.sv','hdl/core/amiga/multitini_amiga_host.sv',
 'hdl/core/amiga/ports/amiga_ram.sv','hdl/core/amiga/ports/ide.v','hdl/sim/tb_amiga_ide_block.sv']
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'build/amiga-ide-block-tests')
    p.add_argument('--mutation',action='store_true',help='Prove the native read-pipeline assertion rejects missing idle edges')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    tracked=SOURCES+['scripts/test_amiga_ide_block.py']
    def hashes():return {n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in tracked}
    before=hashes();sources=[ROOT/n for n in SOURCES]
    if a.mutation:
        changed=out/'synthetic_bad_read_gap.sv';text=(ROOT/'hdl/core/amiga/multitini_amiga_ide_block.sv').read_text()
        assert text.count('if(gap_count!=2)')==1
        changed.write_text(text.replace('if(gap_count!=2)','if(gap_count!=0)',1))
        sources[SOURCES.index('hdl/core/amiga/multitini_amiga_ide_block.sv')]=changed
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    def tool(n):return str(runtime/(n+'.exe')) if os.name=='nt' else n
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'] if os.name=='nt' else []
    exe=out/'bulk.vvp'
    for stage,command in [('compile',[tool('iverilog'),'-g2012','-s','tb_amiga_ide_block','-o',str(exe),*[str(s) for s in sources]]),
                          ('simulate',[tool('vvp'),str(exe)])]:
        r=subprocess.run(guard+command,cwd=ROOT,capture_output=True,text=True,timeout=180)
        log=r.stdout+r.stderr;(out/(stage+'.log')).write_text(log)
        print(log[-10000:],flush=True)
        if r.returncode:
            if not(a.mutation and stage=='simulate' and ('guest PIO word' in log or 'ARM stage word' in log)):
                return r.returncode
            break
    else:
        if a.mutation:raise RuntimeError('FIFO read-gap mutation escaped the fixture')
        if 'PASS AMIGA BULK IDE' not in log:raise RuntimeError('Native block fixture did not finish')
    if hashes()!=before:raise RuntimeError('Sources changed during validation')
    record={'status':'EXPECTED_FAILURE_REPRODUCED' if a.mutation else 'PASS',
            'source_sha256':before,'board_evidence':False,'native_cpu_or_clock_changed':False}
    if a.mutation:record['synthetic_input']={'path':changed.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(changed.read_bytes()).hexdigest()}
    (out/'validation.json').write_text(json.dumps(record,indent=2)+'\n')
    return 0
if __name__=='__main__':raise SystemExit(main())
