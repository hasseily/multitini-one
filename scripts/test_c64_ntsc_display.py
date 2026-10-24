#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check native NTSC capture and dynamic geometry for all six output modes."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args()
    out=a.build_dir.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build'):p.error('Use a new directory under build/')
    out.mkdir(parents=True)
    names=['hdl/common/multitini_display_pkg.sv','hdl/common/multitini_display_geometry.sv',
        'hdl/sim/xilinx_primitives.sv','third_party/appletini/hdl/cdc_bit_sync.sv',
        'hdl/board/multitini_c64_video_bridge.sv','hdl/sim/tb_c64_ntsc_video_bridge.sv']
    tracked=names+['scripts/test_c64_ntsc_display.py','scripts/run_native_tool.py']
    snapshot=lambda:{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in tracked}
    before=snapshot();cases=[]
    prefix=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    def tool(name):return str(prefix/(name+'.exe')) if os.name=='nt' else name
    for dynamic,mode in [(0,0)]+[(1,n) for n in range(6)]:
        tag=f'dynamic{dynamic}-mode{mode}';vvp=out/(tag+'.vvp')
        commands=[[tool('iverilog'),'-g2012','-s','tb_c64_ntsc_video_bridge',
            '-P',f'tb_c64_ntsc_video_bridge.DYNAMIC_DISPLAY={dynamic}',
            '-P',f'tb_c64_ntsc_video_bridge.MODE={mode}','-o',str(vvp),*[str(ROOT/n) for n in names]],
            [tool('vvp'),str(vvp)]]
        for step,cmd in enumerate(commands):
            r=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*cmd],
                cwd=out,capture_output=True,text=True,timeout=300)
            (out/f'{tag}-{step}.log').write_text(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError(f'{tag} step{step}: '+r.stdout[-1600:]+r.stderr[-800:])
        if 'PASS: 404x250 NTSC capture' not in r.stdout:raise RuntimeError('Missing PASS')
        cases.append({'dynamic':dynamic,'mode':mode,'status':'PASS'})
        print(tag+' PASS',flush=True)
    assert snapshot()==before,'Sources changed during test'
    (out/'validation.json').write_text(json.dumps({'status':'PASS','checks':cases,'source_sha256':before,
        'limits':'Native capture/palette/ownership plus actual Auto and signed scanout geometry. DDR pixel-reader execution has its separate regression; no physical monitor proof.'},indent=2)+'\n')
if __name__=='__main__':main()
