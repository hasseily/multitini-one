# SPDX-License-Identifier: GPL-3.0-only
"""py65 MPU subclasses with fast memory paths.

Plain RAM and ROM reads index the memory list directly; only I/O pages
and banked regions go through the machine's slow handlers. The overrides
copy py65's own instruction semantics and only change where the memory
access lands.
"""

from __future__ import annotations

from py65.devices.mpu6502 import MPU


class SimCPU(MPU):
    """Common overrides: operand fetches never touch I/O."""

    def __init__(self, memory):
        super().__init__(memory=memory, pc=None)
        self.p |= self.INTERRUPT

    def ImmediateByte(self):
        return self.memory[self.pc]

    def ZeroPageAddr(self):
        return self.memory[self.pc]

    def ZeroPageXAddr(self):
        return 0xFF & (self.x + self.memory[self.pc])

    def ZeroPageYAddr(self):
        return 0xFF & (self.y + self.memory[self.pc])

    def WordAt(self, addr):
        memory = self.memory
        return memory[addr] + (memory[(addr + 1) & 0xFFFF] << 8)

    def write(self, addr, value):  # pragma: no cover - replaced by subclasses
        self.memory[addr] = value

    def opSTA(self, x):
        self.write(x(), self.a)

    def opSTY(self, x):
        self.write(x(), self.y)

    def opSTX(self, y):
        self.write(y(), self.x)

    def opASL(self, x):
        if x is None:
            tbyte = self.a
        else:
            addr = x()
            tbyte = self.ByteAt(addr)
        self.p &= ~(self.CARRY | self.NEGATIVE | self.ZERO)
        if tbyte & self.NEGATIVE:
            self.p |= self.CARRY
        tbyte = (tbyte << 1) & 0xFF
        if tbyte:
            self.p |= tbyte & self.NEGATIVE
        else:
            self.p |= self.ZERO
        if x is None:
            self.a = tbyte
        else:
            self.write(addr, tbyte)

    def opLSR(self, x):
        if x is None:
            tbyte = self.a
        else:
            addr = x()
            tbyte = self.ByteAt(addr)
        self.p &= ~(self.CARRY | self.NEGATIVE | self.ZERO)
        self.p |= tbyte & 1
        tbyte = tbyte >> 1
        if not tbyte:
            self.p |= self.ZERO
        if x is None:
            self.a = tbyte
        else:
            self.write(addr, tbyte)

    def opROL(self, x):
        if x is None:
            tbyte = self.a
        else:
            addr = x()
            tbyte = self.ByteAt(addr)
        if self.p & self.CARRY:
            if not tbyte & self.NEGATIVE:
                self.p &= ~self.CARRY
            tbyte = (tbyte << 1) | 1
        else:
            if tbyte & self.NEGATIVE:
                self.p |= self.CARRY
            tbyte = tbyte << 1
        tbyte &= 0xFF
        self.FlagsNZ(tbyte)
        if x is None:
            self.a = tbyte
        else:
            self.write(addr, tbyte)

    def opROR(self, x):
        if x is None:
            tbyte = self.a
        else:
            addr = x()
            tbyte = self.ByteAt(addr)
        if self.p & self.CARRY:
            if not tbyte & 1:
                self.p &= ~self.CARRY
            tbyte = (tbyte >> 1) | self.NEGATIVE
        else:
            if tbyte & 1:
                self.p |= self.CARRY
            tbyte = tbyte >> 1
        self.FlagsNZ(tbyte)
        if x is None:
            self.a = tbyte
        else:
            self.write(addr, tbyte)

    def opDECR(self, x):
        if x is None:
            tbyte = self.a
        else:
            addr = x()
            tbyte = self.ByteAt(addr)
        self.p &= ~(self.ZERO | self.NEGATIVE)
        tbyte = (tbyte - 1) & 0xFF
        if tbyte:
            self.p |= tbyte & self.NEGATIVE
        else:
            self.p |= self.ZERO
        if x is None:
            self.a = tbyte
        else:
            self.write(addr, tbyte)

    def opINCR(self, x):
        if x is None:
            tbyte = self.a
        else:
            addr = x()
            tbyte = self.ByteAt(addr)
        self.p &= ~(self.ZERO | self.NEGATIVE)
        tbyte = (tbyte + 1) & 0xFF
        if tbyte:
            self.p |= tbyte & self.NEGATIVE
        else:
            self.p |= self.ZERO
        if x is None:
            self.a = tbyte
        else:
            self.write(addr, tbyte)


class C64CPU(SimCPU):
    """6510 core: $D000-$DFFF goes to I/O when banked in, stores above
    $A000 and to $00/$01 go through the machine (banking, ROM shadow)."""

    def __init__(self, machine, memory):
        self.machine = machine
        self.io_page = -1
        self.io_read = machine.io_read
        self.slow_write = machine.slow_write
        super().__init__(memory)

    def ByteAt(self, addr):
        if (addr & 0xF000) == self.io_page:
            return self.io_read(addr)
        return self.memory[addr]

    def write(self, addr, value):
        if 1 < addr < 0xA000:
            self.memory[addr] = value
        else:
            self.slow_write(addr, value)

    def opSTA(self, x):
        addr = x()
        if 1 < addr < 0xA000:
            self.memory[addr] = self.a
        else:
            self.slow_write(addr, self.a)

    def opSTX(self, y):
        addr = y()
        if 1 < addr < 0xA000:
            self.memory[addr] = self.x
        else:
            self.slow_write(addr, self.x)

    def opSTY(self, x):
        addr = x()
        if 1 < addr < 0xA000:
            self.memory[addr] = self.y
        else:
            self.slow_write(addr, self.y)


class DriveCPU(SimCPU):
    """1541 core: $0000-$07FF RAM and $8000-$FFFF ROM (or extram) are
    direct; VIA pages, RAM mirrors and unmapped space go through the drive."""

    def __init__(self, machine, memory):
        self.machine = machine
        self.slow_read = machine.slow_read
        self.slow_write = machine.slow_write
        super().__init__(memory)

    def ByteAt(self, addr):
        if addr < 0x800 or addr >= 0x8000:
            return self.memory[addr]
        return self.slow_read(addr)

    def write(self, addr, value):
        if addr < 0x800:
            self.memory[addr] = value
        else:
            self.slow_write(addr, value)

    def opSTA(self, x):
        addr = x()
        if addr < 0x800:
            self.memory[addr] = self.a
        else:
            self.slow_write(addr, self.a)

    def opSTX(self, y):
        addr = y()
        if addr < 0x800:
            self.memory[addr] = self.x
        else:
            self.slow_write(addr, self.x)

    def opSTY(self, x):
        addr = x()
        if addr < 0x800:
            self.memory[addr] = self.y
        else:
            self.slow_write(addr, self.y)
