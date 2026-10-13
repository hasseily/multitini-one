#!/usr/bin/env python3
"""Run the exact pinned Vitis FatFs f_mount code without disk or hardware I/O.

The generated native fixture is kept under build/. Only the path parser and
the NULL-volume rejection before disk access are stubbed. The f_mount body
is extracted verbatim, including its unregister-before-forced-mount order.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/vendor-fatfs-unmount-test"
VENDOR_RELATIVE = "data/embeddedsw/lib/sw_services/xilffs_v5_5/src/ff.c"
VENDOR_SHA256 = "3f15ee04c86eddef69c8010ac822491a4430bdb9686ffd99370a5175e484b934"
HEADER_SHA256 = "d38ab7a6c51f7e6ce714bcf651d52787a11779a395401cb8bffb1c29f2365595"


def extract_mount(source: str) -> str:
    match = re.search(r"(?m)^FRESULT f_mount\s*\([^;]*?\)\s*\{", source)
    if match is None:
        raise AssertionError("Pinned vendor source has no f_mount definition")
    position, depth = match.end(), 1
    while depth:
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    return source[match.start():position] + "\n"


PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef char TCHAR;
typedef uint8_t BYTE;
typedef enum { FR_OK=0, FR_DISK_ERR=1, FR_INVALID_DRIVE=11,
               FR_NOT_ENABLED=12 } FRESULT;
/* These are the two FATFS fields used by the exact f_mount implementation.
 * The selected standalone BSP disables sharing locks and reentrancy. */
typedef struct { BYTE fs_type, pdrv; } FATFS;
#define FF_FS_LOCK 0
#define FF_FS_REENTRANT 0
#define LD2PD(volume) (volume)
#define LEAVE_FF(fs,res) return (res)
static FATFS *FatFs[1];
static unsigned forced_mounts;
static int get_ldnumber(const TCHAR **path)
{
    if(strncmp(*path,"0:/",3)) return -1;
    *path += 2;
    return 0;
}
static FRESULT mount_volume(const TCHAR **path, FATFS **fs, BYTE mode)
{
    assert(mode==0);
    int volume=get_ldnumber(path);
    assert(volume==0);
    *fs=FatFs[volume];
    ++forced_mounts;
    /* Match the pinned mount_volume's first failure boundary: no registered
     * work area means FR_NOT_ENABLED before any disk call is reached. */
    assert(*fs==NULL);
    return FR_NOT_ENABLED;
}
'''

CASES = r'''
int main(void)
{
    FATFS fs={3,0xff};
    assert(f_mount(&fs,"0:/",0)==FR_OK);
    assert(FatFs[0]==&fs && !fs.fs_type && !fs.pdrv && !forced_mounts);
    fs.fs_type=3;
    assert(f_mount(NULL,"9:/",0)==FR_INVALID_DRIVE);
    assert(FatFs[0]==&fs && fs.fs_type==3 && !forced_mounts);
    /* The rejected opt=1 operation has already detached the work area. */
    assert(f_mount(NULL,"0:/",1)==FR_NOT_ENABLED);
    assert(FatFs[0]==NULL && !fs.fs_type && forced_mounts==1);
    /* Correct unmount: detach and return success without mounting a disk. */
    assert(f_mount(&fs,"0:/",0)==FR_OK);
    assert(FatFs[0]==&fs && !fs.fs_type && forced_mounts==1);
    fs.fs_type=3;
    assert(f_mount(NULL,"0:/",0)==FR_OK);
    assert(FatFs[0]==NULL && !fs.fs_type && forced_mounts==1);
    assert(f_mount(NULL,"0:/",0)==FR_OK);
    assert(FatFs[0]==NULL && forced_mounts==1);
    puts("PASS: exact vendor f_mount(NULL,opt=1) detaches then returns FR_NOT_ENABLED=12");
    puts("PASS: NULL opt=0 detaches successfully; non-NULL opt=0 registers without disk I/O; invalid volume leaves registration intact");
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor-source", type=Path,
        default=Path(os.environ.get("XILINX_VITIS", "C:/Xilinx/2025.2/Vitis")) / VENDOR_RELATIVE)
    args = parser.parse_args()
    vendor = args.vendor_source.resolve()
    payload = vendor.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    if digest != VENDOR_SHA256:
        raise AssertionError(f"Unexpected vendor FatFs source SHA256: {digest}; expected {VENDOR_SHA256}")
    header = vendor.parent / "include/ff.h"
    if hashlib.sha256(header.read_bytes()).hexdigest() != HEADER_SHA256:
        raise AssertionError("Vendor ff.h does not match the selected firmware BSP header")
    text = payload.decode("utf-8")
    # Anchor the stub to the actual NULL-work-area check, before disk access.
    rejection = re.search(r"fs\s*=\s*FatFs\[vol\];.*?if\s*\(!fs\)\s*\{\s*return FR_NOT_ENABLED;", text, re.S)
    if rejection is None:
        raise AssertionError("Vendor mount_volume NULL-work-area rejection changed")
    body = extract_mount(text)
    OUT.mkdir(parents=True, exist_ok=True)
    fixture = OUT / "vendor_f_mount.c"
    fixture.write_text(PREAMBLE + body + CASES, encoding="utf-8")
    metadata = {
        "vendor_source": str(vendor), "vendor_name": "Vitis 2025.2 xilffs_v5_5 / FatFs R0.15a",
        "vendor_source_sha256": digest, "vendor_header_sha256": HEADER_SHA256,
        "extracted_f_mount_sha256": hashlib.sha256(body.encode()).hexdigest(),
        "disk_io": False, "hardware_access": False,
    }
    (OUT / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None and Path("C:/msys64/ucrt64/bin/gcc.exe").exists():
        compiler = "C:/msys64/ucrt64/bin/gcc.exe"
    if compiler is None:
        raise RuntimeError("A native C compiler is required")
    executable = OUT / ("vendor_f_mount.exe" if os.name == "nt" else "vendor_f_mount")
    env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ.get("PATH", ""))
    print(f"Vendor source: {vendor}", flush=True)
    print(f"SHA256: {digest}", flush=True)
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O0",
                    str(fixture), "-o", str(executable)], check=True, env=env)
    subprocess.run([str(executable)], check=True, env=env)


if __name__ == "__main__":
    main()
