#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real private KERNAL ROM -> instruction-level C64 model -> IEC RTL -> DOS.

This is closed-loop compatibility evidence, not a cycle-exact C64/CDC test.
No ROM or disk bytes are copied into tracked files or receipts.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading

from c64sim import D64, System
from c64sim.iec import IECBus

ROOT = Path(__file__).resolve().parents[1]
GUARD = ROOT / "scripts/run_native_tool.py"
UCRT = Path("C:/msys64/ucrt64/bin")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def native(args: list[object], cwd: Path, log: Path) -> None:
    command = [sys.executable, str(GUARD), "--", *map(str, args)]
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                            errors="replace", timeout=120)
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"native tool failed; see {log}")


class Endpoint:
    def __init__(self, executable: Path, backend, log: Path, corrupt: bool = False, clock_hz=31524725):
        self.backend, self.log = backend, log.open("w")
        self.process = subprocess.Popen([sys.executable, str(GUARD), "--", str(UCRT / "vvp.exe"),
                                         str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.lines = queue.Queue()
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()
        self.pending = []
        self.inflight = False
        self.events, self.replies, self.corrupt = 0, 0, corrupt
        self.corrupted = False
        self.last_line = None
        self.state = None
        self.ticks = 0
        self.previous_inputs = None
        self.last_progress = 0
        self.context = None
        self.last_state = None
        self.ack_timeout = clock_hz // 50
        if self.line() != "READY":
            raise RuntimeError("endpoint did not initialize")

    def _reader(self):
        for line in self.process.stdout:
            self.lines.put(line.strip())
        self.lines.put(None)

    def line(self):
        line = self.lines.get(timeout=60)
        if line is None:
            raise RuntimeError(f"endpoint exited {self.process.poll()}")
        if line != self.last_line:
            self.log.write(line + "\n")
            self.log.flush()
            self.last_line = line
        return line

    def step(self, ticks, atn, clk, data):
        inputs = (atn, clk, data)
        if inputs != self.previous_inputs:
            self.log.write(f"I {self.ticks} {ticks} {atn} {clk} {data}\n")
            self.previous_inputs = inputs
        self.ticks += ticks
        reply = self.pending.pop(0) if self.pending and not self.inflight else None
        self.process.stdin.write(f"{ticks} {atn} {clk} {data} {int(reply is not None)} {reply or 0:08x} 1\n")
        self.process.stdin.flush()
        while True:
            line = self.line()
            if line.startswith("E "):
                event = int(line[2:], 16)
                self.events += 1
                self.last_progress = self.ticks
                response = self.backend.test_iec_event(event)
                if response != 0xFFFFFFFF:
                    self.replies += 1
                    if self.corrupt and self.replies == 5 and not (response & 0x200):
                        response ^= 1
                        self.corrupted = True
                    self.pending.append(response)
            elif line.startswith("R "):
                if line != self.last_state and self.context:
                    self.log.write("P " + self.context() + "\n")
                    self.last_state = line
                data_o, clk_o, token, valid = map(int, line.split()[1:5])
                self.state = line
                if int(line.split()[8]) and self.ticks-self.last_progress > self.ack_timeout:
                    raise RuntimeError("running KERNAL did not acknowledge IEC byte within20ms")
                self.inflight = bool(valid)
                return data_o, clk_o
            else:
                raise RuntimeError("unexpected endpoint output: " + line)

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.write("-1 1 1 1 0 00000000 0\n")
            self.process.stdin.flush()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.log.close()


class RTLBus(IECBus):
    def __init__(self, previous, system, endpoint):
        self.__dict__.update(previous.__dict__)
        self.endpoint, self.system = endpoint, system
        self.clock = system.c64.cpu.processorCycles * 32
        self.soft_data, self.soft_clk = 1, 1
        self._recompute()

    def external(self):
        return (int(not self.c64_atn), int(not (self.c64_clk or self.drive_clk)),
                int(not (self.c64_data or self.drive_data)))

    def advance(self):
        target = self.system.c64.cpu.processorCycles * 32
        if target < self.clock:
            raise AssertionError("co-simulation clock reversed")
        if target != self.clock:
            self.soft_data, self.soft_clk = self.endpoint.step(target - self.clock, *self.external())
            self.clock = target
            self._levels()

    def _levels(self):
        self.atn_low = self.c64_atn
        self.clk_low = self.c64_clk or self.drive_clk or not self.soft_clk
        self.data_low = self.c64_data or self.drive_data or not self.soft_data

    def _recompute(self):
        self.soft_data, self.soft_clk = self.endpoint.step(0, *self.external())
        self._levels()

    def update_c64(self, atn, clk, data):
        self.advance()
        super().update_c64(atn, clk, data)

    def update_drive(self, clk, data):
        self.advance()
        super().update_drive(clk, data)


def emit_call(code: bytearray, address: int):
    code.extend((0x20, address & 255, address >> 8))


def setup_file(system, name: str, device: int, secondary: int, logical: int = 2):
    encoded = name.encode("ascii")
    for i, byte in enumerate(encoded):
        system.poke(0xC600 + i, byte)
    code = bytearray((0xA9, 0))
    emit_call(code, 0xFF90)  # SETMSG: do not print loader messages.
    code.extend((0xA9, len(encoded), 0xA2, 0, 0xA0, 0xC6))
    emit_call(code, 0xFFBD)  # SETNAM
    code.extend((0xA9, logical, 0xA2, device, 0xA0, secondary))
    emit_call(code, 0xFFBA)  # SETLFS
    return code


def execute(system, bus, code, seconds=3):
    # Capture status/flags and the returned LOAD end pointer independently of
    # transport events. The test program is original; ROM routines are intact.
    code.extend(bytes.fromhex("08 48 8E 11 C2 8C 12 C2 68 8D 13 C2 68 8D 10 C2 A5 90 8D 14 C2 4C F0 C1"))
    if len(code) >= 0x1F0:
        raise ValueError("test program exceeded reserved RAM")
    for i, byte in enumerate(code):
        system.poke(0xC000 + i, byte)
    system.c64.cpu.pc = 0xC000
    try:
        system.run_until_pc("c64", 0xC1F0, seconds)
    except Exception as error:
        raise RuntimeError(f"KERNAL execution failed PC={system.c64.cpu.pc:04x} endpoint={bus.endpoint.state} external={bus.external()}") from error
    bus.advance()
    return {"carry": system.peek_ram(0xC210) & 1, "a": system.peek_ram(0xC213),
            "status": system.peek_ram(0xC214), "end": system.peek_word(0xC211)}


def load(system, bus, name, device=9):
    code = setup_file(system, name, device, 0)
    code.extend((0xA9, 0, 0xA2, 0, 0xA0, 0x40))
    emit_call(code, 0xFFD5)
    return execute(system, bus, code)


def command(system, bus, text):
    code = setup_file(system, text, 9, 15, 15)
    emit_call(code, 0xFFC0)
    code.extend((0xA9, 15)); emit_call(code, 0xFFC3)
    return execute(system, bus, code)


def read_status(system, bus):
    code = setup_file(system, "", 9, 15, 15)
    emit_call(code, 0xFFC0)
    code.extend((0xA2, 15)); emit_call(code, 0xFFC6)
    code.extend(bytes.fromhex("A9 00 8D 20 C2"))
    loop = len(code)
    emit_call(code, 0xFFCF)
    code.extend(bytes.fromhex("AE 20 C2 9D 00 C3 EE 20 C2 C9 0D"))
    distance = loop - (len(code) + 2)
    code.extend((0xD0, distance & 255))
    emit_call(code, 0xFFCC)
    code.extend((0xA9, 15)); emit_call(code, 0xFFC3)
    execute(system, bus, code)
    count = system.peek_ram(0xC220)
    if not 1 <= count <= 80:
        raise AssertionError("unbounded DOS status response")
    return system.ram_bytes(0xC300, 0xC300 + count).decode("ascii")


def run_case(build, roms, backend, *, fast=False, ntsc=False, corrupt=False, badlines=True):
    label = ("ntsc" if ntsc else "pal") + ("-fast" if fast else "-stock") + ("-mutant" if corrupt else "")
    folder = build / label
    folder.mkdir()
    media = folder / "media"; media.mkdir(); (media / "SUB").mkdir()
    payload = bytes((i * 29 + 7) & 255 for i in range(48))
    (media / "HELLO.PRG").write_bytes(b"\x01\x08" + payload)
    (media / "SUB/NESTED.PRG").write_bytes(b"\x01\x08nested-file")
    disk = D64.blank("COEXIST", "D8")
    disk.add_file("DRIVE8", b"\x01\x08device-eight")
    system = System(basic_path=roms / "basic.bin", chargen_path=roms / "characters.bin",
                    kernal_path=roms / ("fast/kernal.bin" if fast else "kernal.bin"),
                    drive_rom_path=roms / ("fast/1541.bin" if fast else "1541.bin"),
                    d64=disk, pal=not ntsc, parallel_cable=fast, extram=fast,
                    badlines=badlines)
    system.boot_to_ready(6)
    if backend.test_iec_init(str(media.resolve()).encode()) != 0:
        raise RuntimeError("DOS backend init failed")
    endpoint = Endpoint(build / ("ntsc.vvp" if ntsc else "pal.vvp"), backend,
                        folder / "endpoint.log", corrupt, 32727273 if ntsc else 31524725)
    checks = []
    def passed(message):
        checks.append(message)
        (folder / "progress.json").write_text(json.dumps({"case": label, "checks": checks}, indent=2) + "\n")
        print(f"{label}: {message}", flush=True)
    try:
        bus = RTLBus(system.bus, system, endpoint)
        endpoint.context = lambda: f"{endpoint.ticks} PC={system.c64.cpu.pc:04x} A5={system.peek_ram(0xa5):02x} A4={system.peek_ram(0xa4):02x}"
        system.bus = system.c64.bus = system.drive.bus = bus
        cia_input = system.c64.cia2.port_a_in
        via_input = system.drive.via1.port_b_in
        system.c64.cia2.port_a_in = lambda: (bus.advance(), cia_input())[1]
        system.drive.via1.port_b_in = lambda: (bus.advance(), via_input())[1]
        result = load(system, bus, "HELLO.PRG")
        actual = system.ram_bytes(0x4000, 0x4030)
        if result["carry"] or actual != payload:
            raise AssertionError(f"{label}: LOAD payload mismatch corrupted={endpoint.corrupted} {result}, expected={payload.hex()} got={actual.hex()}")
        passed("LOAD PRG exact48bytes")
        if corrupt:
            raise AssertionError("negative control unexpectedly passed")
        result = load(system, bus, "$")
        listing = system.ram_bytes(0x4000, result["end"])
        assert not result["carry"] and b"HELLO" in listing and b"SUB" in listing, (label, result, listing)
        passed("LOAD directory exposes file and subfolder")
        saved = bytes((i * 17 + 3) & 255 for i in range(37))
        for i, byte in enumerate(saved): system.poke(0x4000 + i, byte)
        code = setup_file(system, "SAVED", 9, 1, 1)
        code.extend(bytes.fromhex("A9 00 85 FB A9 40 85 FC A9 FB"))
        code.extend((0xA2, 37, 0xA0, 0x40)); emit_call(code, 0xFFD8)
        result = execute(system, bus, code)
        assert not result["carry"], (label, "SAVE", result)
        saved_files = [p for p in media.iterdir() if p.is_file() and p.name.startswith("SAVED")]
        assert len(saved_files) == 1 and saved_files[0].read_bytes() == b"\x00\x40" + saved
        assert not load(system, bus, saved_files[0].name)["carry"]
        assert system.ram_bytes(0x4000, 0x4000 + len(saved)) == saved
        passed("SAVE and LOAD roundtrip exact37bytes plus load address")
        assert not command(system, bus, "CD:SUB")["carry"]
        assert not load(system, bus, "NESTED.PRG")["carry"]
        assert system.ram_bytes(0x4000, 0x400B) == b"nested-file"
        passed("command channel CD and nested LOAD")
        status = read_status(system, bus)
        assert status.startswith("00,"), status
        result = load(system, bus, "MISSING.PRG")
        status = read_status(system, bus)
        assert result["carry"] and status.startswith("62,"), (result, status)
        passed("missing file returns carry and DOS62 status")
        result = load(system, bus, "HELLO.PRG", 10)
        assert result["carry"] and result["a"] == 5, (label, "absent device10", result)
        passed("absent device10 does not receive a device9 frame acknowledgement")
        assert not command(system, bus, "CD:/")["carry"]
        assert not load(system, bus, "DRIVE8", 8)["carry"]
        assert system.ram_bytes(0x4000, 0x400C) == b"device-eight"
        passed("device8 LOAD with device9 present")
        assert backend.test_iec_finish() == 0
        return {"case": label, "checks": checks, "events": endpoint.events,
                "replies": endpoint.replies, "cpu_cycles": system.c64.cpu.processorCycles,
                "badline_stalls": system.c64.badline_stalls, "badlines_enabled": badlines}
    finally:
        endpoint.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-iec-kernal")
    parser.add_argument("--rom-dir", type=Path, default=ROOT / "build/Multitini-070dev6/sd/multitini/cores/c64/roms")
    parser.add_argument("--cases", default="pal-stock,pal-fast,ntsc-stock,ntsc-fast")
    parser.add_argument("--no-badlines", action="store_true", help="Diagnostic control only: disable coarse instruction-boundary VIC stalls")
    args = parser.parse_args()
    build = args.build_dir.resolve(); roms = args.rom_dir.resolve()
    if not build.is_relative_to(ROOT / "build") or build == ROOT / "build" or build.exists():
        raise ValueError("use a fresh dedicated directory below repository build/")
    cases = args.cases.split(",")
    if not cases or len(set(cases)) != len(cases) or any(c not in ("pal-stock", "pal-fast", "ntsc-stock", "ntsc-fast") for c in cases):
        raise ValueError("unsupported, repeated or empty cases")
    build.mkdir(parents=True)
    sources = [Path(__file__).resolve(), GUARD, ROOT / "hdl/sim/tb_c64_iec9_rpc.sv",
               ROOT / "hdl/core/c64/multitini_c64_iec9.sv", ROOT / "tests/host/c64_iec_kernal_backend.c",
               ROOT / "firmware/common/c64_iec_dos.c", ROOT / "firmware/include/multitini_c64_iec_dos.h",
               ROOT / "scripts/private_core_roms.py",
               *sorted((ROOT / "scripts/c64sim").glob("*.py"))]
    before = {p.relative_to(ROOT).as_posix(): digest(p) for p in sources}
    receipt = {"schema": 1, "status": "FAIL", "source_sha256": before,
               "external_dependencies": {"py65": importlib.metadata.version("py65")},
               "badlines_enabled": not args.no_badlines,
               "private_rom_sha256": {str(p.relative_to(roms)): digest(p) for p in roms.rglob("*.bin")},
               "cases": [], "limits": ["C64/1541 CPUs and CIAs use existing instruction-boundary c64sim models; this is not a cycle-exact full-machine RTL or board test.",
                   "Optional badline stalls use the existing coarse instruction-boundary model; native VIC bus-stall timing requires a separate full-machine gate.",
                   "Production IEC endpoint and portable DOS engine are exercised; test event adapter and host filesystem callbacks replace the ARM MMIO/CDC/FatFs service.",
                   "Private unmodified user ROMs are loaded at runtime and are not redistributed."]}
    try:
        for ntsc in (False, True):
            name = "ntsc" if ntsc else "pal"
            native([UCRT / "iverilog.exe", "-g2012", "-s", "tb_c64_iec9_rpc",
                    f"-Ptb_c64_iec9_rpc.CLOCK_HZ={32727273 if ntsc else 31524725}", "-o", build / (name + ".vvp"),
                    ROOT / "hdl/core/c64/multitini_c64_iec9.sv", ROOT / "hdl/sim/tb_c64_iec9_rpc.sv"], build, build / (name + "-compile.log"))
        native([UCRT / "gcc.exe", "-shared", "-O2", "-Wall", "-Wextra", "-I", ROOT / "firmware/include",
                ROOT / "tests/host/c64_iec_kernal_backend.c", ROOT / "firmware/common/c64_iec_dos.c",
                "-o", build / "backend.dll"], build, build / "backend-compile.log")
        dll_dir = os.add_dll_directory(str(UCRT)) if os.name == "nt" else None
        backend = ctypes.CDLL(str(build / "backend.dll"))
        backend.test_iec_init.argtypes = [ctypes.c_char_p]
        backend.test_iec_event.argtypes = [ctypes.c_uint32]
        backend.test_iec_event.restype = ctypes.c_uint32
        for name in cases:
            result = run_case(build, roms, backend, fast=name.endswith("fast"), ntsc=name.startswith("ntsc"), badlines=not args.no_badlines)
            receipt["cases"].append(result)
            print("PASS:", name, len(result["checks"]), "checks", flush=True)
        try:
            run_case(build, roms, backend, corrupt=True, badlines=not args.no_badlines)
        except AssertionError as error:
            if "LOAD payload mismatch corrupted=True" not in str(error): raise
            receipt["negative_control"] = {"status": "PASS", "rejection": str(error)}
        else:
            raise RuntimeError("corrupted reply was not rejected")
        after = {p.relative_to(ROOT).as_posix(): digest(p) for p in sources}
        if before != after: raise RuntimeError("qualification sources changed during test")
        receipt["sources_unchanged"] = True
        receipt["status"] = "PASS"
    except Exception as error:
        receipt["error"] = repr(error)
        raise
    finally:
        receipt["artifacts_sha256"] = {
            p.relative_to(build).as_posix(): digest(p) for p in sorted(build.rglob("*"))
            if p.is_file() and (p.suffix == ".log" or p.name == "progress.json")}
        (build / "validation.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(build / "validation.json")


if __name__ == "__main__":
    main()
