#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Probe raw native ECS SuperHiRes cadence and independently calibrate origin."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from test_amiga_ports import ROOT,machine_sources

def main():
 p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,default=ROOT/'build/amiga-shres-probe')
 p.add_argument('--case',action='append',choices=['native-shres'])
 a=p.parse_args();build=a.build_dir.resolve()
 if not build.is_relative_to(ROOT/'build'):p.error('Use repository build directory')
 build.mkdir(parents=True,exist_ok=True)
 sources=[s for s in machine_sources() if s.suffix=='.v' or s.name=='amiga_ram.sv']
 sources += [ROOT/'hdl/sim/tb_amiga_shres_probe.sv']
 tracked=list(dict.fromkeys(sources+[Path(__file__),ROOT/'scripts/test_amiga_ports.py']))
 receipt={'board_evidence':False,'sources':{s.relative_to(ROOT).as_posix():hashlib.sha256(s.read_bytes()).hexdigest() for s in tracked},'cases':[]}
 guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
 def run(name,args):
  r=subprocess.run(guard+args,cwd=ROOT,capture_output=True,text=True,errors='replace');(build/(name+'.log')).write_text(r.stdout+r.stderr)
  if r.returncode:print(r.stdout+r.stderr);raise SystemExit(r.returncode)
  return r.stdout+r.stderr
 exe=build/'resolution.vvp'
 # The pinned Verilog uses #1 pipeline delays without local timescales. Put
 # the explicitly timed fixture first, matching the native geometry runner.
 run('compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s','tb_amiga_shres_probe','-I',str(ROOT/'third_party/amiga/upstream/rtl'),'-o',str(exe),str(sources[-1]),*map(str,sources[:-1])])
 cases={'native-shres':[]}
 for name,args in cases.items():
  if a.case and name not in a.case:continue
  log=run(name,['C:/msys64/ucrt64/bin/vvp.exe',str(exe),*args])
  if 'PASS native ECS SHRES' not in log:raise SystemExit('Missing completion '+name)
  receipt['cases'].append(name);print(log.strip(),flush=True)
 for name,h in receipt['sources'].items():
  if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=h:raise SystemExit('Input changed '+name)
 receipt['result']='passed';(build/'report.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
