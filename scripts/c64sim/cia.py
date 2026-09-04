# SPDX-License-Identifier: GPL-3.0-only
"""MOS 6526 CIA model mirroring third_party/c64/upstream/rtl/mos6526.v.

Timers are evaluated lazily from the C64 cycle counter. Timer B can
count phi2 cycles or timer A underflows; the CNT pin never toggles.
"""

from __future__ import annotations

INF = 1 << 62

ICR_TA = 0x01
ICR_TB = 0x02
ICR_TOD = 0x04
ICR_SP = 0x08
ICR_FLAG = 0x10


class CIATimer:
    __slots__ = ("latch", "value", "start", "running", "cr", "next", "count_ta")

    def __init__(self):
        self.latch = 0xFFFF
        self.value = 0xFFFF
        self.start = 0
        self.running = False
        self.cr = 0
        self.next = INF
        self.count_ta = False

    def current(self, cycles: int) -> int:
        if not self.running:
            return self.value
        elapsed = cycles - self.start
        if elapsed <= self.value:
            return self.value - elapsed
        return self.latch - ((elapsed - self.value - 1) % (self.latch + 1))

    def start_counting(self, cycles: int) -> None:
        self.start = cycles
        self.running = True
        self.next = cycles + self.value + 1

    def stop(self, cycles: int) -> None:
        self.value = self.current(cycles)
        self.running = False
        self.next = INF


class CIA6526:
    def __init__(self, name, port_a_in, port_b_in, on_write=None, on_irq=None,
                 on_pb_access=None):
        self.name = name
        self.port_a_in = port_a_in
        self.port_b_in = port_b_in
        self.on_write = on_write
        self.on_irq = on_irq
        self.on_pb_access = on_pb_access
        self.reset()

    def reset(self) -> None:
        self.pra = 0
        self.prb = 0
        self.ddra = 0
        self.ddrb = 0
        self.ta = CIATimer()
        self.tb = CIATimer()
        self.icr = 0
        self.imr = 0
        self.irq = False
        self.sdr = 0
        self.tod = [0, 0, 0, 1]
        self.tod_alarm = [0, 0, 0, 0]
        self.flag = 1
        self.next_event = INF

    # Interrupts -----------------------------------------------------------

    def _update_irq(self) -> None:
        if not self.irq and self.icr & self.imr:
            self.irq = True
            if self.on_irq is not None:
                self.on_irq(True)

    def set_flag(self, level: int) -> None:
        old = self.flag
        self.flag = level
        if old and not level:
            self.icr |= ICR_FLAG
            self._update_irq()

    def flag_edge(self) -> None:
        self.icr |= ICR_FLAG
        self._update_irq()

    # Timers ---------------------------------------------------------------

    def _schedule(self) -> None:
        self.next_event = self.ta.next if self.ta.next < self.tb.next else self.tb.next

    def _tb_underflow(self) -> None:
        tb = self.tb
        self.icr |= ICR_TB
        tb.value = tb.latch
        if tb.cr & 0x08:
            tb.cr &= ~0x01
            tb.count_ta = False
            tb.running = False
            tb.next = INF

    def run_events(self, cycles: int) -> None:
        ta = self.ta
        tb = self.tb
        while cycles >= ta.next:
            self.icr |= ICR_TA
            if ta.cr & 0x08:
                ta.cr &= ~0x01
                ta.running = False
                ta.value = ta.latch
                ta.next = INF
            else:
                ta.start = ta.next
                ta.value = ta.latch
                ta.next = ta.start + ta.latch + 1
            if tb.count_ta:
                tb.value -= 1
                if tb.value < 0:
                    self._tb_underflow()
        while cycles >= tb.next:
            self.icr |= ICR_TB
            if tb.cr & 0x08:
                tb.cr &= ~0x01
                tb.running = False
                tb.value = tb.latch
                tb.next = INF
            else:
                tb.start = tb.next
                tb.value = tb.latch
                tb.next = tb.start + tb.latch + 1
        self._schedule()
        self._update_irq()

    def _write_cr(self, timer: CIATimer, value: int, cycles: int, is_b: bool) -> None:
        if value & 0x10:
            if timer.running:
                timer.stop(cycles)
            timer.value = timer.latch
        if is_b:
            mode = (value >> 5) & 3
            phi2 = mode == 0
            count_ta = mode >= 2
        else:
            phi2 = not value & 0x20
            count_ta = False
        start = bool(value & 0x01)
        if start and phi2:
            if not timer.running:
                timer.start_counting(cycles)
        elif timer.running:
            timer.stop(cycles)
        timer.count_ta = count_ta and start
        timer.cr = value & ~0x10
        self._schedule()

    # Register access ------------------------------------------------------

    def read(self, reg: int, cycles: int) -> int:
        if reg == 0:
            return (self.pra | ~self.ddra) & self.port_a_in() & 0xFF
        if reg == 1:
            value = ((self.prb & self.ddrb) | (self.port_b_in() & ~self.ddrb)) & 0xFF
            if self.on_pb_access is not None:
                self.on_pb_access()
            return value
        if reg == 2:
            return self.ddra
        if reg == 3:
            return self.ddrb
        if reg == 4:
            return self.ta.current(cycles) & 0xFF
        if reg == 5:
            return self.ta.current(cycles) >> 8
        if reg == 6:
            return self.tb.current(cycles) & 0xFF
        if reg == 7:
            return self.tb.current(cycles) >> 8
        if reg < 12:
            return self.tod[reg - 8]
        if reg == 12:
            return self.sdr
        if reg == 13:
            value = self.icr | (0x80 if self.irq else 0)
            self.icr = 0
            if self.irq:
                self.irq = False
                if self.on_irq is not None:
                    self.on_irq(False)
            return value
        if reg == 14:
            return self.ta.cr
        return self.tb.cr

    def write(self, reg: int, value: int, cycles: int) -> None:
        value &= 0xFF
        if reg == 0:
            self.pra = value
        elif reg == 1:
            self.prb = value
            if self.on_pb_access is not None:
                self.on_pb_access()
        elif reg == 2:
            self.ddra = value
        elif reg == 3:
            self.ddrb = value
        elif reg == 4:
            self.ta.latch = (self.ta.latch & 0xFF00) | value
        elif reg == 5:
            self.ta.latch = (value << 8) | (self.ta.latch & 0xFF)
            if not self.ta.running:
                self.ta.value = self.ta.latch
        elif reg == 6:
            self.tb.latch = (self.tb.latch & 0xFF00) | value
        elif reg == 7:
            self.tb.latch = (value << 8) | (self.tb.latch & 0xFF)
            if not self.tb.running:
                self.tb.value = self.tb.latch
        elif reg < 12:
            if self.tb.cr & 0x80:
                self.tod_alarm[reg - 8] = value
            else:
                self.tod[reg - 8] = value
        elif reg == 12:
            self.sdr = value
        elif reg == 13:
            if value & 0x80:
                self.imr |= value & 0x1F
            else:
                self.imr &= ~value & 0x1F
            self._update_irq()
        elif reg == 14:
            self._write_cr(self.ta, value, cycles, False)
        else:
            self._write_cr(self.tb, value, cycles, True)
        if self.on_write is not None and reg < 4:
            self.on_write(reg, value)

    def port_a_output(self) -> int:
        return (self.pra | ~self.ddra) & 0xFF

    def port_b_output(self) -> int:
        return (self.prb | ~self.ddrb) & 0xFF
