#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate user-supplied boot ROMs and stage them only in a private SD delivery."""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CORES = {"c64", "gbc", "vectrex", "genesis", "apple-iigs"}
REQUIRED = {
    "c64": {"basic.bin": 8192, "kernal.bin": 8192, "characters.bin": 4096, "1541.bin": 16384},
    "vectrex": {"bios.bin": 8192},
    "apple-iigs": {"rom3.rom": 262144, "rom1.rom": 131072, "char.rom": 4096,
                   "adb3.rom": 4096, "adb1.rom": 4096, "hdd.rom": 256},
}
NO_EXTERNAL = {
    "gbc": "The core embeds the licensed MIT SameBoy bootstrap. Supply a user cartridge in ../media/.\n",
    "genesis": "This core needs no external BIOS; TMSS is disabled. Supply a user cartridge in ../media/.\n",
}


def default_rom_source():
    return Path(os.environ.get("MULTITINI_ROM_SOURCE", str(Path.home() /
        "OneDrive/Documents/Apple2/Appletini/Appletini ONE/multitini_roms")))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked(data, size, name):
    if len(data) != size:
        raise ValueError(f"{name}: expected {size} ROM bytes, got {len(data)}")
    if len(set(data)) <= 1:
        raise ValueError(f"{name}: uniform/blank ROM rejected")
    return data


@dataclass
class RomCollection:
    outputs: dict[str, bytes]
    provenance: dict


def collect(source, cores, *, iigs_mame_zip=None):
    source = Path(source).resolve()
    cores = set(cores)
    if not source.is_dir() or not cores or not cores <= CORES:
        raise ValueError("A complete user ROM source directory and known core set are required")
    if source.is_relative_to(ROOT) and not source.is_relative_to(ROOT / "build"):
        raise ValueError("Private ROM inputs must stay outside source directories")
    inputs, outputs, records = {}, {}, {}

    def read(path):
        path = Path(path).resolve()
        if not path.is_relative_to(source) or not path.is_file():
            raise ValueError("Missing or unsafe private ROM input: " + str(path))
        data = path.read_bytes()
        relative = path.relative_to(source).as_posix()
        inputs[relative] = {"bytes": len(data), "sha256": sha(data)}
        return data, relative

    def add(core, name, data, size, origins, transform="exact-copy"):
        checked(data, size, core + "/" + name)
        destination = f"sd/multitini/cores/{core}/roms/{name}"
        outputs[destination] = data
        records[destination] = {"bytes": len(data), "sha256": sha(data),
                                "inputs": origins, "transformation": transform}

    for core in sorted(cores - {"apple-iigs"}):
        for name, size in REQUIRED.get(core, {}).items():
            data, origin = read(source / core / "roms" / name)
            add(core, name, data, size, [origin])
    fast = False
    if "c64" in cores:
        pair = [source / "c64/roms/fast" / name for name in ("kernal.bin", "1541.bin")]
        if any(path.exists() for path in pair):
            for path, size in zip(pair, (8192, 16384)):
                data, origin = read(path)
                add("c64", "fast/" + path.name, data, size, [origin])
            fast = True

    if "apple-iigs" in cores:
        folder = source / "apple-iigs/roms"
        prepared = {}
        for name, size in REQUIRED["apple-iigs"].items():
            path = folder / name
            candidates = [path] if path.is_file() else []
            if not candidates and name in {"char.rom", "adb1.rom", "adb3.rom"}:
                candidates = [folder / (path.stem + suffix) for suffix in (".hex", ".mem")
                              if (folder / (path.stem + suffix)).is_file()]
            if len(candidates) > 1:
                raise ValueError("Ambiguous auxiliary ROM input: " + name)
            if candidates:
                data, origin = read(candidates[0])
                transform = "exact-copy"
                if candidates[0].suffix in {".hex", ".mem"}:
                    tokens = data.decode("ascii").split()
                    if not tokens or any(re.fullmatch(r"[0-9a-fA-F]{2}", token) is None for token in tokens):
                        raise ValueError("Expected one hexadecimal byte per auxiliary ROM token")
                    data = bytes(int(token, 16) for token in tokens)
                    transform = "hexadecimal-byte-tokens"
                if name == "char.rom" and len(data) == 2048:
                    data *= 2
                    transform += "; duplicate-2KiB-font"
                prepared[name] = (data, [origin], transform)
        missing = set(REQUIRED["apple-iigs"]) - set(prepared)
        members = {}
        if missing - {"hdd.rom"}:
            if iigs_mame_zip is None:
                candidates = sorted(folder.glob("*.zip")) if folder.is_dir() else []
                if len(candidates) > 1:
                    raise ValueError("Multiple IIgs archives; select --iigs-mame-zip explicitly")
                iigs_mame_zip = candidates[0] if candidates else None
            if iigs_mame_zip is not None:
                container, origin = read(iigs_mame_zip)
                # Read only named MAME banks. The container itself is never copied.
                import io
                with zipfile.ZipFile(io.BytesIO(container)) as archive:
                    names = archive.namelist()
                    for name in ("341-0728", "341-0748", "342-0077-b", "344s0047.bin",
                                 "341s0632-2.bin", "341s0345.bin"):
                        matches = [entry for entry in names if Path(entry).name == name]
                        if len(matches) > 1:
                            raise ValueError("Ambiguous MAME bank: " + name)
                        if matches:
                            data = archive.read(matches[0])
                            key = origin + "!" + matches[0]
                            inputs[key] = {"bytes": len(data), "sha256": sha(data), "container": origin}
                            members[name] = (data, key)
            else:
                for name in ("341-0728", "341-0748", "342-0077-b", "344s0047.bin",
                             "341s0632-2.bin", "341s0345.bin"):
                    if (folder / name).is_file():
                        members[name] = read(folder / name)

            def member(name, size):
                data, origin = members[name]
                return checked(data, size, name), origin

            if "rom3.rom" in missing and {"341-0728", "341-0748"} <= set(members):
                a, ao = member("341-0728", 131072)
                b, bo = member("341-0748", 131072)
                prepared["rom3.rom"] = (a + b[65536:] + b[:65536], [ao, bo], "MAME-ROM3:A+B-upper64KiB+B-lower64KiB")
            for output, bank, size, transform in (
                    ("rom1.rom", "342-0077-b", 131072, "exact-MAME-ROM1"),
                    ("adb3.rom", "341s0632-2.bin", 4096, "exact-MAME-ADB3"),
                    ("adb1.rom", "341s0345.bin", 3072, "MAME-ADB1:prefix-1024-zero-bytes"),
                    ("char.rom", "344s0047.bin", 16384, "MAME-font:first-2KiB-mask7bit-and-duplicate")):
                if output in missing and bank in members:
                    data, origin = member(bank, size)
                    if output == "adb1.rom":
                        data = bytes(1024) + data
                    if output == "char.rom":
                        data = bytes(value & 0x7f for value in data[:2048]) * 2
                    prepared[output] = (data, [origin], transform)
        for name, size in REQUIRED["apple-iigs"].items():
            if name not in prepared:
                raise ValueError("Missing required private IIgs ROM: apple-iigs/roms/" + name)
            data, origins, transform = prepared[name]
            add("apple-iigs", name, data, size, origins, transform)

    provenance = {"schema_version": 1, "classification": "Private local delivery with user-supplied boot ROMs",
        "cores": sorted(cores),
        "source_directory": str(source), "inputs": inputs, "outputs": records,
        "optional_c64_fast_pair": fast,
        "no_external_boot_rom": {core: NO_EXTERNAL[core].strip() for core in sorted(cores & NO_EXTERNAL.keys())},
        "boot_limits": {"c64": "System ROMs can start BASIC; no disk or program is included.",
                        "vectrex": "BIOS boot is available; no cartridge is included.",
                        "gbc": "A user cartridge is required to run software.",
                        "genesis": "A user cartridge is required to run software.",
                        "apple-iigs": "User bootable HDD/WOZ and operating system are required for software boot; none are included."}}
    return RomCollection(outputs, provenance)


def verify_inputs(collection):
    source = Path(collection.provenance["source_directory"])
    for name, record in collection.provenance["inputs"].items():
        if "container" in record:
            continue  # Whole container hash binds the named member.
        path = (source / name).resolve()
        if not path.is_relative_to(source) or not path.is_file():
            raise ValueError("Private ROM source disappeared: " + name)
        data = path.read_bytes()
        if len(data) != record["bytes"] or sha(data) != record["sha256"]:
            raise ValueError("Private ROM source changed after validation: " + name)


def stage(out, collection):
    out = Path(out).resolve()
    if not out.is_relative_to(ROOT / "build") or out == ROOT / "build":
        raise ValueError("Private ROM delivery output must stay under build/")
    verify_inputs(collection)
    for name, data in collection.outputs.items():
        parts = PurePosixPath(name).parts
        if (len(parts) < 6 or parts[:3] != ("sd", "multitini", "cores") or
                parts[3] not in CORES or parts[4] != "roms" or ".." in parts or
                "\\" in name or ":" in name or
                collection.provenance["outputs"].get(name, {}).get("sha256") != sha(data)):
            raise ValueError("Private ROM output is outside the validated SD ROM layout")
        target = out / name
        if target.exists() and target.read_bytes() != data:
            raise ValueError("Refusing to replace a different staged ROM: " + name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    for core, text in collection.provenance["no_external_boot_rom"].items():
        folder = out / "sd/multitini/cores" / core / "roms"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "README.txt").write_text(text + "\n", encoding="utf-8")
    for core in collection.provenance["cores"]:
        folder = out / "sd/multitini/cores" / core / "media"
        folder.mkdir(parents=True, exist_ok=True)
        readme = folder / "README.txt"
        if not readme.exists():
            readme.write_text(collection.provenance["boot_limits"][core] + "\n"
                "Place user-owned media here. System boot ROMs belong in the sibling ../roms/ folder.\n",
                encoding="utf-8")
    return collection.provenance
