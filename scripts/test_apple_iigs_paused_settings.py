#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify menu-style paused speed updates through the production IIgs loader."""
from __future__ import annotations
import argparse,hashlib,json,os,re,shutil,subprocess,sys
from pathlib import Path
from test_apple_iigs_ports import machine_sources

ROOT=Path(__file__).resolve().parents[1]
PRIMARY_HZ=57272728
RATE_TOLERANCE=0.005  # Includes authentic native line stretch and finite-window phase.

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/apple-iigs-paused-settings-tests')
    parser.add_argument('--hdd-rom',type=Path,default=ROOT/'build/apple-iigs-assets/hdd.rom')
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    data=args.hdd_rom.read_bytes()
    if len(data)!=256:raise ValueError('Source-built licensed HDD ROM must be256 bytes')
    fixture=build/'hdd.hex';fixture.write_text('\n'.join(f'{v:02x}' for v in data)+'\n')
    core=ROOT/'hdl/core/apple_iigs';top='tb_apple_iigs_paused_settings'
    sources=machine_sources()+[core/(n+'.sv') for n in ['multitini_apple_iigs_loader','multitini_apple_iigs_cache','multitini_apple_iigs_adapter']]
    sources += [ROOT/'hdl/sim/apple_iigs_bufgce.sv',ROOT/'hdl/sim'/f'{top}.sv']
    captured=sources+[ROOT/'third_party/apple-iigs/upstream/rtl/dhr_lut.vh',ROOT/'scripts/test_apple_iigs_ports.py',Path(__file__).resolve()]
    hashes=lambda:{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in captured}
    before=hashes();runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'));env=os.environ.copy()
    if os.name=='nt':
        env['PATH']=str(runtime)+os.pathsep+env.get('PATH','')
        env['VERILATOR_ROOT']=(runtime.parent/'share/verilator').as_posix()
    tool=lambda name:str(runtime/(name+'.exe')) if os.name=='nt' else shutil.which(name)
    def run(command,name):
        log=build/(name+'.log')
        with log.open('w') as stream:
            p=subprocess.run([str(x).replace('\\','/') for x in command],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
        text=log.read_text(errors='replace')
        if p.returncode or re.search(r'%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed',text):
            raise ValueError(name+' failed:\n'+text[-12000:])
        return text
    directory=build/'obj'
    run([tool('verilator_bin') if os.name=='nt' else tool('verilator'),'--cc','--exe','--main','--timing','-Wno-fatal',
         '--top-module',top,'-I'+str(ROOT/'third_party/apple-iigs/upstream/rtl'),'--Mdir',directory,*sources],'generate')
    executable=directory/('V'+top+('.exe' if os.name=='nt' else ''))
    if os.name=='nt':
        inc=runtime.parent/'share/verilator/include'
        run([tool('g++'),'-std=c++20','-O1','-pthread','-DVL_TIME_CONTEXT','-I'+str(inc),'-I'+str(inc/'vltstd'),'-I'+str(directory),
             *sorted(directory.glob('*.cpp')),inc/'verilated.cpp',inc/'verilated_timing.cpp',inc/'verilated_threads.cpp','-o',executable],'compile')
    else:run([tool('make'),'-C',directory,'-f','V'+top+'.mk','-j8'],'compile')
    text=run([executable,'+HDD_ROM='+fixture.as_posix()],'simulate')
    pattern=r'PAUSED_MEASURE case=(\d+) requested=(\d+) flags=(\d+) i_flag=(\d+) effective=(\d+) ticks=(\d+) advances=(\d+)'
    keys=['case','requested','flags','i_flag','effective','ticks','advances']
    results=[dict(zip(keys,map(int,m))) for m in re.findall(pattern,text)]
    if len(results)!=10 or text.count('PASS: live paused settings')!=1:raise ValueError('Incomplete paused-settings coverage')
    expected=[(n,0,0,n) for n in range(7)]+[(6,0,1,0),(6,16,1,6),(6,0,1,0)]
    for index,(row,want) in enumerate(zip(results,expected)):
        if row['case']!=index or tuple(row[k] for k in ['requested','flags','i_flag','effective'])!=want or row['advances']!=4096:
            raise ValueError('Unexpected paused-settings case: '+str(row))
        nominal=PRIMARY_HZ/(20,16,12,8,4,2,1)[row['effective']]
        row['measured_mhz']=row['advances']*PRIMARY_HZ/row['ticks']/1e6
        row['nominal_fraction']=row['measured_mhz']*1e6/nominal
        if abs(row['nominal_fraction']-1)>RATE_TOLERANCE:raise ValueError('Paused speed ratio incorrect: '+str(row))
    if hashes()!=before:raise ValueError('Production/test inputs changed during regression')
    (build/'validation.json').write_text(json.dumps({'schema_version':1,'result':'PASS','files':before,
        'measurements':results,'hdd_rom_sha256':hashlib.sha256(data).hexdigest(),'assertions':{
            'all7_live_paused_loader_updates':True,'no_reset_within_live_changes':True,'paused_architecture_unchanged':True,
            'atomic_config_and_Zip_state_exact':True,'IRQ_delay_On_Off_On_effective':True,'source_hashes_unchanged':True},
        'limits':['Synthetic register-only warm-ROM loop; not a desktop, DDR, peripheral or hardware performance measurement.']},indent=2)+'\n')
    print(text.strip());return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print('ERROR:',error,file=sys.stderr);raise SystemExit(1)
