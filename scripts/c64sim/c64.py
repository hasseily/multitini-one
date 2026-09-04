# SPDX-License-Identifier: GPL-3.0-only
"""C64 machine model: 6510 banking, VIC-II timing, CIAs and keyboard."""

from __future__ import annotations

from .cia import CIA6526, INF
from .cpu import C64CPU

PAL_CYCLES_PER_LINE = 63
PAL_LINES = 312
NTSC_CYCLES_PER_LINE = 65
NTSC_LINES = 263
BADLINE_STALL = 40

REGION_BASIC = 0
REGION_D000 = 1
REGION_KERNAL = 2

# RUN/STOP sits at keyboard matrix column 7, row 7.
KEY_RUN_STOP = (7, 7)


class C64:
    def __init__(self, basic, kernal, chargen, bus, pal: bool = True, badlines: bool = True,
                 parallel_cable: bool = False):
        if len(basic) != 8192 or len(kernal) != 8192 or len(chargen) != 4096:
            raise ValueError("ROM sizes must be 8 KiB BASIC, 8 KiB KERNAL, 4 KiB chargen")
        self.basic = list(basic)
        self.kernal = list(kernal)
        self.chargen = list(chargen)
        self.bus = bus
        self.badlines = badlines
        self.parallel_cable = parallel_cable
        self.cycles_per_line = PAL_CYCLES_PER_LINE if pal else NTSC_CYCLES_PER_LINE
        self.lines = PAL_LINES if pal else NTSC_LINES
        self.frame_cycles = self.cycles_per_line * self.lines

        self.mem = [0] * 65536
        self.shadow = [0] * 65536
        self.color = [0] * 1024
        self.vic_regs = [0] * 64
        self.sid_regs = [0] * 32
        self.port_out = 0
        self.port_ddr = 0
        self.region_state = ["ram", "ram", "ram"]
        self.io_page = -1

        self.raster_compare = 0
        self.vic_irq_flags = 0
        self.vic_irq_mask = 0
        self.next_raster = INF
        self.next_badline = INF
        self.next_event = INF
        self.badline_stalls = 0

        self.key_matrix = [0] * 8
        self.keys_down = 0
        self.irq_line = False
        self.nmi_pending = False

        self.cia1 = CIA6526("CIA1", self._cia1_pa_in, self._cia1_pb_in, on_irq=self._cia1_irq)
        self.cia2 = CIA6526("CIA2", self._cia2_pa_in, self._cia2_pb_in,
                            on_write=self._cia2_write, on_irq=self._cia2_irq,
                            on_pb_access=self._cia2_pb_access)

        self.breakpoints = {}
        self.trace = None
        self.instructions = 0

        self._apply_banking()
        self.cpu = C64CPU(self, self.mem)
        self.cpu.io_page = self.io_page
        self._cia2_write(0, 0)

    # Banking ----------------------------------------------------------------

    def _port_value(self) -> int:
        return (self.port_out & self.port_ddr) | (0x17 & ~self.port_ddr & 0xFF)

    def _set_region(self, region: int, start: int, size: int, new: str, rom=None) -> None:
        current = self.region_state[region]
        if current == new:
            return
        mem = self.mem
        shadow = self.shadow
        end = start + size
        if current == "ram":
            shadow[start:end] = mem[start:end]
        if new == "ram":
            mem[start:end] = shadow[start:end]
        elif rom is not None:
            mem[start:end] = rom
        self.region_state[region] = new

    def _apply_banking(self) -> None:
        bits = self._port_value() & 7
        loram = bits & 1
        hiram = bits & 2
        charen = bits & 4
        self._set_region(REGION_BASIC, 0xA000, 0x2000,
                         "rom" if (loram and hiram) else "ram", self.basic)
        self._set_region(REGION_KERNAL, 0xE000, 0x2000,
                         "rom" if hiram else "ram", self.kernal)
        if loram or hiram:
            if charen:
                self._set_region(REGION_D000, 0xD000, 0x1000, "io")
            else:
                self._set_region(REGION_D000, 0xD000, 0x1000, "chargen", self.chargen)
        else:
            self._set_region(REGION_D000, 0xD000, 0x1000, "ram")
        self.io_page = 0xD000 if self.region_state[REGION_D000] == "io" else -1
        self.mem[0] = self.port_ddr
        self.mem[1] = self._port_value()

    def slow_write(self, addr: int, value: int) -> None:
        if addr >= 0xE000:
            if self.region_state[REGION_KERNAL] == "ram":
                self.mem[addr] = value
            else:
                self.shadow[addr] = value
        elif addr >= 0xD000:
            state = self.region_state[REGION_D000]
            if state == "io":
                self.io_write(addr, value)
            elif state == "ram":
                self.mem[addr] = value
            else:
                self.shadow[addr] = value
        elif addr >= 0xC000:
            self.mem[addr] = value
        elif addr >= 0xA000:
            if self.region_state[REGION_BASIC] == "ram":
                self.mem[addr] = value
            else:
                self.shadow[addr] = value
        elif addr == 1:
            self.port_out = value
            self._apply_banking()
            self.cpu.io_page = self.io_page
        elif addr == 0:
            self.port_ddr = value
            self._apply_banking()
            self.cpu.io_page = self.io_page
        else:
            self.mem[addr] = value

    def peek_ram(self, addr: int) -> int:
        """Read RAM even where a ROM or I/O is banked in."""
        if addr >= 0xE000:
            region = REGION_KERNAL
        elif addr >= 0xD000:
            region = REGION_D000
        elif 0xA000 <= addr < 0xC000:
            region = REGION_BASIC
        else:
            return self.mem[addr]
        if self.region_state[region] == "ram":
            return self.mem[addr]
        return self.shadow[addr]

    def poke_ram(self, addr: int, value: int) -> None:
        if addr < 2:
            self.slow_write(addr, value)
            return
        if addr >= 0xE000:
            region = REGION_KERNAL
        elif addr >= 0xD000:
            region = REGION_D000
        elif 0xA000 <= addr < 0xC000:
            region = REGION_BASIC
        else:
            self.mem[addr] = value & 0xFF
            return
        if self.region_state[region] == "ram":
            self.mem[addr] = value & 0xFF
        else:
            self.shadow[addr] = value & 0xFF

    def ram_bytes(self, start: int, end: int) -> bytes:
        return bytes(self.peek_ram(a) for a in range(start, end))

    # I/O ---------------------------------------------------------------------

    def io_read(self, addr: int) -> int:
        page = (addr >> 8) & 0xF
        if page < 4:
            return self._vic_read(addr & 0x3F)
        if page < 8:
            reg = addr & 0x1F
            if reg == 0x1B or reg == 0x1C:
                return 0
            return self.sid_regs[reg]
        if page < 12:
            return self.color[addr & 0x3FF]
        if page == 12:
            return self.cia1.read(addr & 0xF, self.cpu.processorCycles)
        if page == 13:
            return self.cia2.read(addr & 0xF, self.cpu.processorCycles)
        return 0

    def io_write(self, addr: int, value: int) -> None:
        page = (addr >> 8) & 0xF
        if page < 4:
            self._vic_write(addr & 0x3F, value)
        elif page < 8:
            self.sid_regs[addr & 0x1F] = value
        elif page < 12:
            self.color[addr & 0x3FF] = value & 0x0F
        elif page == 12:
            self.cia1.write(addr & 0xF, value, self.cpu.processorCycles)
            self._schedule()
        elif page == 13:
            self.cia2.write(addr & 0xF, value, self.cpu.processorCycles)
            self._schedule()

    # VIC-II ----------------------------------------------------------------

    def raster_line(self) -> int:
        return (self.cpu.processorCycles // self.cycles_per_line) % self.lines

    def _vic_read(self, reg: int) -> int:
        regs = self.vic_regs
        if reg == 0x11:
            return (regs[0x11] & 0x7F) | ((self.raster_line() >> 1) & 0x80)
        if reg == 0x12:
            return self.raster_line() & 0xFF
        if reg == 0x13 or reg == 0x14:
            return 0
        if reg == 0x16:
            return regs[0x16] | 0xC0
        if reg == 0x18:
            return regs[0x18] | 0x01
        if reg == 0x19:
            pending = 0x80 if self.vic_irq_flags & self.vic_irq_mask else 0
            return self.vic_irq_flags | 0x70 | pending
        if reg == 0x1A:
            return self.vic_irq_mask | 0xF0
        if reg == 0x1E or reg == 0x1F:
            return 0
        if reg >= 0x2F:
            return 0xFF
        if reg >= 0x20:
            return regs[reg] | 0xF0
        return regs[reg]

    def _vic_write(self, reg: int, value: int) -> None:
        if reg >= 0x2F:
            return
        if reg == 0x19:
            self.vic_irq_flags &= ~value & 0x0F
            self._update_irq()
            return
        if reg == 0x1A:
            self.vic_irq_mask = value & 0x0F
            self._update_irq()
            return
        self.vic_regs[reg] = value
        if reg == 0x11:
            self.raster_compare = (self.raster_compare & 0xFF) | ((value & 0x80) << 1)
            self._schedule_raster()
            self._schedule_badline()
            self._schedule()
        elif reg == 0x12:
            self.raster_compare = (self.raster_compare & 0x100) | value
            self._schedule_raster()
            self._schedule()

    def _schedule_raster(self) -> None:
        if self.raster_compare >= self.lines:
            self.next_raster = INF
            return
        cycles = self.cpu.processorCycles
        frame_start = cycles - cycles % self.frame_cycles
        target = frame_start + self.raster_compare * self.cycles_per_line
        if target <= cycles:
            target += self.frame_cycles
        self.next_raster = target

    def _schedule_badline(self) -> None:
        den = self.vic_regs[0x11] & 0x10
        if not self.badlines or not den:
            self.next_badline = INF
            return
        yscroll = self.vic_regs[0x11] & 7
        cycles = self.cpu.processorCycles
        frame_start = cycles - cycles % self.frame_cycles
        line = (cycles - frame_start) // self.cycles_per_line
        for candidate in range(line + 1, 248):
            if candidate >= 48 and (candidate & 7) == yscroll:
                self.next_badline = frame_start + candidate * self.cycles_per_line
                return
        first = 48 + ((yscroll - 48) & 7)
        self.next_badline = frame_start + self.frame_cycles + first * self.cycles_per_line

    # Interrupts --------------------------------------------------------------

    def _update_irq(self) -> None:
        self.irq_line = self.cia1.irq or bool(self.vic_irq_flags & self.vic_irq_mask)

    def _cia1_irq(self, level: bool) -> None:
        self._update_irq()

    def _cia2_irq(self, level: bool) -> None:
        if level:
            self.nmi_pending = True

    def _schedule(self) -> None:
        nxt = self.cia1.next_event
        if self.cia2.next_event < nxt:
            nxt = self.cia2.next_event
        if self.next_raster < nxt:
            nxt = self.next_raster
        if self.next_badline < nxt:
            nxt = self.next_badline
        self.next_event = nxt

    def run_events(self) -> None:
        cpu = self.cpu
        while True:
            cycles = cpu.processorCycles
            if cycles >= self.cia1.next_event:
                self.cia1.run_events(cycles)
            if cycles >= self.cia2.next_event:
                self.cia2.run_events(cycles)
            if cycles >= self.next_raster:
                self.vic_irq_flags |= 0x01
                self.next_raster += self.frame_cycles
                self._update_irq()
            if cycles >= self.next_badline:
                cpu.processorCycles += BADLINE_STALL
                self.badline_stalls += 1
                self._schedule_badline()
            self._schedule()
            if self.next_event > cpu.processorCycles:
                return

    # CIA ports ---------------------------------------------------------------

    def _cia1_pa_in(self) -> int:
        return 0xFF

    def _cia1_pb_in(self) -> int:
        if not self.keys_down:
            return 0xFF
        columns = self.cia1.port_a_output()
        rows = 0
        matrix = self.key_matrix
        for column in range(8):
            if not columns & (1 << column):
                rows |= matrix[column]
        return ~rows & 0xFF

    def _cia2_pa_in(self) -> int:
        """External levels on CIA2 port A: IEC CLK/DATA on bits 6/7 and
        the drive's CA2 strobe on user-port pin PA2 (bit 2) when the
        parallel cable is attached; every other line floats high."""
        bus = self.bus
        value = 0x3F
        if self.parallel_cable and not bus.flag2:
            value &= ~0x04
        if not bus.clk_low:
            value |= 0x40
        if not bus.data_low:
            value |= 0x80
        return value

    def _cia2_pb_in(self) -> int:
        if self.parallel_cable:
            return self.bus.par
        return 0xFF

    def _cia2_write(self, reg: int, value: int) -> None:
        if reg == 0 or reg == 2:
            out = self.cia2.port_a_output()
            self.bus.update_c64(bool(out & 0x08), bool(out & 0x10), bool(out & 0x20))
        elif self.parallel_cable:
            self.bus.set_c64_par(self.cia2.port_b_output())

    def _cia2_pb_access(self) -> None:
        if self.parallel_cable or self.bus.trace is not None:
            self.bus.pulse_pc2()

    # Keyboard ----------------------------------------------------------------

    def press_key(self, column: int, row: int) -> None:
        mask = 1 << row
        if not self.key_matrix[column] & mask:
            self.key_matrix[column] |= mask
            self.keys_down += 1

    def release_key(self, column: int, row: int) -> None:
        mask = 1 << row
        if self.key_matrix[column] & mask:
            self.key_matrix[column] &= ~mask
            self.keys_down -= 1

    def release_all_keys(self) -> None:
        self.key_matrix = [0] * 8
        self.keys_down = 0

    # Screen ------------------------------------------------------------------

    def screen_text(self) -> list:
        base = self.mem[0x288] << 8
        rows = []
        for row in range(25):
            line = self.mem[base + row * 40:base + row * 40 + 40]
            rows.append("".join(SCREEN_CODES[code & 0x7F] for code in line))
        return rows


SCREEN_CODES = (
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    " !\"#$%&'()*+,-./0123456789:;<=>?"
    "-ABCDEFGHIJKLMNOPQRSTUVWXYZ+|+|+"
    " ###-|##|#|-#-###-#|-###-#|-##|-"
)
