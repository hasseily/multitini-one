#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove the transport fixture rejects broken Amiga cache/write-buffer variants.

Each mutant removes one ordering or handshake rule from the production memory
module. The unmodified fixture must fail on every mutant. This is simulation
evidence only; it says nothing about FPGA timing or board behaviour.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
MEMORY = "hdl/core/amiga/multitini_amiga_memory.sv"
OTHERS = ["third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/sim/amiga_cdc_model.sv",
          "hdl/common/multitini_async_mailbox.sv", "hdl/core/amiga/multitini_amiga_ide_block.sv",
          "hdl/core/amiga/multitini_amiga_diagnostics.sv", "hdl/core/amiga/multitini_amiga_host.sv",
          "hdl/core/amiga/multitini_amiga_loader.sv", "hdl/core/amiga/ports/ide.v",
          "hdl/core/amiga/ports/amiga_ram.sv", "hdl/sim/tb_amiga_transport.sv"]
# name: (exact production text, broken replacement)
MUTANTS = {
    "fill_ignores_buffered_write": ("state==IDLE && !wb_line_hit;", "state==IDLE;"),
    "write_accepted_during_fill": ("wb_space && !fill_busy;", "wb_space;"),
    "no_write_merge": ("wire merge=rs_valid && !wb_empty", "wire merge=1'b0 && !wb_empty"),
    "retire_word_read_hazard": ("tag_hit && !rs_word;", "tag_hit;"),
    "abandoned_fill_publishes": ("end else if(cpu_fill_live && cpu_req)begin", "end else begin"),
    "merge_into_launching_entry": ("&& !(launch_drain && wb_count==1);", ";"),
    "held_cpu_write_repeated": ("if(cpu_state!=2'b11)cpu_done<=0;", "cpu_done<=0;"),
    "cpu_key_on_retried_lookup": (
        "            if(hit)begin\n                if(request_owner==CHIP)",
        "            if(lookup && request_owner==CPU)cpu_key<=current_cpu_key;\n"
        "            if(hit)begin\n                if(request_owner==CHIP)"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/amiga-memory-mutation-tests")
    output = parser.parse_args().output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("Output must remain under the project build directory")
    output.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    tracked = [MEMORY] + OTHERS + ["scripts/test_amiga_memory_mutations.py"]
    def hashes():
        return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    before = hashes()
    source = (ROOT / MEMORY).read_text()
    results = {}
    for name, (old, new) in MUTANTS.items():
        if source.count(old) != 1:
            raise RuntimeError("Production text for mutant changed: " + name)
        variant = output / f"memory_{name}.sv"
        variant.write_text(source.replace(old, new), encoding="utf-8")
        executable = output / f"{name}.vvp"
        compile_run = subprocess.run(guard + [tool("iverilog"), "-g2012", "-s", "tb_amiga_transport",
                                              "-o", str(executable), *[str(ROOT / s) for s in OTHERS], str(variant)],
                                     cwd=ROOT, capture_output=True, text=True, timeout=180)
        if compile_run.returncode:
            raise RuntimeError(name + " failed to compile:\n" + compile_run.stdout + compile_run.stderr)
        run = subprocess.run(guard + [tool("vvp"), str(executable)], cwd=ROOT, capture_output=True,
                             text=True, timeout=600)
        log = run.stdout + run.stderr
        (output / f"{name}.log").write_text(log, encoding="utf-8")
        caught = "PASS AMIGA memory endian" not in log and "FATAL" in log
        fatal = next((line.strip() for line in log.splitlines() if "FATAL" in line), "")
        print(f"{name}: {'EXPECTED_FAILURE_REPRODUCED' if caught else 'ESCAPED'} {fatal}", flush=True)
        results[name] = {"caught": caught, "first_fatal": fatal}
    if before != hashes():
        raise RuntimeError("Sources changed during mutation qualification")
    status = "EXPECTED_FAILURE_REPRODUCED" if all(r["caught"] for r in results.values()) else "ESCAPED"
    (output / "validation.json").write_text(json.dumps({"status": status, "mutants": results,
        "source_sha256": before, "board_evidence": False}, indent=2) + "\n", encoding="utf-8")
    print(status)
    return 0 if status == "EXPECTED_FAILURE_REPRODUCED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
