#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain MAME CPU evidence and explicitly qualify reviewed differences against VICE 3.10."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import shutil

from c64_vice_oracle import COMMIT as VICE_COMMIT, REFERENCE as VICE_REFERENCE, verify_reference
from c64_vice_cpu_oracle import prepare

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_COMMIT = "122719e8d13e2fdb7dd33886455484207de59fc8"


def program() -> tuple[bytes, dict[str, int]]:
    memory = bytearray([0xEA] * 65536)
    labels: dict[str, int] = {}
    cursor = 0x0800

    def emit(name: str, code: str) -> None:
        nonlocal cursor
        labels[name] = cursor
        data = bytes.fromhex(code)
        memory[cursor:cursor + len(data)] = data
        cursor += len(data)

    emit("setup", "78 D8 A2 FF 9A A9 44")
    emit("store", "8D 00 20")
    emit("cli", "58")
    emit("after_cli", "EA A9 55")
    emit("sei", "78 EA")
    emit("brk", "00 EA")
    emit("after_brk", "EA 58 EA 08 78")
    emit("plp", "28 EA A9 01 4C FD 08")
    memory[0x08FD:0x08FF] = bytes.fromhex("D0 02")
    labels["branch"] = 0x08FD
    memory[0x0901:0x090A] = bytes.fromhex("EA A9 A5 8D 04 02 4C 07 09")
    # Preserve A, record entry count and stacked status, then execute real RTI.
    memory[0x0A00:0x0A0E] = bytes.fromhex("48 EE 00 22 BA BD 02 01 8D 02 22 68 40 EA")
    memory[0x0B00:0x0B0E] = bytes.fromhex("48 EE 01 22 BA BD 02 01 8D 03 22 68 40 EA")
    labels["rti"] = 0x0A0C
    memory[0x2200:0x2204] = bytes(4)
    memory[0xFFFA:0x10000] = bytes.fromhex("00 0B 00 08 00 0A")
    return bytes(memory), labels


def read_reference(path: Path) -> list[dict[str, object]]:
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    origin = int(rows[0]["cycle"]) if rows else 0
    return [{"tick": int(row["cycle"]) - origin, "address": row["address"].lower(),
             "rw": row["rw"], "data": row["data"].lower(), "sync": int(row["sync"])} for row in rows]


def compare(actual: list[dict[str, object]], expected: list[dict[str, object]]) -> dict[str, object]:
    differences = [{"index": index, "rtl": rtl, "mame": gold}
                   for index, (rtl, gold) in enumerate(zip(actual, expected)) if rtl != gold]
    return {"equal": actual == expected, "rtl_count": len(actual), "mame_count": len(expected),
            "first_differences": differences[:8]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-mame-expanded-cpu")
    parser.add_argument("--vivado-bin", type=Path,
                        default=Path(os.environ.get("XILINX_VIVADO", "C:/Xilinx/2025.2/Vivado")) / "bin")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if build == (ROOT / "build").resolve() or not build.is_relative_to((ROOT / "build").resolve()):
        parser.error("Build directory must remain inside this checkout's ignored build/")
    if build.exists():
        parser.error("Use a fresh build directory to preserve earlier evidence")
    build.mkdir(parents=True)
    receipt_path = build / "validation.json"
    receipt_path.unlink(missing_ok=True)
    environment = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    environment["PATH"] = "C:/msys64/ucrt64/bin" + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))
    guard = [sys.executable, ROOT / "scripts/run_native_tool.py", "--"] if os.name == "nt" else []
    suffix = ".bat" if os.name == "nt" else ""
    manifest_path = ROOT / "third_party/mame_reference/import_manifest.json"
    manifest = json.loads(manifest_path.read_text())
    vhdl = [ROOT / f"third_party/c64/upstream/rtl/t65/T65{part}.vhd" for part in ("_Pack", "_ALU", "_MCode")]
    vhdl += [ROOT / "hdl/core/c64/ports/T65.vhd", ROOT / "hdl/core/c64/multitini_c64_6510_port.vhd"]
    wrapper = ROOT / "hdl/core/c64/ports/cpu_6510.vhd"
    bench = ROOT / "hdl/sim/tb_c64_6510_boundaries.sv"
    vice_manifest = verify_reference()
    declarations_path = ROOT / "tests/c64_vice/mame_cpu_disagreements.json"
    declarations = json.loads(declarations_path.read_text())
    if declarations["mame_reference_commit"] != REFERENCE_COMMIT or declarations["vice_reference_commit"] != VICE_COMMIT:
        raise RuntimeError("Historical disagreement reference pins changed")
    tracked = [declarations_path, ROOT / "scripts/c64_vice_oracle.py", ROOT / "scripts/c64_vice_cpu_oracle.py",
               ROOT / "tests/c64_vice/cpu_harness.c", VICE_REFERENCE / "import_manifest.json",
               *[VICE_REFERENCE / "upstream" / entry["path"] for entry in vice_manifest["files"]],
               Path(__file__).resolve(), bench, wrapper, *vhdl, manifest_path,
               ROOT / "scripts/c64_mame_oracle.py", ROOT / "scripts/run_native_tool.py",
               ROOT / "tests/c64_mame/oracle_harness.cpp", ROOT / "tests/c64_mame/memory_harness.cpp",
               *[ROOT / "third_party/mame_reference/upstream" / entry["path"] for entry in manifest["files"]]]

    def hashes() -> dict[str, str]:
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}

    before = hashes()
    receipt_path.write_text(json.dumps({"status": "FAIL", "phase": "started", "source_sha256": before}, indent=2) + "\n")

    def run(name: str, command: list[object], *, allow_failure: bool = False) -> tuple[str, int]:
        argv = [item.as_posix() if isinstance(item, Path) else str(item) for item in command]
        result = subprocess.run(argv, cwd=build, env=environment, capture_output=True,
                                text=True, errors="replace", timeout=600,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        output = result.stdout + result.stderr
        (build / (name + ".log")).write_text(output, encoding="utf-8")
        if not allow_failure and (result.returncode or re.search(r"\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b", output, re.I)):
            raise RuntimeError(f"{name} failed ({result.returncode}):\n{output[-12000:]}")
        return output, result.returncode

    memory, labels = program()
    (build / "memory.bin").write_bytes(memory)
    (build / "memory.hex").write_text("\n".join(f"{byte:02x}" for byte in memory) + "\n")
    run("oracle-build", [sys.executable, ROOT / "scripts/c64_mame_oracle.py", "--build-dir", build / "oracle",
                         "--image", build / "memory.bin", "--trace", build / "baseline.csv"])
    oracle = build / "oracle" / ("mame6510_oracle.exe" if os.name == "nt" else "mame6510_oracle")
    baseline = read_reference(build / "baseline.csv")
    if not baseline:
        raise RuntimeError("Reference baseline is empty")
    fetches = {name: next(int(row["tick"]) for row in baseline
                         if row["address"] == f"{address:04x}" and row["sync"] == 1)
               for name, address in labels.items()}
    cases: list[dict[str, object]] = []

    def add(name: str, changes: dict[int, dict[str, int]], cadence: int = 4) -> None:
        state = {"ready": 1, "irq": 0, "nmi": 0}
        events = []
        control = []
        for tick in range(2048):
            if tick in changes:
                state.update(changes[tick])
                events.append((tick, state["ready"], state["irq"], state["nmi"]))
            control.append((state["ready"] << 2) | (state["irq"] << 1) | state["nmi"])
        index = len(cases)
        stem = f"{index:03d}-{name}"
        (build / (stem + ".events")).write_text("@trace\n" + "".join(" ".join(map(str, event)) + "\n" for event in events))
        (build / (stem + ".hex")).write_text("\n".join(f"{value:x}" for value in control) + "\n")
        cases.append({"id": index, "name": name, "stem": stem, "cadence": cadence, "events": events})

    add("brk-rti-baseline", {})
    add("brk-rti-enable-gaps", {}, 2)
    for boundary in ("cli", "sei", "plp", "branch"):
        for offset in (-1, 0, 1, 2):
            tick = fetches[boundary] + offset
            add(f"irq-{boundary}-{offset:+d}", {tick: {"irq": 1}, tick + 12: {"irq": 0}})
    for boundary in ("cli", "brk", "rti", "branch"):
        for offset in (0, 1, 2, 3, 4, 5):
            tick = fetches[boundary] + offset
            add(f"nmi-{boundary}-{offset:+d}", {tick: {"nmi": 1}, tick + 2: {"nmi": 0}}, 2)
    tick = fetches["after_cli"]
    add("simultaneous-irq-nmi", {tick: {"irq": 1, "nmi": 1}, tick + 12: {"irq": 0, "nmi": 0}})
    for boundary in ("cli", "sei", "brk", "rti", "plp", "branch"):
        tick = fetches[boundary] + 1
        add(f"rdy-{boundary}-read", {tick: {"ready": 0}, tick + 5: {"ready": 1}}, 2)
        add(f"irq-during-rdy-{boundary}", {tick: {"ready": 0}, tick + 1: {"irq": 1},
            tick + 5: {"ready": 1}, tick + 16: {"irq": 0}})
        add(f"nmi-during-rdy-{boundary}", {tick: {"ready": 0}, tick + 1: {"nmi": 1},
            tick + 3: {"nmi": 0}, tick + 5: {"ready": 1}}, 2)
    write_tick = next(int(row["tick"]) for row in baseline if row["rw"] == "W" and row["address"] == "2000")
    add("rdy-does-not-stop-store", {write_tick: {"ready": 0}, write_tick + 5: {"ready": 1}})
    stack_tick = next(int(row["tick"]) for row in baseline if row["rw"] == "W" and row["address"] == "01ff")
    add("rdy-does-not-stop-brk-stack", {stack_tick: {"ready": 0}, stack_tick + 6: {"ready": 1}}, 2)
    # Short pulses distinguish the actual polling cycle from a delayed pin
    # register, including deassertion before the following opcode fetch.
    for boundary in ("cli", "sei", "plp", "branch"):
        for offset in range(6):
            tick = fetches[boundary] + offset
            add(f"irq-pulse-{boundary}-{offset:+d}", {tick: {"irq": 1}, tick + 1: {"irq": 0}})
    # A second NMI during the selected vector is consumed when MAME resumes
    # after the high byte, before attempting the first handler fetch. Include
    # the immediately following clocks, where a fresh edge must survive.
    tick = fetches["brk"]
    for offset in (5, 6, 7, 8, 9):
        add(f"nmi-double-brk-vector-{offset:+d}", {
            tick + 2: {"nmi": 1}, tick + 3: {"nmi": 0},
            tick + offset: {"nmi": 1}, tick + offset + 1: {"nmi": 0}})
    add("nmi-double-rdy-brk-vector-high", {
        tick + 2: {"nmi": 1}, tick + 3: {"nmi": 0},
        tick + 6: {"ready": 0}, tick + 7: {"nmi": 1},
        tick + 8: {"nmi": 0}, tick + 11: {"ready": 1}})
    add("nmi-double-rdy-handler-fetch", {
        tick + 2: {"nmi": 1}, tick + 3: {"nmi": 0},
        tick + 7: {"ready": 0}, tick + 8: {"nmi": 1},
        tick + 9: {"nmi": 0}, tick + 12: {"ready": 1}})
    # Repeat every event schedule with a different fabric enable cadence.
    # Interrupt acceptance must depend on CPU clock attempts, not CE spacing.
    for case in list(cases):
        cadence = 2 if case["cadence"] == 4 else 4
        changes = {tick: {"ready": ready, "irq": irq, "nmi": nmi}
                   for tick, ready, irq, nmi in case["events"]}
        add(str(case["name"]) + f"-ce{cadence}", changes, cadence)
    (build / "cases.txt").write_text(str(len(cases)) + "\n" + "".join(
        f"{case['id']} {case['cadence']} memory.hex {case['stem']}.hex\n" for case in cases))
    (build / "case_definitions.json").write_text(json.dumps({"fetch_ticks": fetches, "cases": cases}, indent=2) + "\n")

    for case in cases:
        stem = str(case["stem"])
        _, code = run(stem + "-oracle", [*guard, oracle, build / "memory.bin", build / (stem + ".csv"),
                     "0800", "0204", "a5", "a0", "2048", build / (stem + ".events")], allow_failure=True)
        case["oracle_returncode"] = code

    vice_sources = prepare(build)
    shutil.copyfile(ROOT / "tests/c64_vice/cpu_harness.c", build / "cpu_harness.c")
    vice_oracle = build / ("vice6510_oracle.exe" if os.name == "nt" else "vice6510_oracle")
    compiler = Path("C:/msys64/ucrt64/bin/gcc.exe") if os.name == "nt" else Path("gcc")
    run("vice-oracle-build", [*guard, compiler, "-std=c11", "-O1", build / "cpu_harness.c", "-o", vice_oracle])
    for case in cases:
        stem = str(case["stem"])
        run(stem + "-vice-oracle", [*guard, vice_oracle, build / "memory.bin", build / (stem + ".hex"), build / (stem + "-vice.csv")])

    source = wrapper.read_text()
    declaration = "\t\tdoIO    : out unsigned(7 downto 0)"
    ports = ";\n\t\ttest_data : out unsigned(7 downto 0);\n\t\ttest_sync : out std_logic"
    connection = "\t\tSync => test_sync,\n"
    assignment = "test_data <= unsigned(localDi);\n"
    if source.count(declaration) != 1 or source.count("\t\tR_W_n   => localWe,\n") != 1:
        raise RuntimeError("Observation-only wrapper boundary changed")
    observed = source.replace(declaration, declaration + ports).replace(
        "\t\tR_W_n   => localWe,\n", "\t\tR_W_n   => localWe,\n" + connection).replace(
        "end architecture;", assignment + "end architecture;")
    if observed.replace(ports, "").replace(connection, "").replace(assignment, "") != source:
        raise RuntimeError("Observation instrumentation changed production behavior")
    observed_path = build / "cpu_6510_observed.vhd"
    observed_path.write_text(observed)
    top = "tb_c64_6510_boundaries"
    run("xvlog", [*guard, args.vivado_bin / ("xvlog" + suffix), "--nolog", "--sv", bench])
    run("xvhdl", [*guard, args.vivado_bin / ("xvhdl" + suffix), "--nolog", "--2008", *vhdl, observed_path])
    run("xelab", [*guard, args.vivado_bin / ("xelab" + suffix), "--nolog", "--timescale", "1ns/1ps", "--snapshot", top + "_sim", top])
    output, _ = run("simulate", [*guard, args.vivado_bin / ("xsim" + suffix), "--nolog", top + "_sim", "--runall"])
    traces: dict[int, list[dict[str, object]]] = {int(case["id"]): [] for case in cases}
    for match in re.finditer(r"^BOUNDARY_BUS,(\d+),(\d+),(\d+),([0-9a-f]+),([RW]),([0-9a-f]+),([01])$", output, re.M | re.I):
        case_id, sequence, tick, address, rw, data, sync = match.groups()
        rows = traces[int(case_id)]
        if int(sequence) != len(rows):
            raise RuntimeError("Non-contiguous RTL trace")
        rows.append({"tick": int(tick), "address": address.lower(), "rw": rw, "data": data.lower(), "sync": int(sync)})
    summaries = {int(match[0]): [int(value) for value in match[1:]] for match in re.findall(
        r"^BOUNDARY_SUMMARY,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)$", output, re.M)}
    failures = []
    disagreements = []
    exact_matches = 0
    for case in cases:
        index = int(case["id"])
        expected = read_reference(build / (str(case["stem"]) + ".csv"))
        actual = traces[index]
        result = compare(actual, expected)
        summary = summaries.get(index, [])
        vice = read_reference(build / (str(case["stem"]) + "-vice.csv"))
        qualified = actual == vice and case["oracle_returncode"] == 0 and bool(summary) and summary[0] == 1
        name = str(case["name"])
        declaration = declarations["disagreements"].get(name)
        if declaration:
            signature = hashlib.sha256(json.dumps(expected, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            qualified = qualified and not result["equal"] and signature == declaration["mame_trace_sha256"]
            disagreements.append(name)
        else:
            qualified = qualified and result["equal"]
            if result["equal"]: exact_matches += 1
        case.update(result, rtl_summary=summary, vice_equal=actual == vice,
                    historical_disagreement=declaration,
                    status="PASS" if qualified else "FAIL")
        (build / (str(case["stem"]) + "-rtl.json")).write_text(json.dumps(actual, indent=2) + "\n")
        case["artifact_sha256"] = {extension: hashlib.sha256(
            (build / (str(case["stem"]) + extension)).read_bytes()).hexdigest()
            for extension in (".csv", "-vice.csv", ".events", ".hex", "-rtl.json")}
        if case["status"] != "PASS":
            failures.append(case["name"])
            print(f"FAIL: {case['name']} {json.dumps(result)}", flush=True)
    corrupted = [dict(row) for row in traces[0]]
    if not compare(corrupted, read_reference(build / (str(cases[0]["stem"]) + ".csv")))["equal"]:
        raise RuntimeError("Unmodified BRK/RTI baseline failed before negative control")
    vector_index = next(index for index, row in enumerate(corrupted) if row["address"] == "fffe")
    corrupted[vector_index]["address"] = "fffa"
    negative = compare(corrupted, read_reference(build / (str(cases[0]["stem"]) + ".csv")))
    if negative["equal"]:
        raise RuntimeError("Deliberate BRK vector corruption was not rejected")
    if (set(disagreements) != set(declarations["disagreements"]) or
            len(cases) != declarations["case_count"] or exact_matches != declarations["exact_mame_matches"]):
        failures.append("historical-declaration-set-changed")
    after = hashes()
    stable = before == after
    receipt = {"schema": 1, "status": "PASS" if not failures and stable else "FAIL",
               "reference_commit": REFERENCE_COMMIT, "source_sha256": before,
               "qualification": "Historical MAME with exact declared disagreements verified against fresh VICE execution",
               "raw_mame_status": "FAIL" if disagreements else "PASS",
               "exact_mame_matches": exact_matches, "expected_disagreement_count": len(disagreements),
               "vice_reference_commit": VICE_COMMIT, "vice_oracle_sources": vice_sources,
               "memory_sha256": hashlib.sha256(memory).hexdigest(),
               "sources_unchanged": stable, "case_count": len(cases), "passed": len(cases) - len(failures),
               "failed": failures, "cases": cases, "negative_control": {"status": "PASS", "corruption":
                   "BRK vector low read address FFFE changed to FFFA", "comparison": negative},
               "observation_wrapper_sha256": hashlib.sha256(observed_path.read_bytes()).hexdigest(),
               "limits": ["MAME reference traces remain unchanged; status PASS qualifies exact declared disagreements against fresh VICE, not full MAME conformance.",
                   "Stimulus uses CPU clock attempts relative to first opcode fetch; CE gaps are not CPU clocks.",
                   "All accepted accesses compare address, direction, data, SYNC and relative CPU tick.",
                   "Directed boundary matrix is not exhaustive NMOS opcode/interrupt conformance or board proof."]}
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"{receipt['status']}: historical CPU qualification {receipt['passed']}/{len(cases)}, MAME exact={exact_matches}, VICE disagreements={len(disagreements)}; negative control rejected; sources_unchanged={stable}")
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, StopIteration) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
