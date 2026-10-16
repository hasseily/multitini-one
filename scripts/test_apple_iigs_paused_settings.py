#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify live settings and ARM-style card resets on unchanged production IIgs HDL."""
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
    commands=[]
    def run(command,name,expected_failure=None):
        log=build/(name+'.log')
        commands.append({'stage':name,'command':list(map(str,command))})
        with log.open('w') as stream:
            p=subprocess.run([str(x).replace('\\','/') for x in command],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
        text=log.read_text(errors='replace')
        if expected_failure is not None:
            if p.returncode==0 or expected_failure not in text:
                raise ValueError(name+' did not reject the intended missing firmware action:\n'+text[-12000:])
            return {'expected_rejection':True,'returncode':p.returncode,'assertion':expected_failure,
                    'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()}
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
    card_pattern=r'CARD_RESET_MEASURE case=(\d+) scenario=(\d+) requested=(\d+) card=(\d+) effective=(\d+) ticks=(\d+) advances=(\d+)'
    card_keys=['case','scenario','requested','card','effective','ticks','advances']
    card_results=[dict(zip(card_keys,map(int,m))) for m in re.findall(card_pattern,text)]
    if len(card_results)!=12 or text.count('PASS: core-reset-only card switches')!=1:
        raise ValueError('Incomplete reset-only card-mode coverage')
    expected_cards=[(scenario,speed,card,speed) for scenario in range(1,4)
                    for card,speed in [(2,0),(0,0),(0,4),(0,6)]]
    for index,(row,want) in enumerate(zip(card_results,expected_cards)):
        if row['case']!=index or tuple(row[k] for k in ['scenario','requested','card','effective'])!=want or row['advances']!=4096:
            raise ValueError('Unexpected reset-only card case: '+str(row))
        nominal=PRIMARY_HZ/(20,16,12,8,4,2,1)[row['effective']]
        row['measured_mhz']=row['advances']*PRIMARY_HZ/row['ticks']/1e6
        row['nominal_fraction']=row['measured_mhz']*1e6/nominal
        if abs(row['nominal_fraction']-1)>RATE_TOLERANCE:
            raise ValueError('Reset-only card speed ratio incorrect: '+str(row))
    seeds=re.findall(r'CARD_RESET_SEED scenario=(\d+) requested=(\d+) effective=(\d+) c059=ff c05c=fe unlock=4',text)
    if [tuple(map(int,s)) for s in seeds]!=[(1,6,4),(2,6,0),(3,0,6)]:
        raise ValueError('Actual guest speed/cache/slot/delay/unlock override seeds missing')
    if 'resets=9 measurements=12 global_resets=0' not in text:
        raise ValueError('Card-mode core-reset-only boundary was not demonstrated')
    negatives={
        'omit_mode_reset':run([executable,'+HDD_ROM='+fixture.as_posix(),'+NEG_SKIP_MODE_RESET'],
                             'negative-omit-mode-reset','Mode restart left stale ZipGS guest state'),
        'omit_none_speed_normalization':run([executable,'+HDD_ROM='+fixture.as_posix(),'+NEG_KEEP_NONE_SPEED'],
                             'negative-omit-none-normalization','None must use Stock')}
    if hashes()!=before:raise ValueError('Production/test inputs changed during regression')
    (build/'validation.json').write_text(json.dumps({'schema_version':1,'result':'PASS','files':before,
        'measurements':results,'card_reset_measurements':card_results,'guest_override_seeds':seeds,
        'negative_controls':negatives,'commands':commands,
        'hdd_rom_sha256':hashlib.sha256(data).hexdigest(),'assertions':{
            'all7_live_paused_loader_updates':True,'no_reset_within_live_changes':True,'paused_architecture_unchanged':True,
            'atomic_config_and_Zip_state_exact':True,'IRQ_delay_On_Off_On_effective':True,'source_hashes_unchanged':True,
            'card_switches_use_only_core_reset_request':True,'card_mode_restarts':9,
            'all_guest_Zip_state_cleared_before_host_flags_reapply':True,
            'None_normalized_to_Stock_and_guest_writes_ignored':True,'Zip_return_clean_Stock_then_live14_57':True,
            'production_machine_HDL_unchanged':True},
        'limits':['Focused firmware-strategy regression on unchanged machine HDL using synthetic code and a source-built licensed HDD ROM.',
                  'Configuration/restart packets model the existing ARM boot boundary; the ARM C service is verified separately.',
                  'Synthetic warm-loop rates are not OS boot time, desktop performance, DDR throughput or physical-board validation.']},indent=2)+'\n')
    print(text.strip());return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print('ERROR:',error,file=sys.stderr);raise SystemExit(1)
