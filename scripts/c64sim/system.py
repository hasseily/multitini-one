# SPDX-License-Identifier: GPL-3.0-only
"""Lockstep C64 + 1541 system harness."""

from __future__ import annotations

import collections
import copy
import time
from pathlib import Path

from .c64 import C64, KEY_RUN_STOP
from .d64 import D64
from .drive import Drive1541
from .gcr import GCRDisk
from .iec import IECBus

ROOT = Path(__file__).resolve().parents[2]
ROM_DIR = ROOT / "third_party" / "c64" / "ROMs"
DEFAULT_BASIC = ROM_DIR / "basic.901226-01.bin"
DEFAULT_KERNAL = ROM_DIR / "kernal.901227-03.bin"
DEFAULT_CHARGEN = ROM_DIR / "characters.901225-01.bin"
DEFAULT_DRIVE_ROM = ROM_DIR / "1541-II.251968-03.bin"

PAL_HZ = 985248
NTSC_HZ = 1022727
PAL_CYCLE_NS = 1015
NTSC_CYCLE_NS = 978
DRIVE_CYCLE_NS = 1000
DRIVE_HZ = 1000000

# KERNAL screen editor loop that waits for a key in the keyboard buffer.
KERNAL_WAIT_FOR_KEY = 0xE5CD
KEYBOARD_BUFFER = 0x0277
KEYBOARD_COUNT = 0xC6
KEYBOARD_SIZE = 0x0289


class SimTimeout(Exception):
    """The simulation reached its time budget before the stop condition."""


def _load(path, override, size):
    if override is not None:
        data = bytes(override)
    else:
        data = Path(path).read_bytes()
    if len(data) != size:
        raise ValueError("%s: expected %d bytes, got %d" % (path, size, len(data)))
    return data


def petscii(text: str) -> bytes:
    """ASCII to PETSCII for the upper case/graphics character set."""
    out = bytearray()
    for ch in text:
        code = ord(ch)
        if 0x61 <= code <= 0x7A:
            code -= 0x20
        elif ch == "\n":
            code = 0x0D
        elif code > 0x7F:
            raise ValueError("non-ASCII character %r" % ch)
        out.append(code)
    return bytes(out)


class System:
    def __init__(self, kernal_path=DEFAULT_KERNAL, basic_path=DEFAULT_BASIC,
                 chargen_path=DEFAULT_CHARGEN, drive_rom_path=DEFAULT_DRIVE_ROM,
                 d64=None, pal: bool = True, badlines: bool = True,
                 parallel_cable: bool = False, extram: bool = False,
                 drive_rom_bytes=None, kernal_bytes=None, basic_bytes=None,
                 chargen_bytes=None, device: int = 8, write_protected: bool = False):
        basic = _load(basic_path, basic_bytes, 8192)
        kernal = _load(kernal_path, kernal_bytes, 8192)
        chargen = _load(chargen_path, chargen_bytes, 4096)
        drive_rom = _load(drive_rom_path, drive_rom_bytes, 16384)

        self.pal = pal
        self.c64_hz = PAL_HZ if pal else NTSC_HZ
        self.c64_cycle_ns = PAL_CYCLE_NS if pal else NTSC_CYCLE_NS
        self.bus = IECBus()
        self.bus.time_fn = self.time_ns
        self.c64 = C64(basic, kernal, chargen, self.bus, pal=pal, badlines=badlines,
                       parallel_cable=parallel_cable)
        disk = None
        if d64 is not None:
            if isinstance(d64, GCRDisk):
                disk = d64
            else:
                if not isinstance(d64, D64):
                    d64 = D64.load(d64)
                disk = GCRDisk.from_d64(d64, write_protected=write_protected)
        self.drive = Drive1541(drive_rom, self.bus, device=device, parallel_cable=parallel_cable,
                               extram=extram, disk=disk)
        self.bus.on_flag2 = self.c64.cia2.set_flag

        self.pending_keys = bytearray()
        self._stop_at_ready = False
        self.stop_reason = None
        self.wall_seconds = 0.0
        self.c64.breakpoints[KERNAL_WAIT_FOR_KEY] = self._ready_hook

    # Time -------------------------------------------------------------------

    def time_ns(self) -> int:
        return self.c64.cpu.processorCycles * self.c64_cycle_ns

    def sim_seconds(self) -> float:
        return self.c64.cpu.processorCycles / self.c64_hz

    # Main loop --------------------------------------------------------------

    def run(self, max_seconds: float) -> str:
        """Run both CPUs in lockstep until a breakpoint stops it or the
        budget of simulated seconds is used; returns the stop reason."""
        c64 = self.c64
        drive = self.drive
        cm = c64.cpu
        dm = drive.cpu
        cstep = cm.step
        dstep = dm.step
        c64_bp = c64.breakpoints
        drive_bp = drive.breakpoints
        c64_trace = c64.trace
        drive_trace = drive.trace
        c64_ns = self.c64_cycle_ns
        drive_ns = DRIVE_CYCLE_NS
        end = cm.processorCycles + int(max_seconds * self.c64_hz)
        c64_count = 0
        drive_count = 0
        self.stop_reason = None
        wall_start = time.perf_counter()
        try:
            while True:
                cc = cm.processorCycles
                if cc * c64_ns <= dm.processorCycles * drive_ns:
                    if cc >= end:
                        self.stop_reason = "timeout"
                        return "timeout"
                    pc = cm.pc
                    if pc in c64_bp:
                        if c64_bp[pc](self):
                            self.stop_reason = "breakpoint"
                            return "breakpoint"
                    if c64_trace is not None:
                        c64_trace.append((pc, cm.a, cm.x, cm.y, cm.sp, cm.p, cc))
                    if c64.irq_line and not cm.p & 4:
                        cm.irq()
                    elif c64.nmi_pending:
                        c64.nmi_pending = False
                        cm.nmi()
                    cstep()
                    c64_count += 1
                    if cm.processorCycles >= c64.next_event:
                        c64.run_events()
                else:
                    pc = dm.pc
                    if drive_bp and pc in drive_bp:
                        if drive_bp[pc](self):
                            self.stop_reason = "drive-breakpoint"
                            return "drive-breakpoint"
                    if drive_trace is not None:
                        drive_trace.append((pc, dm.a, dm.x, dm.y, dm.sp, dm.p, dm.processorCycles))
                    if drive.irq_line and not dm.p & 4:
                        dm.irq()
                    dstep()
                    drive_count += 1
                    if dm.processorCycles >= drive.next_event:
                        drive.run_events()
        finally:
            self.wall_seconds += time.perf_counter() - wall_start
            c64.instructions += c64_count
            drive.instructions += drive_count

    def run_for(self, seconds: float) -> str:
        self._stop_at_ready = False
        return self.run(seconds)

    def run_until_pc(self, cpu: str, pc: int, max_sim_seconds: float = 10.0) -> float:
        """Run until the C64 ("c64") or drive ("drive") CPU is about to
        execute the instruction at pc; returns the simulated seconds used."""
        target = self.c64 if cpu == "c64" else self.drive
        previous = target.breakpoints.get(pc)
        target.breakpoints[pc] = lambda system: True
        start = self.sim_seconds()
        try:
            reason = self.run_for(max_sim_seconds)
        finally:
            if previous is None:
                target.breakpoints.pop(pc, None)
            else:
                target.breakpoints[pc] = previous
        if reason not in ("breakpoint", "drive-breakpoint"):
            raise SimTimeout("%s PC %04X not reached within %.1f simulated seconds" % (
                cpu, pc, max_sim_seconds))
        return self.sim_seconds() - start

    def run_until(self, condition, max_sim_seconds: float = 10.0, interval: float = 0.001) -> float:
        """Run in slices of `interval` simulated seconds until
        condition(system) is true; returns the simulated seconds used."""
        start = self.sim_seconds()
        deadline = start + max_sim_seconds
        while not condition(self):
            if self.sim_seconds() >= deadline:
                raise SimTimeout("condition not met within %.1f simulated seconds" % max_sim_seconds)
            self.run_for(min(interval, deadline - self.sim_seconds()))
        return self.sim_seconds() - start

    def _run_to_ready(self, max_sim_seconds: float) -> float:
        start = self.sim_seconds()
        self._stop_at_ready = True
        try:
            reason = self.run(max_sim_seconds)
        finally:
            self._stop_at_ready = False
        if reason != "breakpoint":
            raise SimTimeout("READY prompt not reached within %.1f simulated seconds" % max_sim_seconds)
        return self.sim_seconds() - start

    def boot_to_ready(self, max_sim_seconds: float = 5.0) -> float:
        """Run from reset until the KERNAL waits for a key; returns sim seconds."""
        return self._run_to_ready(max_sim_seconds)

    def run_until_ready(self, max_sim_seconds: float = 120.0) -> float:
        """Run until the typed lines are consumed and READY. is back."""
        return self._run_to_ready(max_sim_seconds)

    def _ready_hook(self, system) -> bool:
        mem = self.c64.mem
        pending = self.pending_keys
        if pending:
            count = mem[KEYBOARD_COUNT]
            limit = mem[KEYBOARD_SIZE] or 10
            while pending and count < limit:
                mem[KEYBOARD_BUFFER + count] = pending.pop(0)
                count += 1
            mem[KEYBOARD_COUNT] = count
            return False
        return self._stop_at_ready and mem[KEYBOARD_COUNT] == 0

    # Keyboard ---------------------------------------------------------------

    def type_line(self, text: str) -> None:
        """Queue a line of text plus RETURN for the keyboard buffer."""
        self.pending_keys += petscii(text) + b"\x0d"

    def type_keys(self, data: bytes) -> None:
        self.pending_keys += bytes(data)

    def press_stop(self) -> None:
        self.c64.press_key(*KEY_RUN_STOP)

    def release_keys(self) -> None:
        self.c64.release_all_keys()

    # Memory helpers ---------------------------------------------------------

    def peek(self, addr: int) -> int:
        """Read C64 memory as the CPU sees it, without I/O side effects."""
        if (addr & 0xF000) == self.c64.io_page:
            return self.c64.peek_ram(addr)
        return self.c64.mem[addr]

    def peek_ram(self, addr: int) -> int:
        return self.c64.peek_ram(addr)

    def poke(self, addr: int, value: int) -> None:
        self.c64.poke_ram(addr, value)

    def peek_word(self, addr: int) -> int:
        return self.peek_ram(addr) | (self.peek_ram(addr + 1) << 8)

    def ram_bytes(self, start: int, end: int) -> bytes:
        return self.c64.ram_bytes(start, end)

    def screen_text(self) -> list:
        return self.c64.screen_text()

    def screen_contains(self, text: str) -> bool:
        return any(text in line for line in self.screen_text())

    # Disk helpers -----------------------------------------------------------

    def drive_extram(self) -> bytes:
        """The drive's 8 KiB RAM at $8000 (empty when extram is off)."""
        return self.drive.extram_bytes

    def disk_image(self) -> D64:
        """Decode the drive's raw GCR tracks back into a D64."""
        if self.drive.disk is None:
            raise ValueError("no disk in the drive")
        return self.drive.disk.to_d64()

    def insert_disk(self, d64, write_protected: bool = False) -> None:
        if isinstance(d64, GCRDisk):
            disk = d64
        else:
            if not isinstance(d64, D64):
                d64 = D64.load(d64)
            disk = GCRDisk.from_d64(d64, write_protected=write_protected)
        self.drive.insert_disk(disk)

    # Debug helpers ----------------------------------------------------------

    def enable_bus_trace(self, enabled: bool = True) -> None:
        self.bus.enable_trace(enabled)

    def bus_trace(self, limit: int = 0) -> str:
        return self.bus.format_trace(limit)

    def enable_instruction_trace(self, cpu: str = "c64", depth: int = 4096) -> None:
        target = self.c64 if cpu == "c64" else self.drive
        target.trace = collections.deque(maxlen=depth)

    def disable_instruction_trace(self, cpu: str = "c64") -> None:
        target = self.c64 if cpu == "c64" else self.drive
        target.trace = None

    def format_instruction_trace(self, cpu: str = "c64", limit: int = 64) -> str:
        from py65.disassembler import Disassembler

        target = self.c64 if cpu == "c64" else self.drive
        if not target.trace:
            return ""
        disassembler = Disassembler(target.cpu)
        lines = []
        for pc, a, x, y, sp, p, cycles in list(target.trace)[-limit:]:
            _, text = disassembler.instruction_at(pc)
            lines.append("%10d %04X  %-14s A=%02X X=%02X Y=%02X SP=%02X P=%02X" % (
                cycles, pc, text, a, x, y, sp, p))
        return "\n".join(lines)

    def add_breakpoint(self, cpu: str, pc: int, callback=None) -> None:
        """callback(system) returns True to stop; default stops."""
        target = self.c64 if cpu == "c64" else self.drive
        target.breakpoints[pc] = callback or (lambda system: True)

    def remove_breakpoint(self, cpu: str, pc: int) -> None:
        target = self.c64 if cpu == "c64" else self.drive
        target.breakpoints.pop(pc, None)

    def snapshot(self) -> "System":
        return copy.deepcopy(self)

    def drive_state(self) -> dict:
        drive = self.drive
        return {
            "track": drive.track,
            "half_track": drive.half_track,
            "motor": drive.motor,
            "led": drive.led,
            "write_mode": drive.write_mode,
            "speed": drive.speed,
            "position": drive.position,
        }

    def stats(self) -> dict:
        c64_cycles = self.c64.cpu.processorCycles
        wall = self.wall_seconds
        return {
            "sim_seconds": self.sim_seconds(),
            "wall_seconds": wall,
            "c64_cycles": c64_cycles,
            "drive_cycles": self.drive.cpu.processorCycles,
            "c64_instructions": self.c64.instructions,
            "drive_instructions": self.drive.instructions,
            "c64_cycles_per_wall_second": c64_cycles / wall if wall else 0.0,
            "wall_per_sim_second": wall / self.sim_seconds() if c64_cycles else 0.0,
            "badline_stalls": self.c64.badline_stalls,
            "bus_transitions": self.bus.transitions,
            "disk_bytes_read": self.drive.bytes_read,
            "disk_bytes_written": self.drive.bytes_written,
        }
