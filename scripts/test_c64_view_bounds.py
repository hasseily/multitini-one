#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check C64 viewport arithmetic and both live bridge configurations."""
from pathlib import Path
import subprocess,json,hashlib,sys,os
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/c64-view-bounds-tests'
OUT.mkdir(parents=True,exist_ok=True)
sources=[ROOT/p for p in ('hdl/common/multitini_display_pkg.sv','hdl/common/multitini_display_geometry.sv','hdl/sim/xilinx_primitives.sv','third_party/appletini/hdl/cdc_bit_sync.sv','hdl/board/multitini_video_test.sv')]+[ROOT/'hdl/board/multitini_c64_video_bridge.sv']
tracked=[Path(__file__).resolve()]+sources+[ROOT/'hdl/sim/tb_c64_view_bounds.sv',ROOT/'hdl/sim/tb_c64_video_bridge.sv']
sha=lambda:{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
before=sha()
def run(args,name):
    if os.name=='nt' and args[0] in ('iverilog','vvp'):
        args[0]=str(Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))/(args[0]+'.exe'))
    r=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']+list(map(str,args)),cwd=OUT,capture_output=True,text=True,timeout=300)
    (OUT/(name+'.log')).write_text(r.stdout+r.stderr)
    assert r.returncode==0,name
    print(r.stdout[-1000:],flush=True)
for mode in (0,1):
    name='bridge-'+str(mode)
    run(['iverilog','-g2012','-s','tb_c64_video_bridge','-P',f'tb_c64_video_bridge.DYNAMIC_DISPLAY={mode}','-o',OUT/(name+'.vvp')]+sources+[ROOT/'hdl/sim/tb_c64_video_bridge.sv'],name+'-compile')
    run(['vvp',OUT/(name+'.vvp')],name+'-run')
run(['iverilog','-g2012','-s','tb_c64_view_bounds','-o',OUT/'bounds.vvp']+sources+[ROOT/'hdl/sim/tb_c64_view_bounds.sv'],'bounds-compile')
run(['vvp',OUT/'bounds.vvp'],'bounds-run')
assert sha()==before
(OUT/'validation.json').write_text(json.dumps({'passed':True,'source_sha256':before},indent=2)+'\n')
