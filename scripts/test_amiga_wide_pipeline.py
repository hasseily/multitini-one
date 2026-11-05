#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check full1280x512 virtual capture before real1024x768 clipped scanout."""
import argparse,hashlib,json,os,re,subprocess,sys
from pathlib import Path
from test_amiga_frame_profiles import BUFFER
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--build-dir',type=Path,default=ROOT/'build/amiga-wide-pipeline')
 p.add_argument('--mutation-clipped-capture',action='store_true')
 p.add_argument('--bypass-frame-effects',action='store_true')
 a=p.parse_args();out=a.build_dir.resolve()
 if not out.is_relative_to(ROOT/'build'):p.error('Use project build evidence')
 out.mkdir(parents=True,exist_ok=True)
 sources=BUFFER[:-1]+['hdl/common/multitini_display_pkg.sv','hdl/common/multitini_display_geometry.sv','hdl/common/multitini_display_geometry_pair.sv','hdl/sim/tb_amiga_wide_pipeline.sv']
 tracked=sources+['scripts/test_amiga_wide_pipeline.py','scripts/test_amiga_frame_profiles.py','scripts/run_native_tool.py']
 hashes=lambda:{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in tracked}
 before=hashes();results=[];passed=False
 runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
 tool=lambda n:str(runtime/(n+'.exe')) if os.name=='nt' else n
 guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'] if os.name=='nt' else []
 commands=[('compile',[tool('iverilog'),'-g2012','-Wall','-Wimplicit','-s','tb_amiga_wide_pipeline',
  '-P','tb_amiga_wide_pipeline.OMIT_VIRTUAL_CAPTURE='+str(int(a.mutation_clipped_capture)),
  '-P','tb_amiga_wide_pipeline.BYPASS_FRAME_EFFECTS='+str(int(a.bypass_frame_effects)),
  '-o',str(out/'test.vvp')]+[str(ROOT/n) for n in sources]),('simulate',[tool('vvp'),str(out/'test.vvp')])]
 try:
  for name,cmd in commands:
   r=subprocess.run(guard+cmd,cwd=ROOT,capture_output=True,text=True,timeout=600)
   log=r.stdout+r.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8')
   results.append(dict(name=name,command=cmd,exit_code=r.returncode,log=name+'.log',sha256=hashlib.sha256(log.encode()).hexdigest()))
   if name=='simulate' and a.mutation_clipped_capture:
    if not r.returncode or 'Wide virtual capture geometry missing' not in log:raise RuntimeError('Missing clipped-capture mutation rejection: '+log)
    print('PASS clipped-capture mutation rejected',flush=True)
   elif r.returncode or re.search(r'\b(?:FATAL|ERROR):',log):raise RuntimeError(log[-10000:])
   elif name=='simulate':
    print(log,flush=True)
    if 'PASS wide virtual pipeline:' not in log:raise RuntimeError('Missing completion')
  passed=True
 finally:
  after=hashes();(out/'validation.json').write_text(json.dumps(dict(passed=passed and before==after,
   board_evidence=False,expected_clipped_capture_failure=a.mutation_clipped_capture,source_sha256=before,source_sha256_after=after,results=results),indent=2)+'\n')
 if before!=after:raise RuntimeError('Wide pipeline source changed during run')
if __name__=='__main__':main()
