#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args();b=a.build_dir.resolve()
    if b.exists() or not b.is_relative_to(ROOT/'build'):raise ValueError('fresh build subdirectory required')
    b.mkdir(parents=True);(b/'temp').mkdir()
    common=['firmware/common/net_config.c','firmware/common/net_w5100s.c','firmware/common/net_protocol.c']
    targets=['firmware/common/net_uci.c','firmware/common/http_json.c','firmware/common/http_uci.c','firmware/common/c64_uci.c']
    files=common+targets+['firmware/platform/xilinx/net_service.c','tests/host/test_net.c','tests/host/test_net_uci_http.c','tests/host/net_model.h','tests/host/test_net_service.c','tests/host/test_c64_uci.c','tests/host/test_sd_dma_quiesce.c','firmware/platform/xilinx/sd_quiesce_diskio.inc']
    receipt={'status':'RUNNING','source_sha256':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files+['firmware/include/multitini_net.h','firmware/include/multitini_net_uci.h','firmware/include/multitini_http.h','firmware/include/multitini_c64_uci.h','firmware/include/multitini_core_link.h','firmware/include/multitini_regs.h','firmware/common/net_internal.h','firmware/common/http_internal.h','firmware/platform/xilinx/net_service.h','firmware/platform/xilinx/c64_uci_geos.c','firmware/platform/xilinx/c64_uci_geos.h','firmware/platform/xilinx/c64_uci_internal.h','firmware/platform/xilinx/sd_storage.h','tests/host/test_c64_uci_geos.c','scripts/run_native_tool.py','scripts/test_network.py']},'limits':'Register-accurate synthetic W5100S model; no physical network/board claim.'}
    reference=ROOT/'third_party/ultimate_firmware_reference/software/io'
    receipt['reference_commit']='d43546fe25b26cb332f43f00219d33153dfe44f8'
    receipt['reference_sha256']={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in [reference/'network/network_target.cc',reference/'network/network_target.h',
                  reference/'command_interface/http_target.cc',reference/'command_interface/http_target.h',
                  reference/'command_interface/test_http_target.cc']}
    env={k:v for k,v in os.environ.items() if k.lower() not in ('path','temp','tmp')};env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''));env['TEMP']=env['TMP']=str(b/'temp')
    def run(name,args):
        cmd=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*map(str,args)]
        r=subprocess.run(cmd,cwd=b,env=env,capture_output=True,text=True,timeout=90);(b/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stdout+r.stderr)
        return r.stdout
    try:
        receipt['checks']=[]
        for name,test,src in [('base','tests/host/test_net.c',common),('engine','tests/host/test_c64_uci.c',['firmware/common/c64_uci.c']),('uci-http','tests/host/test_net_uci_http.c',common+targets),('cid','tests/host/test_sd_dma_quiesce.c',[]),('geos','tests/host/test_c64_uci_geos.c',['firmware/platform/xilinx/c64_uci_geos.c','firmware/common/c64_uci.c']),('platform','tests/host/test_net_service.c',common+targets+['firmware/platform/xilinx/net_service.c'])]:
            run(name+'-compile',['C:/msys64/ucrt64/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'firmware/include'),'-I'+str(ROOT/'firmware/platform/xilinx'),'-I'+str(ROOT/'tests/xilinx_stubs'),*[ROOT/f for f in src+[test]],'-o',b/(name+'.exe')])
            result=run(name,[b/(name+'.exe')]);assert 'PASS ' in result;receipt['checks']+=result.splitlines();print(result)
        receipt['negative_controls']=[]
        for name,file,before,after,test,src in [
            ('ring','firmware/common/net_w5100s.c','& 0x7ff','& 0x3ff','tests/host/test_net.c',common),
            ('ttl-zero','firmware/common/net_protocol.c','if (!ttl)\n        return;','if (!ttl) ttl=60;','tests/host/test_net.c',common),
            ('https','firmware/common/http_uci.c','if (header->secure)','if (false)','tests/host/test_net_uci_http.c',common+targets)]:
            original=(ROOT/file).read_text();assert before in original
            mutant=b/(name+'-mutant.c');mutant.write_text(original.replace(before,after))
            units=[mutant if f==file else ROOT/f for f in src]+[ROOT/test]
            run(name+'-negative-compile',['C:/msys64/ucrt64/bin/gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'firmware/include'),'-I'+str(ROOT/'firmware/common'),*units,'-o',b/(name+'-negative.exe')])
            rejected=False
            try:run(name+'-negative',[b/(name+'-negative.exe')])
            except RuntimeError:
                rejected='Assertion' in (b/(name+'-negative.log')).read_text()
            if not rejected:raise RuntimeError(name+' negative control not rejected by an assertion')
            receipt['negative_controls'].append({'name':name,'rejected':True,'mutant_sha256':hashlib.sha256(mutant.read_bytes()).hexdigest()})
        receipt['sources_unchanged']=all(hashlib.sha256((ROOT/f).read_bytes()).hexdigest()==h
            for f,h in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('Sources changed during qualification')
        receipt['status']='PASS'
    except Exception as e:receipt.update(status='FAIL',error=str(e));raise
    finally:
        receipt['artifact_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in b.glob('*.log')}
        (b/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
