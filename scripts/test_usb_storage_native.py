#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the USB0 SD port against the actual ARM compiler and generated BSP."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FILES=['firmware/platform/xilinx/'+name+'.c' for name in
       ('gic_init','usb0_personality','usb_storage_service','usb_storage_sd',
        'xusbps_ch9','xusbps_ch9_storage','xusbps_class_storage','sd_storage')]
FILES+=['firmware/common/usb_storage_mode.c']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bsp',type=Path,required=True,help='Generated domain containing include/ and Xilinx.spec')
    parser.add_argument('--compiler',type=Path,default=Path(os.environ.get('XILINX_VITIS','C:/Xilinx/2025.2/Vitis'))/'gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe')
    parser.add_argument('--output',type=Path,default=ROOT/'build/usb-storage-native')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    bsp=args.bsp.resolve();assert (bsp/'include/xusbps.h').is_file() and args.compiler.is_file()
    before={name:digest(ROOT/name) for name in FILES}
    commands=[]
    flags=['-DSDT','-std=gnu11','-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=hard',
           '-O2','-Wall','-Wextra','-Werror','-I'+str(bsp/'include'),
           '-I'+str(ROOT/'firmware/include'),'-I'+str(ROOT/'firmware/platform/xilinx')]
    log=[]
    for name in FILES:
        command=[str(args.compiler),*flags,'-c',str(ROOT/name),'-o',str(out/(Path(name).stem+'.o'))]
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
        log.append('$ '+' '.join(command)+'\n'+result.stdout+result.stderr);commands.append(command)
        (out/'compile.log').write_text('\n'.join(log))
        if result.returncode:
            print(result.stdout+result.stderr);return result.returncode
    assert before=={name:digest(ROOT/name) for name in FILES},'USB source changed during native compile'
    (out/'validation.json').write_text(json.dumps({'status':'PASS','sources':before,
        'compiler':str(args.compiler),'compiler_sha256':digest(args.compiler),
        'bsp':str(bsp),'xparameters_sha256':digest(bsp/'include/xparameters.h'),
        'commands':commands,'scope':'Actual vendor ARM object compilation; full firmware link and hardware testing separate'},indent=2)+'\n')
    print('PASS actual ARM/BSP USB storage compilation:',len(FILES),'production sources')
    return 0
if __name__=='__main__':raise SystemExit(main())
