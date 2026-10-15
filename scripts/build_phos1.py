#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build selected images or reuse explicit, validated FPGA artifacts."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD_ID = "50484F31"
DEFAULT_OUTPUT = ROOT / "build/phos1/hardware"
MATRIX = {"supervisor": ("SUPERVISOR", False, False),
          "c64-pal": ("C64", False, False), "gbc": ("GBC", True, False),
          "vectrex": ("VECTREX", False, False),
          "genesis-ntsc": ("GENESIS", False, False),
          "genesis-pal": ("GENESIS", False, True)}


def selection_plan(images, reuse, build_id, output, reuse_from=None):
    """Validate every selection before creating a directory or invoking tools."""
    import core_artifacts
    selections = {}
    for name in images:
        machine, psram, pal = MATRIX[name]
        entry = {"machine": machine, "action": "build"}
        candidate = reuse.get(name, reuse_from / name if reuse_from else None)
        if candidate is not None:
            try:
                artifact = core_artifacts.inspect_artifact(candidate, machine,
                    gbc_psram=psram, genesis_pal=pal)
                if not artifact["current_input_match"]:
                    changed = artifact.get("changed_required", artifact.get("changed_inputs", []))
                    raise ValueError("Reuse requires an FPGA rebuild for " + name + ": " + ", ".join(changed))
                entry.update(action="reuse", status="validated", directory=str(candidate.resolve()), artifact=artifact)
            except (OSError, ValueError, KeyError) as error:
                if name in reuse:
                    raise
                entry.update(reuse_candidate=str(candidate.resolve()), reuse_rejection=str(error))
        if entry["action"] == "build":
            directory = output / name
            if directory.exists():
                raise ValueError("New FPGA build directory already exists: " + str(directory))
            entry.update(status="planned", directory=str(directory), build_id=build_id)
        selections[name] = entry
    return {"schema_version": 1, "requested_build_id": build_id,
            "output": str(output), "status": "planned", "images": selections}


def write_selection(path, plan):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def main(argv=None):
    import core_artifacts
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--build-id", default=BUILD_ID, help="ID for newly built images; reused images keep their original ID")
    parser.add_argument("--image", choices=tuple(MATRIX), action="append")
    parser.add_argument("--reuse-image", action="append", default=[], metavar="NAME=DIR",
                        help="Reuse this explicit image directory after provenance, variant and input checks")
    parser.add_argument("--reuse-from", type=Path, metavar="DIR",
                        help="Validate DIR/NAME for each selection; reuse matches and build missing/incompatible images")
    parser.add_argument("--plan", "--check-only", action="store_true", dest="plan",
                        help="Validate and display all selections without running Vivado")
    parser.add_argument("--selection-manifest", type=Path,
                        help="Save the selection record (execution default: OUTPUT/core-selection.json)")
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=2)
    parser.add_argument("--vivado", type=Path, default=Path("C:/Xilinx/2025.2/Vivado/bin/vivado.bat"))
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[0-9A-Fa-f]{8}", args.build_id):
        parser.error("--build-id must contain exactly eight hexadecimal digits")
    args.build_id = args.build_id.upper()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        parser.error("Output must be a fresh child of the checkout build directory")
    images = args.image or list(MATRIX)
    if len(images) != len(set(images)):
        parser.error("Duplicate image")
    try:
        reuse = core_artifacts.parse_reuse_images(args.reuse_image, MATRIX)
        if set(reuse) - set(images):
            raise ValueError("Reused image was not selected by --image: " + ", ".join(sorted(set(reuse) - set(images))))
        plan = selection_plan(images, reuse, args.build_id, output,
                              args.reuse_from.resolve() if args.reuse_from else None)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    manifest = args.selection_manifest.resolve() if args.selection_manifest else output / "core-selection.json"
    if not manifest.is_relative_to(ROOT / "build"):
        parser.error("Selection manifest must stay under the checkout build directory")
    if (not args.plan or args.selection_manifest) and manifest.exists():
        parser.error("Selection manifest already exists: " + str(manifest))
    for name, entry in plan["images"].items():
        if entry["action"] == "reuse":
            artifact = entry["artifact"]
            print(f"PLAN REUSE {name} from={entry['directory']} original_build_id={artifact['build_id']} abi={artifact['abi']}", flush=True)
        else:
            reason = " reason=" + entry["reuse_rejection"] if "reuse_rejection" in entry else ""
            print(f"PLAN BUILD {name} build_id={args.build_id} output={entry['directory']}{reason}", flush=True)
    reuse_count = sum(entry["action"] == "reuse" for entry in plan["images"].values())
    print(f"PLAN TOTAL build={len(images) - reuse_count} reuse={reuse_count}", flush=True)
    if args.plan:
        if args.selection_manifest:
            write_selection(manifest, plan)
        return 0
    plan["status"] = "running"
    write_selection(manifest, plan)
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]

    def build(name):
        machine, psram, pal = MATRIX[name]
        folder = output / name
        folder.mkdir(parents=True, exist_ok=False)
        (folder / "temp").mkdir()
        environment = dict(os.environ, TEMP=(folder / "temp").as_posix(), TMP=(folder / "temp").as_posix())
        # Remove inherited variant selections; every build names all applicable settings.
        for key in ("MULTITINI_GENESIS_PAL", "MULTITINI_ENABLE_GBC_PSRAM", "MULTITINI_C64_VIDEO_STANDARD"):
            environment.pop(key, None)
        settings = {"MULTITINI_VIVADO_DIR": folder.as_posix(), "MULTITINI_MACHINE": machine,
                    "MULTITINI_BUILD_ID": args.build_id,
                    "TEMP": environment["TEMP"], "TMP": environment["TMP"]}
        if "MULTITINI_VIVADO_JOBS" in environment:
            settings["MULTITINI_VIVADO_JOBS"] = environment["MULTITINI_VIVADO_JOBS"]
        if machine == "C64": settings["MULTITINI_C64_VIDEO_STANDARD"] = "PAL"
        if machine == "GBC": settings["MULTITINI_ENABLE_GBC_PSRAM"] = str(int(psram))
        if machine == "GENESIS": settings["MULTITINI_GENESIS_PAL"] = str(int(pal))
        config = "".join(f"set ::env({key}) {{{value}}}\n" for key, value in settings.items())
        for kind, script in (("generate", "create_project"), ("build", "build_and_export_xsa")):
            (folder / f"{kind}.tcl").write_text(config +
                f"source {{{(ROOT / 'scripts' / (script + '.tcl')).as_posix()}}}\n", encoding="utf-8")
        capture = [sys.executable, ROOT / "scripts/capture_vectrex_build_inputs.py",
                   "--machine", machine, "--output", folder / "source-inputs.json"]
        if psram: capture.append("--gbc-psram")
        if pal: capture.append("--genesis-pal")
        subprocess.run(list(map(str, capture)), cwd=ROOT, env=environment, check=True)
        record = json.loads((folder / "source-inputs.json").read_text())
        with zipfile.ZipFile(folder / "source-inputs.zip", "w", zipfile.ZIP_DEFLATED) as archive:
            for filename, digest in record["files"].items():
                data = (ROOT / filename).read_bytes()
                if hashlib.sha256(data).hexdigest() != digest:
                    raise RuntimeError("Source changed during capture: " + filename)
                archive.writestr(filename, data)
            for filename in ("source-inputs.json", "generate.tcl", "build.tcl"):
                archive.write(folder / filename, "build-config/" + filename)
        print("START " + name, flush=True)
        for kind in ("generate", "build"):
            command = guard + [str(args.vivado), "-mode", "batch", "-source", str(folder / f"{kind}.tcl"),
                              "-log", str(folder / f"{kind}.log"), "-journal", str(folder / f"{kind}.jou")]
            with (folder / f"{kind}-console.log").open("w", encoding="utf-8") as log:
                subprocess.run(command, cwd=folder, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([sys.executable, str(ROOT / "scripts/capture_vectrex_build_inputs.py"),
                        "--output", str(folder / "source-inputs.json"), "--verify"], cwd=ROOT, check=True)
        artifact = core_artifacts.inspect_artifact(folder, machine, gbc_psram=psram, genesis_pal=pal)
        core_artifacts.require_compatible_inputs(artifact)
        if int(artifact["build_id"], 16) != int(args.build_id, 16):
            raise ValueError("New FPGA image does not have its requested build ID: " + name)
        print("PASS " + name, flush=True)
        return artifact

    failed = []
    build_images = [name for name, entry in plan["images"].items() if entry["action"] == "build"]
    for name, entry in plan["images"].items():
        if entry["action"] == "reuse":
            entry["status"] = "reused"
            print("REUSED " + name, flush=True)
    write_selection(manifest, plan)

    def record_result(name, error=None, artifact=None):
        entry = plan["images"][name]
        entry["status"] = "failed" if error else "built"
        if error:
            entry["error"] = str(error)
            failed.append(name)
            print(f"FAIL {name}: {error}", flush=True)
        elif artifact:
            entry["artifact"] = artifact
        write_selection(manifest, plan)

    # Complete supervisor startup before other PS7 IP builds share the tool state.
    if "supervisor" in build_images and len(build_images) > 1:
        try:
            artifact = build("supervisor")
            record_result("supervisor", artifact=artifact)
        except Exception as error:
            record_result("supervisor", error)
        build_images.remove("supervisor")
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = {pool.submit(build, name): name for name in build_images}
        for future in as_completed(pending):
            try:
                artifact = future.result()
                record_result(pending[future], artifact=artifact)
            except Exception as error:
                record_result(pending[future], error)
    plan["status"] = "failed" if failed else "complete"
    write_selection(manifest, plan)
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
