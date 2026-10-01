#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the C PNG encoder with an independent zlib decoder and PNG CRCs."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper",required=True,type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    font=[int(word,16) for word in (root/"hdl/common/multitini_font7x8.mem").read_text().split()]
    copied_font=[int(word,16) for word in re.findall(r"UINT64_C\(0x([0-9A-Fa-f]+)\)",
        (root/"firmware/common/screenshot_font.h").read_text())]
    assert len(font)==96 and copied_font==font, "Screenshot font must match the FPGA font exactly"
    with tempfile.TemporaryDirectory(prefix="multitini-png-") as directory:
        path=Path(directory)/"capture.png"
        subprocess.run([str(args.helper.resolve()),str(path)],check=True)
        data=path.read_bytes()
    assert data[:8]==b"\x89PNG\r\n\x1a\n"
    pos=8;payload=bytearray();kinds=[]
    while pos<len(data):
        size=struct.unpack_from(">I",data,pos)[0]
        kind=data[pos+4:pos+8];chunk=data[pos+8:pos+8+size]
        checksum=struct.unpack_from(">I",data,pos+8+size)[0]
        assert zlib.crc32(kind+chunk)==checksum
        kinds.append(kind)
        if kind==b"IHDR": assert struct.unpack(">IIBBBBB",chunk)==(1920,3,8,2,0,0,0)
        elif kind==b"IDAT":payload.extend(chunk)
        elif kind==b"IEND":assert size==0
        else:raise AssertionError(kind)
        pos+=12+size
    assert pos==len(data) and kinds==[b"IHDR",b"IDAT",b"IDAT",b"IDAT",b"IEND"]
    raw=zlib.decompress(payload)
    assert len(raw)==3*(1+1920*3)
    for y in range(3):
        line=raw[y*(1+1920*3):(y+1)*(1+1920*3)]
        assert line[0]==0
        for x in range(1920):
            value=(x*37+y*1009)&65535
            r,g,b=value>>11,(value>>5)&63,value&31
            assert line[1+x*3:4+x*3]==bytes(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)))
    print("PASS exact FPGA font, PNG chunks/CRC32, zlib/Adler32, dimensions and every RGB pixel")
    return 0
if __name__=="__main__":raise SystemExit(main())
