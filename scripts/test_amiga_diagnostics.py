#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify passive native diagnostics and command transport with exact waveforms."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['third_party/appletini/hdl/cdc_bit_sync.sv','hdl/sim/amiga_cdc_model.sv','hdl/common/multitini_async_mailbox.sv',
 'hdl/core/amiga/multitini_amiga_ide_block.sv','hdl/core/amiga/multitini_amiga_diagnostics.sv','hdl/core/amiga/multitini_amiga_host.sv',
 'hdl/sim/tb_amiga_diagnostics.sv']
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/amiga-ecs/diagnostics820/focused01')
    p.add_argument('--mutation',choices=['req-gated','pio-ungated']);a=p.parse_args();out=a.output.resolve()
    if not out.is_relative_to(ROOT/'build'):raise SystemExit('Use a project-local build directory')
    out.mkdir(parents=True,exist_ok=True);sources=[ROOT/n for n in SOURCES]
    tracked=SOURCES+['scripts/test_amiga_diagnostics.py'];before={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in tracked}
    if a.mutation:
        old,new={'req-gated':('wire request_event=','wire request_event=tick_q && '),
                 'pio-ungated':('if(tick_q)begin','if(1)begin')}[a.mutation]
        text=sources[4].read_text();assert text.count(old)==1
        synthetic=out/('synthetic_'+a.mutation.replace('-','_')+'.sv');synthetic.write_text(text.replace(old,new,1));sources[4]=synthetic
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    def tool(n):return str(runtime/(n+'.exe')) if os.name=='nt' else n
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'] if os.name=='nt' else []
    exe=out/'diagnostics.vvp';failed=False
    for stage,cmd in [('compile',[tool('iverilog'),'-g2012','-s','tb_amiga_diagnostics','-o',str(exe),*[str(q) for q in sources]]),('simulate',[tool('vvp'),str(exe)])]:
        r=subprocess.run(guard+cmd,cwd=ROOT,capture_output=True,text=True,timeout=180);log=r.stdout+r.stderr;(out/(stage+'.log')).write_text(log);print(log[-6000:],flush=True)
        if r.returncode:
            expected='snapshot word4 expected0003 got0007' if a.mutation=='req-gated' else 'snapshot word60'
            if not(a.mutation and stage=='simulate' and expected in log):return r.returncode
            failed=True;break
    if a.mutation and not failed:raise RuntimeError('Timing mutation escaped independent oracle')
    if not a.mutation and 'PASS AMIGA DIAGNOSTICS' not in log:raise RuntimeError('Diagnostic fixture did not finish')
    if before!={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in tracked}:raise RuntimeError('Inputs changed during validation')
    record={'status':'EXPECTED_FAILURE_REPRODUCED' if a.mutation else 'PASS','source_sha256':before,'board_evidence':False,
            'bounds':'10-period independent input waveform; accepted/held, CPU request wait, IDE activity are exclusive; DDR detail overlaps; host uses four-stage coherent mailboxes'}
    if a.mutation:record['synthetic_input']={'path':synthetic.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(synthetic.read_bytes()).hexdigest()}
    (out/'validation.json').write_text(json.dumps(record,indent=2)+'\n');return 0
if __name__=='__main__':raise SystemExit(main())
