# SPDX-License-Identifier: GPL-3.0-only
"""Prepare unchanged VICE CPU bodies with bounded host callback adapters."""
from pathlib import Path
import json
import shutil

from c64_vice_oracle import COMMIT, REFERENCE, digest

REF = REFERENCE / "upstream/vice/src"


def function(source: str, name: str) -> str:
    """Extract one complete, unchanged function from the pinned source."""
    position = source.index(name + "(")
    start = source.rfind("\n", 0, position) + 1
    opening = source.index("{", position)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def prepare(out: Path) -> dict:
    fragments: dict[str, dict] = {}

    def emit(name: str, reference: str, chunks: list[str]) -> None:
        source = (REF / reference).read_text()
        header = source[:source.index("*/") + 2] + "\n"
        (out / name).write_text(header + "\n".join(chunks))
        fragments[name] = {
            "reference": "vice/src/" + reference,
            "reference_sha256": digest(REF / reference),
            "sha256": digest(out / name),
            "source_line_ranges": [[source[:source.index(chunk)].count("\n") + 1,
                                    source[:source.index(chunk) + len(chunk)].count("\n") + 1]
                                   for chunk in chunks],
            "transform": "Original license header plus verbatim body spans; no body substitutions.",
        }

    source = (REF / "interrupt.h").read_text()
    emit("interrupt.inc", "interrupt.h", [source[source.index("/* Define the number"):
                                                source.index("/* Extern functions.")]])
    source = (REF / "mainc64cpu.c").read_text()
    emit("cpu_hooks.inc", "mainc64cpu.c", [function(source, name) for name in (
        "interrupt_delay", "maincpu_steal_cycles", "check_ba",
        "interrupt_check_nmi_delay", "interrupt_check_irq_delay")])
    source = (REF / "c64/c64cpusc.c").read_text()
    emit("fetch.inc", "c64/c64cpusc.c", [source[source.index("/* Opcode info updated"):
                                               source.index("static void check_and_run_alternate_cpu")]])
    start = source.index("#define CLK_INC()")
    emit("clock.inc", "c64/c64cpusc.c", [source[start:source.index("/* Skip cycle implementation */", start)]])
    for name in ("6510dtvcore.c", "6510core.h"):
        shutil.copyfile(REF / name, out / name)
        fragments[name] = {"reference": "vice/src/" + name,
                           "reference_sha256": digest(REF / name), "sha256": digest(out / name),
                           "transform": "Exact source bytes."}
    (out / "traps.h").write_text("#define TRAP_OPCODE 2\n")
    (out / "profiler.h").write_text("")
    receipt = {"reference_commit": COMMIT, "fragments": fragments,
               "host_headers": {name: digest(out / name) for name in ("traps.h", "profiler.h")},
               "limits": ["Host adapters provide flat RAM, pin changes, idle VIC clock callbacks and inactive debugger/profiler APIs.",
                          "No reference CPU, polling, opcode fetch or interrupt bodies are rewritten."]}
    (out / "oracle-source.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt
