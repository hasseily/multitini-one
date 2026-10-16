#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assemble the source-built IIgs SmartPort ROM into an ignored asset directory."""
from __future__ import annotations
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"hdl/core/apple_iigs/roms/smartport.a65"
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def build(directory,acme):
    directory=Path(directory).resolve()
    if not directory.is_relative_to(ROOT/"build"):
        raise ValueError("Generated SmartPort ROM assets must stay under the repository build directory")
    directory.mkdir(parents=True,exist_ok=True)
    source=SOURCE.resolve()
    subprocess.run([str(acme),str(source)],cwd=directory,check=True)
    output=directory/"hdd.rom";data=output.read_bytes()
    if len(data)!=2304:raise ValueError("SmartPort ROM must contain256slot+2048expansion bytes")
    if data[1:8:2]!=bytes([0x20,0,3,0]) or data[255]!=10 or data[251]!=0x80:
        raise ValueError("SmartPort signature or legacy entry changed")
    (directory/"smartport_c700.mem").write_text("".join(f"{x:02X}\n" for x in data[:256]),encoding="ascii")
    (directory/"smartport_c800.mem").write_text("".join(f"{x:02X}\n" for x in data[256:]),encoding="ascii")
    record={"source":source.relative_to(ROOT).as_posix(),"source_sha256":sha(source),
        "license":"GPL-3.0-only","size":2304,"sha256":sha(output),"assembler":str(acme),
        "output":"hdd.rom","source_inputs":{source.relative_to(ROOT).as_posix():sha(source),
            Path(__file__).relative_to(ROOT).as_posix():sha(__file__)},
        "layout":{"slot":"C700-C7FF","expansion":"C800-CFFF","load_kind":"0x49","load_window":"0x2000-0x28FF"},
        "references":{"Appletini":"7b972bfda432657444d55a3eaaccf4b1d8e06699",
            "AppleWin_IIgs":"9adbba8622f378253765b7d438b6cbcc4d03fc57"},
        "notice":"Original Multitini implementation; no Apple SSC wrappers or Apple system ROM payloads."}
    (directory/"provenance.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
    return output
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-dir",type=Path,default=ROOT/"build/apple-iigs-assets")
    parser.add_argument("--acme",default="C:/Users/hasse/tools/acme/acme.exe" if sys.platform=="win32" else "acme")
    args=parser.parse_args();print(build(args.asset_dir,args.acme));return 0
if __name__=="__main__":raise SystemExit(main())
