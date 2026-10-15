#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit and package explicit PHO1 firmware and all machine cores; never flash."""
from pathlib import Path
from datetime import datetime, timezone
from dataclasses import asdict
import argparse
import json
import re
import tempfile
import zipfile

import package_cpu1_video_test as package
import audit_vectrex_package as payload
import image_manifest
import core_artifacts
import private_core_roms
from build_phos1 import MATRIX, BUILD_ID

ROOT = package.ROOT
ABI_MINOR = 19
GS_GUIDES = ("APPLE_IIGS_HARDWARE_TEST.md", "APPLE_IIGS_CPU_VALIDATION.md",
             "APPLE_IIGS_PLAN.md", "APPLE_IIGS_M053_BUILD.md", "APPLE_IIGS_M052_BUILD.md",
             "CORE_ARTIFACT_REUSE.md", "USB_SD_MOUNT.md")


def verify_source_tar(directory, record):
    archive = record.get("archive", {})
    package.require(archive.get("name") == "source-inputs.sources.tar.gz",
                    "Unexpected FPGA source TAR archive name")
    path = directory / archive["name"]
    package.require(path.is_file() and package.sha(path) == archive.get("sha256"),
                    "FPGA source TAR archive missing or changed: " + str(path))
    return path


def image_selections(args, matrix):
    """Resolve explicit paths only; a selection file never discovers an image."""
    reuse = core_artifacts.parse_reuse_images(args.reuse_image, matrix)
    directories, build_ids, pinned = {}, {}, {}
    if args.selection_manifest:
        selected = json.loads(args.selection_manifest.read_text())
        package.require(selected.get("schema_version", selected.get("schema")) == 1,
                        "Unsupported image selection manifest")
        for name, entry in selected.get("images", {}).items():
            package.require(name in matrix, "Unknown selected image: " + name)
            package.require(entry.get("machine") == matrix[name][0], "Selected image machine differs: " + name)
            directory = Path(entry["directory"]).resolve()
            package.require(entry.get("action") in {"reuse", "build"}, "Unknown image selection action")
            if entry["action"] == "reuse":
                package.require(name not in reuse, "Duplicate explicit reuse selection: " + name)
                package.require(isinstance(entry.get("artifact"), dict), "Reuse selection lacks pinned artifact audit")
                reuse[name] = directory
                pinned[name] = entry["artifact"]
            else:
                package.require(name not in reuse, "Conflicting image build/reuse selection: " + name)
                directories[name] = directory
                build_ids[name] = entry.get("build_id", selected.get("requested_build_id", args.build_id))
                if isinstance(entry.get("artifact"), dict):
                    pinned[name] = entry["artifact"]
    package.require(not (args.supervisor_dir and "supervisor" in reuse), "Conflicting supervisor selections")
    package.require(not (args.supervisor_dir and "supervisor" in directories), "Conflicting supervisor selections")
    for name in matrix:
        if name not in directories:
            directories[name] = (reuse.get(name) or
                (args.supervisor_dir if name == "supervisor" and args.supervisor_dir else args.hardware / name)).resolve()
        build_ids.setdefault(name, args.build_id)
        build_ids[name] = str(build_ids[name]).removeprefix("0x").upper()
        package.require(re.fullmatch(r"[0-9A-F]{8}", build_ids[name]), "Invalid per-image build ID")
    return reuse, directories, build_ids, pinned


def match_pinned_artifact(expected, actual, name):
    for field in ("build_id", "abi", "artifact_sha256", "source_zip", "source_tar", "source_manifest"):
        package.require(expected.get(field) == actual.get(field), "Selected artifact changed: " + name + " " + field)


def verify_image_set(args, cpu):
    reuse, directories, build_ids, pinned = image_selections(args, MATRIX)
    builds = {}
    for name, (machine, psram, pal) in MATRIX.items():
        build = package.verify_hardware(directories[name], machine, build_id=build_ids[name],
                                        gbc_psram=psram, genesis_pal=pal, reuse=name in reuse)
        build["source_tar"] = verify_source_tar(directories[name], build["record"])
        core_artifacts.require_runtime_abi(build["identity"], 0, args.abi_minor)
        if machine == "APPLE_IIGS":
            package.require(build["identity"]["abi_minor"] >= 21, "IIgs transport requires ABI 0.21")
        if machine == "NES":
            package.require(build["identity"]["abi_minor"] >= 22, "NES package requires ABI 0.22")
        build["platform_contract"] = core_artifacts.platform_contract(build["ps7_init_sha256"], cpu["ps7_init_sha256"])
        if name in pinned:
            match_pinned_artifact(pinned[name], build["identity"], name)
        builds[name] = build
    return builds


def image_records(builds):
    return {name: dict(build["identity"], selection="reuse" if build["reused"] else "new-build",
                       bit=package.describe(build["bit"]), xsa=package.describe(build["xsa"]),
                       platform_contract=build["platform_contract"],
                       original_sources={"zip": f"evidence/{name}/source-inputs.zip",
                                         "tar": f"evidence/{name}/source-inputs.sources.tar.gz",
                                         "manifest": f"evidence/{name}/source-inputs.json"})
            for name, build in builds.items()}


def copy_delivery_guides(out):
    guides = {}
    for name in GS_GUIDES:
        target = out / "docs" / name
        package.copy(ROOT / "docs" / name, target)
        guides["docs/" + name] = package.describe(target)
    return guides


def prepare_iigs_content(out, hdd_bootstrap):
    """Keep both sibling directories present after a fresh ZIP extraction."""
    core = out / "sd/multitini/cores/apple-iigs"
    package.copy(hdd_bootstrap, core / "roms/hdd.rom")
    media = core / "media"
    media.mkdir(parents=True, exist_ok=True)
    (media / "README.txt").write_text(
        "Apple IIgs media and persistent state\n\n"
        "Hard disks accept .hdv, .po or ProDOS .2mg.\n"
        "The 3.5-inch drive accepts 800 KiB .po/.2mg and matching WOZ2 images.\n"
        "The 5.25-inch drive accepts 140 KiB DOS-order .dsk and matching WOZ2 images.\n"
        "The firmware saves pram.bin, gs-settings.bin and recovery journals here.\n"
        "Keep recovery journals and raw-floppy .gsraw sidecars with their media.\n"
        "Eject disks before removing power.\n"
        "System, character and ADB ROMs belong in the sibling ../roms/ folder,\n"
        "alongside the included licensed hdd.rom bootstrap.\n"
        "Private packaging stages validated owner-supplied Apple ROMs in that folder.\n"
        "No games or operating-system disk images are supplied.\n", encoding="utf-8")


def prepare_nes_content(out):
    """Preserve the NES media directory in an extracted ROM-free package."""
    media = out / "sd/multitini/cores/nes/media"
    media.mkdir(parents=True, exist_ok=True)
    (media / "README.txt").write_text(
        "Nintendo Entertainment System cartridges\n\n"
        "Place your iNES (.nes) cartridges here. No system BIOS is required.\n"
        "Only the cartridge formats and mappers listed in docs/NES_PLAN.md are supported.\n"
        "No games or Nintendo firmware are supplied.\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hardware", type=Path, required=True)
    parser.add_argument("--supervisor-dir", type=Path, help="Explicit separately rebuilt supervisor")
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--output", type=Path, help="Fresh output directory; default build/firmware-<suffix>")
    parser.add_argument("--build-id", default=BUILD_ID, help="Eight hexadecimal digits identifying the firmware release")
    parser.add_argument("--abi-minor", type=int, default=ABI_MINOR)
    parser.add_argument("--suffix", default="phos1", help="Lowercase filename suffix, without a leading hyphen")
    parser.add_argument("--vitis", type=Path, default=Path("C:/Xilinx/2025.2/Vitis"))
    parser.add_argument("--evidence", action="append", type=Path, default=[])
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--include-apple-iigs", action="store_true", help="Include the GS source/machine and runtime HDD boot ROM")
    parser.add_argument("--include-nes", action="store_true", help="Include the NES machine and empty cartridge directory")
    parser.add_argument("--reuse-image", action="append", default=[], metavar="NAME=DIR",
                        help="Reuse an explicitly selected audited FPGA build with its original identity and sources")
    parser.add_argument("--selection-manifest", type=Path,
                        help="Explicit build/reuse selection manifest emitted by the matrix builder")
    parser.add_argument("--rom-source", type=Path, default=private_core_roms.default_rom_source(),
                        help="Required private boot-ROM collection; defaults to MULTITINI_ROM_SOURCE or the user's OneDrive ROM folder")
    parser.add_argument("--iigs-mame-zip", type=Path,
                        help="Optional user-owned MAME archive within --rom-source for missing prepared IIgs ROMs")
    args = parser.parse_args()
    if args.include_apple_iigs:
        MATRIX['apple-iigs']=('APPLE_IIGS',False,False)
    if args.include_nes:
        MATRIX['nes']=('NES',False,False)
    if not re.fullmatch(r"[0-9A-Fa-f]{8}", args.build_id):
        parser.error("--build-id must contain exactly eight hexadecimal digits")
    if not 0 <= args.abi_minor <= 65535:
        parser.error("--abi-minor must be in the range 0..65535")
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", args.suffix) or len(args.suffix) > 64:
        parser.error("--suffix must be a safe lowercase alphanumeric slug, optionally separated by hyphens")
    args.build_id = args.build_id.upper()
    package.ABI = f"0.{args.abi_minor}"
    output = args.output or ROOT / ("build/firmware-" + args.suffix)
    workspace, out, vitis = args.workspace.resolve(), output.resolve(), args.vitis.resolve()
    package.require(out.is_relative_to(ROOT / "build") and out != ROOT / "build", "Output must stay under build/")
    if not args.check_only:
        package.require(not out.exists(), "Output directory already exists: " + str(out))
        package.require(not out.with_suffix(".zip").exists(), "Output ZIP already exists: " + str(out.with_suffix(".zip")))
    firmware_name = f"FIRMWARE-{args.suffix}.BIN"
    identity = bytes.fromhex(args.build_id).decode("ascii", errors="replace")
    if not re.fullmatch(r"[A-Z0-9]+", identity):
        identity = "0x" + args.build_id
    if args.include_apple_iigs or args.include_nes:
        version = re.search(r'^#define MT_FIRMWARE_VERSION\s+"([^"]+)"',
                            (ROOT / "firmware/include/multitini_version.h").read_text(), re.M)
        package.require(version is not None, "Missing firmware release version")
        identity = version[1]
    cpu = package.verify_cpu_images(workspace, build_id=args.build_id, abi_minor=args.abi_minor)
    builds = verify_image_set(args, cpu)
    rom_cores = {"c64", "gbc", "vectrex", "genesis"} | ({"apple-iigs"} if args.include_apple_iigs else set())
    roms = private_core_roms.collect(args.rom_source, rom_cores, iigs_mame_zip=args.iigs_mame_zip)
    if args.include_apple_iigs:
        package.require(roms.outputs["sd/multitini/cores/apple-iigs/roms/hdd.rom"] ==
                        (ROOT / "build/apple-iigs-assets/hdd.rom").read_bytes(),
                        "Supplied IIgs HDD bootstrap differs from the licensed source-built bootstrap")
    required = dict(cpu["record"]["files"])
    for build in builds.values():
        if build["reused"]:
            continue
        for name, digest in build["record"]["files"].items():
            package.require(name not in required or required[name] == digest, "Source conflict: " + name)
            required[name] = digest
    source = package.source_inventory(required)
    for path in args.evidence:
        package.require(path.is_file(), "Missing evidence: " + str(path))
    if args.check_only:
        print(json.dumps({"status": "PASS", "firmware_id": "0x" + args.build_id, "abi": package.ABI,
                          "images": image_records(builds), "private_roms": roms.provenance,
                          "matrix": {n: b["timing"] for n, b in builds.items()}}, indent=2))
        return 0
    out.mkdir(parents=True, exist_ok=False)
    for name, path in cpu["paths"].items():
        package.copy(path, out / "inputs" / {"cpu0_elf": "diagnostic.elf", "cpu1_elf": "video_worker.elf",
                                             "cpu1_binary": "video_core1_blob.bin"}[name])
    package.copy(cpu["fsbl"], out / "inputs/fsbl.elf")
    package.copy(cpu["xsa"], out / "inputs/platform.xsa")
    package.copy(workspace / "cpu1_manifest.json", out / "evidence/vitis/cpu1_manifest.json")
    if args.selection_manifest:
        package.copy(args.selection_manifest, out / "evidence/core-selection.json")
    for app in ("diagnostic", "video_worker"):
        for name in ("CMakeLists.txt", "UserConfig.cmake", "lscript.ld", "app.yaml"):
            package.copy(workspace / app / "src" / name, out / "evidence/vitis" / app / name)
        package.copy(workspace / app / "build/compile_commands.json", out / "evidence/vitis" / app / "compile_commands.json")
    for name, build in builds.items():
        package.copy(build["bit"], out / "inputs" / (name + ".bit"))
        package.copy(Path(str(build["bit"]) + ".kind"), out / "inputs" / (name + ".bit.kind"))
        package.copy(build["xsa"], out / "inputs" / (name + ".xsa"))
        directory = build["bit"].parents[2]
        package.copy(directory / "source-inputs.json", out / "evidence" / name / "source-inputs.json")
        source_tar = out / "evidence" / name / build["source_tar"].name
        package.copy(build["source_tar"], source_tar)
        package.require(package.sha(source_tar) == build["record"]["archive"]["sha256"],
                        "Packaged FPGA source TAR archive changed: " + name)
        for path in build["artifacts"]:
            if path not in (build["bit"], Path(str(build["bit"]) + ".kind"), build["xsa"]):
                package.copy(path, out / "evidence" / name / path.relative_to(directory))
    for i, path in enumerate(args.evidence):
        package.copy(path, out / "evidence/validation" / f"{i:02d}-{path.name}")
    delivery_guides = copy_delivery_guides(out) if args.include_apple_iigs else {}
    package.archive_files(out / "source.zip", {name: ROOT / name for name in source})
    package.write_json(out / "source-manifest.json", source)
    package.archive_files(out / "inputs/used-vitis-platform.zip",
        {"multitini_platform/" + name: cpu["platform"] / name for name in cpu["record"]["platform_files"]})
    package.write_json(out / "evidence/vitis/platform-manifest.json", cpu["record"]["platform_files"])
    bootgen = vitis / "bin/bootgen.bat"
    (out / "sd").mkdir()
    package.run([ROOT / "scripts/make_firmware_bin.bat", out / firmware_name, out / "inputs/fsbl.elf",
                 out / "inputs/supervisor.bit", out / "inputs/diagnostic.elf"], vitis=vitis)
    package.copy(out / firmware_name, out / "sd/FIRMWARE.BIN")
    core_specs = (("c64", "Commodore 64", "0x43363420", [("pal", "PAL", "c64-pal")]),
                  ("gbc", "Game Boy Color", "0x47424320", [("native", "Game Boy / Game Boy Color", "gbc")]),
                  ("vectrex", "Vectrex", "0x56454358", [("native", "Vectrex / Vectrex32", "vectrex")]),
                  ("genesis", "Genesis / Mega Drive", "0x4D442020", [("ntsc", "NTSC", "genesis-ntsc"), ("pal", "PAL", "genesis-pal")]))
    if args.include_apple_iigs:
        core_specs += (("apple-iigs", "Apple IIgs", "0x49494753", [("native", "Apple IIgs", "apple-iigs")]),)
        # Verify the licensed source-built bootstrap before staging private ROMs.
        asset=ROOT/'build/apple-iigs-assets/hdd.rom'
        asset_record=json.loads((asset.parent/'provenance.json').read_text())
        package.require(asset.is_file() and asset.stat().st_size==256, "Missing source-built IIgs HDD bootstrap")
        package.require(asset_record.get('license')=='GPL-2.0-or-later' and
                        asset_record.get('sha256')==package.sha(asset) and
                        asset_record.get('source_sha256')==package.sha(ROOT/asset_record['source']),
                        "IIgs HDD bootstrap/source provenance differs")
        prepare_iigs_content(out, asset)
        package.copy(asset.parent/'provenance.json',out/'evidence/apple-iigs/hdd-rom-provenance.json')
    if args.include_nes:
        core_specs += (("nes", "Nintendo Entertainment System", "0x4E455320", [("ntsc", "NTSC", "nes")]),)
        prepare_nes_content(out)
        nes_guides = ("NES_PLAN.md", "NES_HARDWARE_TEST.md")
        if (ROOT / "docs/NES_BUILD.md").is_file():
            nes_guides += ("NES_BUILD.md",)
        for name in nes_guides:
            target = out / "docs" / name
            package.copy(ROOT / "docs" / name, target)
            delivery_guides["docs/" + name] = package.describe(target)
        package.copy(ROOT / "third_party/nes/upstream/COPYING", out / "licenses/nes/COPYING")
        package.copy(ROOT / "third_party/nes/T65-NOTICE.txt", out / "licenses/nes/T65-NOTICE.txt")
    private_core_roms.stage(out, roms)
    package.write_json(out / "evidence/private-rom-provenance.json", roms.provenance)
    cores = {}
    with tempfile.TemporaryDirectory(prefix=args.suffix + "-audit-", dir=ROOT / "build") as temporary:
        scratch = Path(temporary)
        objcopy = vitis / "gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy.exe"
        package.run([objcopy, "-O", "binary", out / "inputs/video_worker.elf", scratch / "worker.bin"])
        package.require((scratch / "worker.bin").read_bytes() == (out / "inputs/video_core1_blob.bin").read_bytes(), "CPU1 raw image mismatch")
        for ident, label, core_id, variants in core_specs:
            directory = out / "sd/multitini/cores" / ident
            content_root = "0:/multitini/cores/" + ident + "/media"
            abis = {builds[v[2]]["identity"]["abi"] for v in variants}
            package.require(len(abis) == 1, "Core variants need the same actual ABI: " + ident)
            core_abi = abis.pop()
            major, minor = core_abi.split(".")
            command = [package.sys.executable, ROOT / "scripts/make_core_package.py", "--id", ident, "--name", label,
                       "--core-id", core_id, "--abi-major", major, "--abi-minor", minor,
                       "--content-root", content_root, "--output", directory, "--bootgen", bootgen]
            for variant, display, build_name in variants:
                command += ["--variant", f"{variant}|{display}|{out / 'inputs' / (build_name + '.bit')}"]
            package.run(command, vitis=vitis)
            cores[ident] = package.verify_core(directory, {v[0] for v in variants}, abi=core_abi)
            for variant, _, build_name in variants:
                processed, _ = payload.process_bit(bootgen, out / "inputs" / (build_name + ".bit"), scratch, build_name)
                package.require(package.digest(processed) == cores[ident][variant]["sha256"], "SD payload differs from selected bitstream")
        firmware = out / firmware_name
        update_manifest = image_manifest.verify_image(firmware,
            expected_role=image_manifest.ROLE_FIRMWARE, expected_recovery=True)
        package.require(firmware.stat().st_size <= (0x00600000 if args.include_apple_iigs else 0x00DF0000), "Firmware exceeds the active updater slot")
        raw_firmware = scratch / "firmware.raw.bin"
        raw_firmware.write_bytes(firmware.read_bytes()[:update_manifest.payload_size])
        images = payload.parse_firmware(raw_firmware.read_bytes())
        audit = {"status": "PASS", "fsbl": payload.verify_elf(images[0], out / "inputs/fsbl.elf"),
                 "cpu0": payload.verify_elf(images[2], out / "inputs/diagnostic.elf"), "cores": cores,
                 "cpu1_embedded_sha256": package.sha(out / "inputs/video_core1_blob.bin"),
                 "update_manifest": asdict(update_manifest), "raw_bootgen_sha256": package.sha(raw_firmware)}
        processed, log = payload.process_bit(bootgen, out / "inputs/supervisor.bit", scratch, "supervisor")
        package.require(images[1][0]["payload"] == processed and images[1][0]["attributes"] == 0x20 and
                        images[1][0]["load_address"] == images[1][0]["entry_address"] == 0, "Supervisor partition mismatch")
        audit.update(supervisor_processed_sha256=package.digest(processed), bootgen_process_output=log,
                     bootgen_readback=payload.bootgen(bootgen, ["-read", raw_firmware], scratch))
    package.write_json(out / "evidence/payload-audit.json", audit)
    package.write_json(out / "evidence/timing-audit.json", {n: b["timing"] for n, b in builds.items()})
    (out / "README.txt").write_text(
        f"Multitini {identity} / -{args.suffix} / ABI {package.ABI}\n\n"
        "Copy sd/ contents to the SD card, preserving media and settings.\n"
        f"{firmware_name} is identical to sd/FIRMWARE.BIN, the updater's required filename.\n"
        "Use the existing golden updater, then reload cores. No flash was performed here.\n"
        "Selected cores: C64 PAL, PSRAM GBC, Vectrex, Genesis NTSC/PAL.\n"
        + ("NES adds iNES cartridge loading through the shared PSRAM service.\n"
           "No NES BIOS or cartridges are included; see docs/NES_PLAN.md.\n" if args.include_nes else "") +
        "This is a private local delivery with validated user-supplied boot ROMs in sd/*/roms.\n"
        + ("Apple IIgs includes selectable stock/accelerated CPU, HDD/WOZ media and PRAM.\n"
           "Apple system/character/ADB ROMs are included privately; see docs/APPLE_IIGS_HARDWARE_TEST.md.\n"
           "The new FSBL enables HP3; install matching firmware before selecting the GS core.\n" if args.include_apple_iigs else "") +
        "Appletini F1.2.5-derived Dot bleed, Blur and Glow replace independent axis smoothing.\n"
        + ("Blur uses a 12.5%, 25%, 50% kernel mix and preserves flat colours exactly. Glow is unchanged.\n"
           if args.build_id in ("50484F32", "50484F33", "50484F34", "50484F35") else "") +
        ("Per-core Video size: 1x/2x/3x/4x/Auto; centered oversize images clip.\n"
         "Auto retains current sizing; native source frames remain complete.\n"
           if args.build_id in ("50484F33", "50484F34", "50484F35") else "") +
        ("PHO4 centers the standby artwork in all six output modes.\n"
         "Opaque menus cover every output pixel, including partial cell strips.\n"
         "Screen screenshots use the same edge-cell background/transparency policy.\n"
           if args.build_id == "50484F34" else "") +
        ("PHO5 centers the visible compact MULTITINI/ONE/F12 block; 1080p is unchanged.\n"
         "Video size commits geometry without reprogramming an unchanged pixel clock.\n"
         "Effects-Off status and Genesis transfer phases are explicit.\n"
         "Genesis runtime diagnosis remains open; this package does not establish a cartridge-load fix.\n"
           if args.build_id == "50484F35" else "") +
        "Controls retain Off/Light/Medium/Strong; other picture controls are preserved.\n"
        "The FPGA applies frames in order; CPU1 prepares color tables.\n"
        "source.zip and evidence/ contain exact source inputs, tool settings, timing/CDC,\n"
        "compile records and independent firmware/core payload audits. No games or OS disks are supplied.\n"
        "source.zip is the current firmware/source snapshot; reused images retain their original\n"
        "source ZIP/TAR and settings under evidence/<image>/. manifest.json records each identity.\n"
        + ("See docs/CORE_ARTIFACT_REUSE.md for per-image selection and compatibility limits.\n"
           if args.include_apple_iigs else "") +
        "Vivado/Vitis 2025.2; board performance and gameplay acceptance remain pending.\n", encoding="utf-8")
    package.verify_sources(source)
    package.verify_cpu_images(workspace, build_id=args.build_id, abi_minor=args.abi_minor)
    for name, build in builds.items():
        package.require(package.sha(build["bit"]) == package.sha(out / "inputs" / (name + ".bit")), "Bitstream changed during packaging")
    final_builds = verify_image_set(args, cpu)
    for name in builds:
        match_pinned_artifact(builds[name]["identity"], final_builds[name]["identity"], name)
    if args.selection_manifest:
        package.require(package.sha(args.selection_manifest) == package.sha(out / "evidence/core-selection.json"),
                        "Image selection manifest changed during packaging")
    private_core_roms.verify_inputs(roms)
    for name, record in roms.provenance["outputs"].items():
        package.require(package.describe(out / name) == {key: record[key] for key in ("bytes", "sha256")},
                        "Staged private ROM differs from its validated input: " + name)
    files = {p.relative_to(out).as_posix(): package.describe(p) for p in sorted(out.rglob("*")) if p.is_file()}
    package.write_json(out / "manifest.json", {"classification": "Private local ROM delivery; board acceptance pending",
        "created_utc": datetime.now(timezone.utc).isoformat(), "firmware_id": "0x" + args.build_id, "suffix": "-" + args.suffix,
        "abi": package.ABI, "git_head": package.git("rev-parse", "HEAD").strip(), "dirty_source_snapshot": True,
        "source_files": len(source), "images": image_records(builds),
        "private_roms": roms.provenance,
        "delivery_guide_files": delivery_guides, "files": files})
    files["manifest.json"] = package.describe(out / "manifest.json")
    (out / "SHA256SUMS.txt").write_text("".join(f"{v['sha256']}  {n}\n" for n, v in sorted(files.items())), encoding="ascii")
    package.archive_files(out.with_suffix(".zip"), {p.relative_to(out).as_posix(): p for p in out.rglob("*") if p.is_file()})
    print(json.dumps({"status": "PASS", "firmware": package.describe(out / firmware_name),
                      "bundle": package.describe(out.with_suffix(".zip")), "output": str(out)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
