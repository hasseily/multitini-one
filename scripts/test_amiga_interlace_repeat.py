#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check four native Copper-reloaded field pairs per case; no hardware access."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from test_amiga_ports import ROOT,machine_sources

def main():
 p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,default=ROOT/'build/amiga-interlace-repeat-tests')
 p.add_argument('--case',action='append',choices=['standard','maximum','field-vstep','late-blank-pauses','base-auto'])
 p.add_argument('--mutation',choices=['force-bob'])
 a=p.parse_args();build=a.build_dir.resolve()
 if not build.is_relative_to(ROOT/'build'):p.error('Use repository build directory')
 build.mkdir(parents=True,exist_ok=True)
 sources=[s for s in machine_sources() if s.suffix=='.v' or s.name=='amiga_ram.sv']
 sources += [ROOT/'hdl/board/multitini_amiga_video_normalizer.sv',ROOT/'hdl/sim/tb_amiga_interlace_repeat.sv']
 if a.mutation:
  original=ROOT/'hdl/board/multitini_amiga_video_normalizer.sv';mutant=build/'normalizer-force-bob.sv'
  text=original.read_text();needle='wire want_weave=native_lace && !ntsc;'
  if text.count(needle)!=1:raise SystemExit('Weave policy changed')
  mutant.write_text(text.replace(needle,'wire want_weave=capture_mode!=0 && native_lace && !ntsc;'))
  sources[sources.index(original)]=mutant
 tracked=list(dict.fromkeys(sources+[ROOT/'hdl/board/multitini_amiga_video_normalizer.sv']+[Path(__file__),ROOT/'scripts/test_amiga_ports.py']))
 receipt={'board_evidence':False,'sources':{s.relative_to(ROOT).as_posix():hashlib.sha256(s.read_bytes()).hexdigest() for s in tracked},'cases':[]}
 guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
 def run(name,args):
  r=subprocess.run(guard+args,cwd=ROOT,capture_output=True,text=True,errors='replace');(build/(name+'.log')).write_text(r.stdout+r.stderr)
  if r.returncode and not(a.mutation and name!='compile'):print(r.stdout+r.stderr);raise SystemExit(r.returncode)
  return r.stdout+r.stderr
 exe=build/'resolution.vvp'
 # The pinned Verilog uses #1 pipeline delays without local timescales. Put
 # the explicitly timed fixture first, matching the native geometry runner.
 run('compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s','tb_amiga_interlace_repeat','-I',str(ROOT/'third_party/amiga/upstream/rtl'),'-o',str(exe),str(sources[-1]),*map(str,sources[:-1])])
 cases={'standard':['+WEAVE'],'maximum':['+MAX'],'field-vstep':['+WEAVE','+FIELD_VSTEP'],'late-blank-pauses':['+WEAVE','+FIELD_VSTEP','+LATE_BLANK','+PAUSES'],'base-auto':['+BASE_AUTO','+PAUSES']}
 for name,args in cases.items():
  if a.case and name not in a.case:continue
  log=run(name,['C:/msys64/ucrt64/bin/vvp.exe',str(exe),*args])
  if a.mutation:
   if 'Native profile metadata mismatch' not in log:raise SystemExit('Mutation was not rejected for the expected reason '+name)
  elif 'PASS repeated native Copper weave' not in log:raise SystemExit('Missing completion '+name)
  receipt['cases'].append(name);print(log.strip(),flush=True)
 for name,h in receipt['sources'].items():
  if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=h:raise SystemExit('Input changed '+name)
 receipt['mutation']=a.mutation;receipt['result']='mutation_rejected' if a.mutation else 'passed';(build/'report.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
