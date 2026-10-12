#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate runtime display timing, compact menus, clock faults, and shell wiring."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    build = ROOT / "build/display-mode-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    logs = []
    tracked = {}
    def run(command, name, cwd=build):
        native = list(map(str, command))
        if os.name == "nt" and Path(native[0]).name == "verilator.exe" and not Path(native[0]).is_file():
            # MSYS supplies Verilator as a Perl launcher, not a native .exe.
            def posix(value):
                value = str(value).replace("\\", "/")
                return "/" + value[0].lower() + value[2:] if len(value)>2 and value[1]==":" else value
            shell = "cd " + shlex.quote(posix(cwd)) + " && export PATH=/ucrt64/bin:/usr/bin:$PATH; exec verilator "
            native = ["C:/msys64/usr/bin/bash.exe", "-lc", shell + shlex.join([posix(a) for a in native[1:]])]
        result = subprocess.run(guard + native, cwd=cwd,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=180)
        (build / (name + ".log")).write_text(result.stdout)
        logs.append({"name": name, "returncode": result.returncode})
        if result.returncode:
            print(result.stdout)
            raise RuntimeError(name + " failed")
        return result.stdout
    def source_list(paths):
        for p in paths:
            tracked.setdefault(p,hashlib.sha256((ROOT/p).read_bytes()).hexdigest())
        return [ROOT / p for p in paths]
    source_list(["scripts/test_display_modes.py","scripts/create_project.tcl","hdl/hdl_sources.txt"])
    controller = source_list(["hdl/common/multitini_display_pkg.sv",
        "hdl/common/multitini_display_control.sv", "hdl/sim/tb_display_control.sv"])
    run([tool("iverilog"), "-g2012", "-s", "tb_display_control", "-o", build/"controller.vvp"]+controller,"controller-compile")
    output=run([tool("vvp"),build/"controller.vvp"],"controller-run")
    if "VIDEO MODE CONTROL PASS" not in output: raise RuntimeError("controller success marker missing")
    sources=source_list(["hdl/generated/multitini_regs_pkg.sv", "hdl/common/multitini_display_pkg.sv",
        "hdl/common/multitini_display_geometry.sv", "hdl/sim/xilinx_primitives.sv",
        "hdl/common/multitini_async_mailbox.sv", "hdl/common/multitini_text_overlay.sv",
        "hdl/board/multitini_video_test.sv", "hdl/sim/tb_display_output.sv"])
    shutil.copyfile(ROOT/"hdl/common/multitini_font7x8.mem",build/"multitini_font7x8.mem")
    source_list(["hdl/common/multitini_font7x8.mem"])
    run([tool("verilator"),"--binary","--timing","--top-module","tb_display_output",
         "-Wno-fatal","--Mdir",build/"output","--build-jobs","8"]+sources,"output-compile")
    executable=build/"output"/("Vtb_display_output.exe" if os.name=="nt" else "Vtb_display_output")
    output=run([executable],"output-run")
    if output.count("PASS display ")!=6: raise RuntimeError("missing display mode coverage")
    print("\n".join(line for line in output.splitlines() if line.startswith("PASS ")))
    text_sources=source_list(["hdl/generated/multitini_regs_pkg.sv",
        "hdl/sim/xilinx_primitives.sv", "hdl/common/multitini_async_mailbox.sv",
        "hdl/common/multitini_text_overlay.sv", "hdl/sim/tb_text_overlay.sv"])
    run([tool("verilator"),"--binary","--timing","--top-module","tb_text_overlay",
         "-GDYNAMIC_DISPLAY=1","-Wno-fatal","--Mdir",build/"text","--build-jobs","8"]+
        text_sources,"text-compile")
    text_executable=build/"text"/("Vtb_text_overlay"+(".exe" if os.name=="nt" else ""))
    for mode in range(6):
        output=run([text_executable,f"+MODE={mode}"],f"text-mode-{mode}")
        marker=f"PASS: mode={mode} dynamic=1; 5 full overlay rasters;"
        if marker not in output: raise RuntimeError("text glyph/geometry coverage missing")
        print("\n".join(line for line in output.splitlines() if line.startswith(marker)))
    for machine in ("c64","gameboy","vectrex"):
        top=f"tb_{machine}_video_bridge"
        sources=source_list(["hdl/common/multitini_display_pkg.sv",
            "hdl/common/multitini_display_geometry.sv", "hdl/sim/xilinx_primitives.sv",
            "third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/board/multitini_video_test.sv",
            f"hdl/board/multitini_{machine}_video_bridge.sv", f"hdl/sim/{top}.sv"])
        if machine=="vectrex":
            sources+=source_list(["hdl/core/vectrex/multitini_vectrex_compositor.sv"])
            run([tool("verilator"),"--binary","--timing","--top-module",top,"-GDYNAMIC_DISPLAY=1",
                 "-Wno-fatal","--Mdir",build/machine,"--build-jobs","8"]+sources,machine+"-compile")
            output=run([build/machine/("V"+top+(".exe" if os.name=="nt" else ""))],machine+"-run")
        else:
            run([tool("iverilog"),"-g2012","-s",top,"-P",top+".DYNAMIC_DISPLAY=1",
                 "-o",build/(machine+".vvp")]+sources,machine+"-compile")
            output=run([tool("vvp"),build/(machine+".vvp")],machine+"-run")
        if "PASS" not in output:raise RuntimeError(machine+" bridge success marker missing")
        print("\n".join(line for line in output.splitlines() if "PASS" in line))
    border=source_list(["hdl/board/multitini_border_overlay.sv","hdl/sim/tb_border_overlay.sv"])
    run([tool("verilator"),"--binary","--timing","--top-module","tb_border_overlay",
         "-GDYNAMIC_DISPLAY=1","-Wno-fatal","--Mdir",build/"border","--build-jobs","8"]+border,"border-compile")
    output=run([build/"border"/("Vtb_border_overlay"+(".exe" if os.name=="nt" else ""))],"border-run")
    if "PASS" not in output:raise RuntimeError("border success marker missing")
    print("\n".join(line for line in output.splitlines() if "PASS" in line))
    effects=source_list(["hdl/sim/xilinx_primitives.sv","hdl/common/multitini_async_mailbox.sv",
        "hdl/common/multitini_video_effects.sv","hdl/sim/tb_video_effects.sv"])
    for num,den in ((1,1),(2,1),(3,1),(4,1),(5,1),(6,1),(3,2)):
        name=f"effects-{num}-{den}"
        run([tool("iverilog"),"-g2012","-s","tb_video_effects","-P","tb_video_effects.DYNAMIC_DISPLAY=1",
             "-P",f"tb_video_effects.SCALE_NUM={num}","-P",f"tb_video_effects.SCALE_DEN={den}",
             "-o",build/(name+".vvp")]+effects,name+"-compile")
        output=run([tool("vvp"),build/(name+".vvp")],name+"-run")
        if "PASS video effects:" not in output:raise RuntimeError(name+" success marker missing")
        print("\n".join(line for line in output.splitlines() if "PASS" in line))
    # Read the production manifest so new common modules cannot silently become
    # black boxes in the older shell lint scripts. Machine cores and generated
    # PS wrapper remain vendor-build responsibilities here.
    common=[]; section="common"
    for line in (ROOT/"hdl/hdl_sources.txt").read_text().splitlines():
        line=line.strip()
        if not line or line.startswith("#"):continue
        if line.startswith("["):section=line[1:-1];continue
        path=(ROOT/"hdl"/line.split("|")[0].strip()).resolve()
        if section=="common" and path.suffix in (".sv",".v"):
            common.append(path);source_list([path.relative_to(ROOT).as_posix()])
    common += source_list(["hdl/board/multitini_c64_video_bridge.sv",
        "hdl/board/multitini_gameboy_video_bridge.sv", "hdl/board/multitini_vectrex_video_bridge.sv",
        "hdl/core/genesis/multitini_genesis_video_normalizer.sv",
        "hdl/board/multitini_genesis_video_bridge.sv",
        "hdl/core/vectrex/multitini_vectrex_compositor.sv", "hdl/sim/xilinx_primitives.sv",
        "hdl/sim/video_fifo_model.sv"])
    for machine in range(5):
        run([tool("iverilog"),"-g2012","-i","-s","multitini_one_top","-P",
             f"multitini_one_top.ACTIVE_MACHINE={machine}","-o",build/f"shell-{machine}.vvp"]+common,
             f"shell-{machine}-elaborate")
    after={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked}
    if tracked!=after:raise RuntimeError("Display validation inputs changed during the run")
    (build/"validation.json").write_text(json.dumps({"passed":True,"logs":logs,
        "source_sha256":tracked,"source_sha256_after":after},indent=2)+"\n")
    print("PASS clock faults and all five common shell configurations")
    return 0
if __name__=="__main__":
    raise SystemExit(main())
