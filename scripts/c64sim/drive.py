# SPDX-License-Identifier: GPL-3.0-only
"""1541-II drive model: memory decoding, two VIAs, IEC wiring and the
GCR read/write head, mirroring hdl/core/c64/ports/c1541_logic.sv and
c1541_gcr.sv."""

from __future__ import annotations

from .cpu import DriveCPU
from .gcr import MAX_HALF_TRACKS, ZONE_BYTE_PERIOD_US
from .via import INF, VIA6522

WPS_INSERT_PULSE_CYCLES = 200000


class Drive1541:
    def __init__(self, rom, bus, device: int = 8, parallel_cable: bool = False,
                 extram: bool = False, disk=None):
        if len(rom) != 16384:
            raise ValueError("1541 ROM must be 16 KiB")
        self.rom = list(rom)
        self.bus = bus
        self.parallel_cable = parallel_cable
        self.extram = extram
        self.device_bits = ((device - 8) & 3) << 5

        mem = [0xFF] * 65536
        mem[0:0x800] = [0] * 0x800
        mem[0x8000:0xC000] = self.rom
        mem[0xC000:0x10000] = self.rom
        if extram:
            mem[0x8000:0xA000] = [0] * 0x2000
        self.mem = mem

        self.disk = disk
        self.half_track = 34
        self.position = 0
        self.motor = False
        self.led = False
        self.stepper = 0
        self.speed = 0
        self.byte_period = ZONE_BYTE_PERIOD_US[0]
        self.write_mode = False
        self.soe = 1
        self.sync = False
        self.data_byte = 0
        self.next_byte = INF
        self.next_event = INF
        self.wps_pulse_until = 0
        self.bytes_read = 0
        self.bytes_written = 0
        self.irq_line = False

        self.via1 = VIA6522("VIA1", self._via1_pa_in, self._via1_pb_in,
                            on_write=self._via1_write, on_irq=self._irq_changed,
                            on_ca2=self._via1_ca2)
        self.via2 = VIA6522("VIA2", self._via2_pa_in, self._via2_pb_in,
                            on_write=self._via2_write, on_irq=self._irq_changed,
                            on_ca2=self._via2_ca2, on_cb2=self._via2_cb2)
        self.breakpoints = {}
        self.trace = None
        self.instructions = 0

        self.cpu = DriveCPU(self, mem)
        bus.on_atn = self._atn_changed
        bus.on_pc2 = self._pc2_pulse
        self._update_iec_outputs()
        self._update_mechanics()

    # Memory ---------------------------------------------------------------

    def slow_read(self, addr: int) -> int:
        select = addr & 0x1800
        if select == 0x1800:
            via = self.via2 if addr & 0x400 else self.via1
            return via.read(addr & 0xF, self.cpu.processorCycles)
        if select == 0:
            return self.mem[addr & 0x7FF]
        return 0xFF

    def slow_write(self, addr: int, value: int) -> None:
        if addr < 0x8000:
            select = addr & 0x1800
            if select == 0x1800:
                via = self.via2 if addr & 0x400 else self.via1
                via.write(addr & 0xF, value, self.cpu.processorCycles)
                self._schedule()
            elif select == 0:
                self.mem[addr & 0x7FF] = value
        elif self.extram and addr < 0xA000:
            self.mem[addr] = value

    def peek(self, addr: int) -> int:
        if addr < 0x800 or addr >= 0x8000 or (addr & 0x1800) == 0:
            return self.mem[addr if addr >= 0x8000 else addr & 0x7FF]
        return 0xFF

    # Properties -----------------------------------------------------------

    @property
    def track(self) -> float:
        return self.half_track / 2 + 1

    @property
    def extram_bytes(self) -> bytes:
        return bytes(self.mem[0x8000:0xA000]) if self.extram else b""

    def insert_disk(self, disk) -> None:
        """Mount a GCRDisk; the write-protect sensor pulses like a real insert."""
        self.disk = disk
        self.wps_pulse_until = self.cpu.processorCycles + WPS_INSERT_PULSE_CYCLES

    def remove_disk(self) -> None:
        self.disk = None
        self.wps_pulse_until = self.cpu.processorCycles + WPS_INSERT_PULSE_CYCLES

    # VIA1: serial bus and parallel port -----------------------------------

    def _via1_pa_in(self) -> int:
        return self.bus.par if self.parallel_cable else 0xFF

    def _via1_pb_in(self) -> int:
        bus = self.bus
        value = 0x1A | self.device_bits
        if bus.atn_low:
            value |= 0x80
        if bus.clk_low:
            value |= 0x04
        if bus.data_low:
            value |= 0x01
        return value

    def _via1_write(self, reg: int, value: int) -> None:
        if reg == 0 or reg == 2:
            self._update_iec_outputs()
        elif self.parallel_cable and reg in (1, 3, 15):
            self.bus.set_drive_par(self.via1.port_a_output())

    def _update_iec_outputs(self) -> None:
        out = self.via1.port_b_output()
        clk_low = bool(out & 0x08)
        atna = bool(out & 0x10)
        data_low = bool(out & 0x02) or (atna != self.bus.atn_low)
        self.bus.update_drive(clk_low, data_low)

    def _atn_changed(self, atn_low: bool) -> None:
        self.via1.set_ca1(1 if atn_low else 0)
        self._update_iec_outputs()
        self._schedule()

    def _pc2_pulse(self) -> None:
        if self.parallel_cable:
            self.via1.set_cb1(0)
            self.via1.set_cb1(1)
            self._schedule()

    def _via1_ca2(self, level: int) -> None:
        if self.parallel_cable:
            self.bus.set_flag2(level)

    # VIA2: drive mechanics ------------------------------------------------

    def _via2_pa_in(self) -> int:
        return self.data_byte

    def _via2_pb_in(self) -> int:
        value = 0x6F
        if not (self.motor and self.sync):
            value |= 0x80
        if self.cpu.processorCycles < self.wps_pulse_until:
            return value
        if self.disk is None or not self.disk.write_protected:
            value |= 0x10
        return value

    def _via2_write(self, reg: int, value: int) -> None:
        if reg == 0 or reg == 2:
            self._update_mechanics()

    def _via2_ca2(self, level: int) -> None:
        self.soe = level

    def _via2_cb2(self, level: int) -> None:
        write_mode = level == 0
        if write_mode != self.write_mode:
            self.write_mode = write_mode
            self.sync = False
            if self.motor:
                self.next_byte = self.cpu.processorCycles + self.byte_period
            self._schedule()

    def _update_mechanics(self) -> None:
        out = self.via2.port_b_output()
        phase = out & 3
        motor = bool(out & 4)
        self.led = bool(out & 8)
        speed = (out >> 5) & 3
        cycles = self.cpu.processorCycles
        if phase != self.stepper:
            step = (phase - self.stepper) & 3
            self.stepper = phase
            if step == 1 and self.half_track < MAX_HALF_TRACKS - 1:
                self._move_head(self.half_track + 1)
            elif step == 3 and self.half_track > 0:
                self._move_head(self.half_track - 1)
        if speed != self.speed:
            self.speed = speed
            self.byte_period = ZONE_BYTE_PERIOD_US[speed]
        if motor != self.motor:
            self.motor = motor
            self.sync = False
            self.next_byte = cycles + self.byte_period if motor else INF
        self._schedule()

    def _move_head(self, half: int) -> None:
        old = self._track_data()
        self.half_track = half
        new = self._track_data()
        if old is not None and new is not None and len(old) != len(new):
            self.position = self.position * len(new) // len(old)
        elif new is not None:
            self.position %= len(new)
        self.sync = False
        if self.motor:
            self.next_byte = self.cpu.processorCycles + self.byte_period

    def _track_data(self):
        if self.disk is None:
            return None
        return self.disk.tracks[self.half_track]

    def _byte_tick(self) -> None:
        track = self._track_data()
        if track is None:
            # No flux: the read amplifier produces noise; deliver zeros.
            self.sync = False
            self.data_byte = 0
        else:
            position = self.position + 1
            if position >= len(track):
                position = 0
            self.position = position
            if self.write_mode:
                track[position] = self.via2.port_a_output()
                self.sync = False
                self.bytes_written += 1
            else:
                byte = track[position]
                if byte == 0xFF and track[position - 1] == 0xFF:
                    self.sync = True
                    return
                self.sync = False
                self.data_byte = byte
                self.bytes_read += 1
        if self.soe:
            self.cpu.p |= 0x40
            self.via2.pulse_ca1()

    def _irq_changed(self, level: bool) -> None:
        self.irq_line = self.via1.irq or self.via2.irq

    def _schedule(self) -> None:
        nxt = self.next_byte
        if self.via1.next_event < nxt:
            nxt = self.via1.next_event
        if self.via2.next_event < nxt:
            nxt = self.via2.next_event
        self.next_event = nxt

    def run_events(self) -> None:
        cpu = self.cpu
        while True:
            cycles = cpu.processorCycles
            if cycles >= self.next_byte:
                self._byte_tick()
                self.next_byte += self.byte_period
            if cycles >= self.via1.next_event:
                self.via1.run_events(cycles)
            if cycles >= self.via2.next_event:
                self.via2.run_events(cycles)
            self._schedule()
            if self.next_event > cycles:
                return
