# SPDX-License-Identifier: GPL-3.0-only
"""MOS 6522 VIA model mirroring iecdrv_via6522.vhd.

Timers are evaluated lazily from the drive cycle counter: the register
reads compute the current count and the underflow events are scheduled
as cycle numbers that the drive polls between instructions.
"""

from __future__ import annotations

INF = 1 << 62

IFR_CA2 = 0x01
IFR_CA1 = 0x02
IFR_SR = 0x04
IFR_CB2 = 0x08
IFR_CB1 = 0x10
IFR_T2 = 0x20
IFR_T1 = 0x40


class VIA6522:
    def __init__(self, name, port_a_in, port_b_in, on_write=None, on_irq=None,
                 on_ca2=None, on_cb2=None):
        self.name = name
        self.port_a_in = port_a_in
        self.port_b_in = port_b_in
        self.on_write = on_write
        self.on_irq = on_irq
        self.on_ca2 = on_ca2
        self.on_cb2 = on_cb2
        self.reset()

    def reset(self) -> None:
        self.pra = 0
        self.prb = 0
        self.ddra = 0
        self.ddrb = 0
        self.acr = 0
        self.pcr = 0
        self.ifr = 0
        self.ier = 0
        self.sr = 0xFF
        self.irq = False
        self.t1_latch = 0x5550
        self.t1_start = 0
        self.t1_value = 0x5550
        self.t1_armed = False
        self.t1_next = INF
        self.t2_latch_lo = 0x50
        self.t2_start = 0
        self.t2_value = 0x5550
        self.t2_armed = False
        self.t2_next = INF
        self.next_event = INF
        self.ca1 = 1
        self.cb1 = 1
        self.ca2_handshake = 1
        self.cb2_handshake = 1
        self.ca2_level = 1
        self.cb2_level = 1

    # Interrupts -----------------------------------------------------------

    def _update_irq(self) -> None:
        level = bool(self.ifr & self.ier & 0x7F)
        if level != self.irq:
            self.irq = level
            if self.on_irq is not None:
                self.on_irq(level)

    # Handshake lines ------------------------------------------------------

    def _ca2_output(self) -> int:
        mode = (self.pcr >> 1) & 7
        if mode == 4:
            return self.ca2_handshake
        if mode == 6:
            return 0
        return 1

    def _cb2_output(self) -> int:
        mode = (self.pcr >> 5) & 7
        if mode == 4:
            return self.cb2_handshake
        if mode == 6:
            return 0
        return 1

    def _refresh_ca2(self) -> None:
        level = self._ca2_output()
        if level != self.ca2_level:
            self.ca2_level = level
            if self.on_ca2 is not None:
                self.on_ca2(level)

    def _refresh_cb2(self) -> None:
        level = self._cb2_output()
        if level != self.cb2_level:
            self.cb2_level = level
            if self.on_cb2 is not None:
                self.on_cb2(level)

    def _ora_access(self) -> None:
        """Flag clearing and CA2 handshake/pulse on an ORA read or write."""
        self.ifr &= ~IFR_CA1
        if not self.pcr & 0x02:
            self.ifr &= ~IFR_CA2
        mode = (self.pcr >> 1) & 7
        if mode == 4:
            # Mirrors the VHDL: the ORA access drives CA2 high, the CA1
            # active edge drives it low.
            self.ca2_handshake = 1
            self._refresh_ca2()
        elif mode == 5 and self.on_ca2 is not None:
            self.on_ca2(0)
            self.on_ca2(1)

    def _orb_access(self) -> None:
        self.ifr &= ~IFR_CB1
        if not self.pcr & 0x20:
            self.ifr &= ~IFR_CB2
        mode = (self.pcr >> 5) & 7
        if mode == 4:
            self.cb2_handshake = 1
            self._refresh_cb2()
        elif mode == 5 and self.on_cb2 is not None:
            self.on_cb2(0)
            self.on_cb2(1)

    def set_ca1(self, level: int) -> None:
        old = self.ca1
        if level == old:
            return
        self.ca1 = level
        if old != (self.pcr & 1):
            self._ca1_event()

    def pulse_ca1(self) -> None:
        """A full pulse on CA1 contains both edges, so it always flags."""
        self._ca1_event()

    def _ca1_event(self) -> None:
        self.ifr |= IFR_CA1
        if (self.pcr >> 1) & 7 == 4:
            self.ca2_handshake = 0
            self._refresh_ca2()
        self._update_irq()

    def set_cb1(self, level: int) -> None:
        old = self.cb1
        if level == old:
            return
        self.cb1 = level
        if old != ((self.pcr >> 4) & 1):
            self.ifr |= IFR_CB1
            if (self.pcr >> 5) & 7 == 4:
                self.cb2_handshake = 0
                self._refresh_cb2()
            self._update_irq()

    # Timers ---------------------------------------------------------------

    def _t1_count(self, cycles: int) -> int:
        elapsed = cycles - self.t1_start
        if elapsed <= self.t1_value:
            return self.t1_value - elapsed
        period = self.t1_latch + 2
        rem = (elapsed - self.t1_value - 1) % period
        if rem == 0:
            return 0xFFFF
        return self.t1_latch - (rem - 1)

    def _t2_count(self, cycles: int) -> int:
        if self.acr & 0x20:
            return self.t2_value
        return (self.t2_value - (cycles - self.t2_start)) & 0xFFFF

    def _schedule(self) -> None:
        self.next_event = self.t1_next if self.t1_next < self.t2_next else self.t2_next

    def run_events(self, cycles: int) -> None:
        if cycles >= self.t1_next:
            if self.t1_armed:
                self.ifr |= IFR_T1
            if self.acr & 0x40:
                self.t1_start = self.t1_next + 1
                self.t1_value = self.t1_latch
                self.t1_next = self.t1_start + self.t1_latch + 1
            else:
                self.t1_armed = False
                self.t1_next = INF
        if cycles >= self.t2_next:
            if self.t2_armed:
                self.ifr |= IFR_T2
            self.t2_armed = False
            self.t2_next = INF
        self._schedule()
        self._update_irq()

    # Register access ------------------------------------------------------

    def read(self, reg: int, cycles: int) -> int:
        if reg == 0:
            value = (self.prb & self.ddrb) | (self.port_b_in() & ~self.ddrb & 0xFF)
            self._orb_access()
            self._update_irq()
            return value
        if reg == 1:
            value = self.port_a_in() & (self.pra | ~self.ddra & 0xFF)
            self._ora_access()
            self._update_irq()
            return value
        if reg == 2:
            return self.ddrb
        if reg == 3:
            return self.ddra
        if reg == 4:
            self.ifr &= ~IFR_T1
            self._update_irq()
            return self._t1_count(cycles) & 0xFF
        if reg == 5:
            return self._t1_count(cycles) >> 8
        if reg == 6:
            return self.t1_latch & 0xFF
        if reg == 7:
            return self.t1_latch >> 8
        if reg == 8:
            self.ifr &= ~IFR_T2
            self._update_irq()
            return self._t2_count(cycles) & 0xFF
        if reg == 9:
            return self._t2_count(cycles) >> 8
        if reg == 10:
            self.ifr &= ~IFR_SR
            self._update_irq()
            return self.sr
        if reg == 11:
            return self.acr
        if reg == 12:
            return self.pcr
        if reg == 13:
            return self.ifr | (0x80 if self.irq else 0)
        if reg == 14:
            return self.ier & 0x7F
        return self.port_a_in() & (self.pra | ~self.ddra & 0xFF)

    def write(self, reg: int, value: int, cycles: int) -> None:
        value &= 0xFF
        if reg == 0:
            self.prb = value
            self._orb_access()
        elif reg == 1:
            self.pra = value
            self._ora_access()
        elif reg == 2:
            self.ddrb = value
        elif reg == 3:
            self.ddra = value
        elif reg == 4 or reg == 6:
            self.t1_latch = (self.t1_latch & 0xFF00) | value
        elif reg == 5:
            self.t1_latch = (value << 8) | (self.t1_latch & 0xFF)
            self.t1_start = cycles
            self.t1_value = self.t1_latch
            self.t1_armed = True
            self.t1_next = cycles + self.t1_value + 1
            self.ifr &= ~IFR_T1
            self._schedule()
        elif reg == 7:
            self.t1_latch = (value << 8) | (self.t1_latch & 0xFF)
            self.ifr &= ~IFR_T1
        elif reg == 8:
            self.t2_latch_lo = value
        elif reg == 9:
            self.t2_start = cycles
            self.t2_value = (value << 8) | self.t2_latch_lo
            self.t2_armed = True
            self.t2_next = INF if self.acr & 0x20 else cycles + self.t2_value + 1
            self.ifr &= ~IFR_T2
            self._schedule()
        elif reg == 10:
            self.sr = value
            self.ifr &= ~IFR_SR
        elif reg == 11:
            if (value ^ self.acr) & 0x20:
                self.t2_value = self._t2_count(cycles)
                self.t2_start = cycles
                self.t2_next = INF if value & 0x20 else (
                    cycles + self.t2_value + 1 if self.t2_armed else INF)
                self._schedule()
            self.acr = value
        elif reg == 12:
            self.pcr = value
            self._refresh_ca2()
            self._refresh_cb2()
        elif reg == 13:
            self.ifr &= ~(value & 0x7F)
        elif reg == 14:
            if value & 0x80:
                self.ier |= value & 0x7F
            else:
                self.ier &= ~value & 0x7F
        else:
            self.pra = value
        self._update_irq()
        if self.on_write is not None and reg in (0, 1, 2, 3, 12, 15):
            self.on_write(reg, value)

    def port_a_output(self) -> int:
        """Pin state driven by port A (inputs float high)."""
        return (self.pra | ~self.ddra) & 0xFF

    def port_b_output(self) -> int:
        return (self.prb | ~self.ddrb) & 0xFF
