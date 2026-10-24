#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare native UA boxcar tones with the continuous hold/integrate spectrum."""
from __future__ import annotations
import argparse,csv,hashlib,json,os,re,subprocess,sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
CLOCK=31524725;OUTPUT=48000;SAMPLES=8192;WARM=1024

def waveform(source_rate,divider,frequency):
    period=2*divider+3;hold=period/12500000.;omega=2*np.pi*frequency/source_rate
    end=np.arange(1,SAMPLES+WARM+2,dtype=np.int64)
    ticks=(end*CLOCK+OUTPUT-1)//OUTPUT
    clocks=np.arange(1,int(ticks[-1])+1,dtype=np.int64)
    position=(((clocks-9)*12500000//CLOCK)//period)-1
    pcm=np.rint(12000*np.sin(omega*np.arange(16384))).astype(np.int64)
    signal=pcm[np.maximum(position,0)];signal[position<0]=0
    mixed=signal*7//8;sums=np.r_[0,np.cumsum(mixed)]
    indices=ticks;starts=np.r_[0,indices[:-1]]
    normalized=(sums[indices]-sums[starts])//(indices-starts)
    outputs=np.r_[0,normalized[:-1]]
    # The saturated pipeline delays input values by eight native clocks;
    # endpoint sampling adds half a native clock to the boxcar centroid.
    # Hardware outputs the normalization of the preceding audio interval.
    times=np.arange(WARM,WARM+SAMPLES,dtype=float)/OUTPUT
    def integral(t):
        count=np.floor(t/hold);fraction=t/hold-count
        total=np.imag((1-np.exp(1j*omega*count))/(1-np.exp(1j*omega)))
        return hold*(total+fraction*np.sin(omega*count))*10500
    delay=hold+8.5/CLOCK
    ideal=(integral(times-delay)-integral(times-1/OUTPUT-delay))*OUTPUT
    return pcm,outputs,outputs[WARM:WARM+SAMPLES].astype(float),ideal,hold

def analyze(actual,ideal,source_rate,frequency,hold):
    window=np.hanning(SAMPLES);reference=np.fft.rfft(ideal*window)
    residual=np.fft.rfft((actual-ideal)*window);carrier=max(abs(reference))
    spur=int(np.argmax(abs(residual[1:]))+1)
    # Evaluate each predicted source/hold image above -50dBc, using identical
    # finite-window projections for measured and analytic streams.
    q=(frequency/source_rate)%1;q=min(q,1-q);playback=1/hold
    raw=[]
    for image in range(-128,129):
        f=(image+q)*playback
        amplitude=abs(np.sinc(image+q)*np.sinc(f/OUTPUT))
        folded=abs((f+OUTPUT/2)%OUTPUT-OUTPUT/2)
        raw.append((folded,amplitude))
    maximum=max(a for _,a in raw);lines=[];time=np.arange(SAMPLES)/OUTPUT
    for f,a in raw:
        if a<maximum*10**(-50/20):continue
        basis=np.exp(-2j*np.pi*f*time)*window
        measured=abs(np.dot(actual,basis));expected=abs(np.dot(ideal,basis))
        lines.append({'frequency_hz':f,'error_db':float(20*np.log10(measured/expected))})
    worst_line=max(abs(line['error_db']) for line in lines)
    spur_db=float(20*np.log10(abs(residual[spur])/carrier))
    return {'source_rate':source_rate,'tone_hz':frequency,'samples':SAMPLES,'largest_unmodeled_line_dbc':spur_db,
        'unmodeled_frequency_hz':spur*OUTPUT/SAMPLES,'maximum_image_error_db':worst_line,'predicted_lines':lines,
        'status':'PASS' if spur_db<=-50 and worst_line<=.5 else 'FAIL'}

def main():
    global CLOCK
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-ua-spectral')
    p.add_argument('--confirm-rtl',action='store_true',help='Compare each tone with the real production mixer')
    p.add_argument('--clock-hz',type=int,default=CLOCK)
    args=p.parse_args();build=args.build_dir.resolve()
    CLOCK=args.clock_hz
    if not build.is_relative_to(ROOT/'build') or build==ROOT/'build':p.error('Use dedicated build child')
    build.mkdir(parents=True,exist_ok=True)
    source=ROOT/'hdl/core/c64/multitini_c64_ua_mix.sv';bench=ROOT/'hdl/sim/tb_c64_ua_spectral.sv'
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    before={f.relative_to(ROOT).as_posix():sha(f) for f in (Path(__file__),source,bench)}
    result={'status':'FAIL','source_sha256':before,'clock_hz':CLOCK,'cases':[],
        'limits':'One voice at volume32/centre, 8192 stereo output samples after warmup. Continuous analytic sample-hold then48kHz boxcar includes all hold images. Physical output and C64U audio quality untested.'}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']=os.environ.get('PATH',os.environ.get('Path',''))
    def run(label,tool,*arguments):
        r=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--','C:/Xilinx/2025.2/Vivado/bin/'+tool+'.bat',*map(str,arguments)],cwd=build,env=env,capture_output=True,text=True,timeout=300)
        output=r.stdout+r.stderr;(build/(label+'.log')).write_text(output)
        if r.returncode or re.search(r'ERROR:|Fatal|FATAL',output):raise RuntimeError(output[-10000:])
    try:
        if args.confirm_rtl:
            wrapper=build/'spectral_clock.sv'
            wrapper.write_text(f'module spectral_clock;tb_c64_ua_spectral #(.CORE_CLOCK_HZ({CLOCK})) test();endmodule\n')
            run('compile','xvlog','--nolog','--sv',source,bench,wrapper)
            run('elaborate','xelab','--nolog','--timescale','1ns/1ps','--snapshot','spectral_sim','spectral_clock')
        for fs,divider in ((22050,283),(44100,142),(48000,130)):
            for frequency in (1000,5000,10000,15000):
                pcm,expected,actual,ideal,hold=waveform(fs,divider,frequency)
                case=analyze(actual,ideal,fs,frequency,hold)
                if args.confirm_rtl:
                    (build/'tone_samples.txt').write_text(''.join(f'{int(value)&65535:04x}\n' for value in pcm))
                    (build/'tone_config.txt').write_text(str(divider)+'\n')
                    run(f'{fs}-{frequency}','xsim','--nolog','spectral_sim','--runall')
                    rows=list(csv.DictReader((build/'spectral.csv').read_text().splitlines()))
                    values=np.array([int(row['left']) for row in rows],dtype=np.int64)
                    if not np.array_equal(values[:len(expected)],expected):raise RuntimeError(f'Bit-exact RTL tone mismatch {fs}/{frequency}')
                    if any(row['left']!=row['right'] for row in rows):raise RuntimeError('Stereo centre mismatch')
                    case['rtl_bit_exact']=True
                result['cases'].append(case)
                print(f"{case['status']}: UA spectrum {fs}/{frequency} image_error={case['maximum_image_error_db']:.3f}dB residual={case['largest_unmodeled_line_dbc']:.3f}dBc",flush=True)
        if any(sha(ROOT/path)!=digest for path,digest in before.items()):raise RuntimeError('Sources changed during spectrum test')
        result.update(status='PASS' if all(c['status']=='PASS' for c in result['cases']) else 'FAIL',sources_unchanged=True)
    except Exception as e:result['error']=str(e);raise
    finally:(build/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result['status']=='PASS' else 1

if __name__=='__main__':raise SystemExit(main())
