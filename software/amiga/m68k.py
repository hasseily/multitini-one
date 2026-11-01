"""Small original 68000 assembler for the Multitini guest helper.

This deliberately accepts only the instructions used by helper.py. It emits
named, checked PC-relative references and an instruction listing; it is not a
general assembler or a dependency on an external SDK.
SPDX-License-Identifier: GPL-3.0-only
"""
from dataclasses import dataclass
import re
import struct


@dataclass
class EA:
    mode: int
    reg: int
    extra: tuple = ()


class Assembler:
    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.fixups = []
        self.listing = []

    @property
    def pc(self):
        return len(self.code)

    def word(self, value):
        self.code.extend(struct.pack(">H", value & 0xffff))

    def long(self, value):
        self.code.extend(struct.pack(">I", value & 0xffffffff))

    def label(self, name):
        if name in self.labels:
            raise ValueError("Duplicate label: " + name)
        self.labels[name] = self.pc
        self.listing.append((self.pc, name + ":"))

    def data(self, data):
        self.code.extend(data)

    def align(self, alignment=2):
        self.code.extend(bytes((-self.pc) % alignment))

    def relative(self, name):
        self.fixups.append((self.pc, name))
        self.word(0)

    @staticmethod
    def number(text):
        return int(text.replace("$", "0x"), 0)

    def ea(self, operand, size):
        op = operand.strip().lower()
        if re.fullmatch(r"[da][0-7]", op):
            return EA(op[0] == "a", int(op[1]))
        if op == "sp":
            return EA(1, 7)
        if op.startswith("#"):
            value = self.number(op[1:])
            return EA(7, 4, (("long" if size == "l" else "word", value),))
        match = re.fullmatch(r"([\w]+)\(pc\)", op)
        if match:
            return EA(7, 2, (("relative", match[1]),))
        match = re.fullmatch(r"(-?(?:0x[0-9a-f]+|\d+))\(a([0-7])\)", op)
        if match:
            value = self.number(match[1])
            if not -32768 <= value <= 32767:
                raise ValueError("Address displacement outside signed word")
            return EA(5, int(match[2]), (("word", value),))
        match = re.fullmatch(r"\(a([0-7])\)(\+?)", op)
        if match:
            return EA(3 if match[2] else 2, int(match[1]))
        match = re.fullmatch(r"-\((a[0-7]|sp)\)", op)
        if match:
            return EA(4, 7 if match[1] == "sp" else int(match[1][1]))
        match = re.fullmatch(r"(.+)\.([wl])", op)
        if match:
            value = self.number(match[1])
            return EA(7, 0 if match[2] == "w" else 1,
                      (("word" if match[2] == "w" else "long", value),))
        raise ValueError("Unsupported effective address: " + operand)

    def extension(self, ea):
        for kind, value in ea.extra:
            getattr(self, kind)(value)

    def ins(self, mnemonic, *operands):
        start = self.pc
        text = mnemonic + (" " + ",".join(operands) if operands else "")
        pieces = mnemonic.lower().split(".")
        op = pieces[0]
        size = pieces[1] if len(pieces) == 2 else "w"
        sizebits = {"b": 0, "w": 0x40, "l": 0x80}[size]
        if op == "move":
            src, dst = [self.ea(v, size) for v in operands]
            if dst.mode == 7 and dst.reg >= 2 or size == "b" and (src.mode == 1 or dst.mode == 1):
                raise ValueError("Illegal MOVE effective address")
            self.word({"b": 0x1000, "l": 0x2000, "w": 0x3000}[size] |
                      dst.reg << 9 | dst.mode << 6 | src.mode << 3 | src.reg)
            self.extension(src)
            self.extension(dst)
        elif op == "moveq":
            value, reg = self.number(operands[0][1:]), int(operands[1][1])
            if not -128 <= value <= 127:
                raise ValueError("MOVEQ outside signed byte")
            self.word(0x7000 | reg << 9 | value & 255)
        elif op in ("lea", "jsr", "jmp"):
            ea = self.ea(operands[0], "l")
            base = {"lea": 0x41c0, "jsr": 0x4e80, "jmp": 0x4ec0}[op]
            if op == "lea":
                base |= int(operands[1][1]) << 9
            self.word(base | ea.mode << 3 | ea.reg)
            self.extension(ea)
        elif op in ("cmpi", "andi", "ori", "addi", "subi"):
            ea = self.ea(operands[1], size)
            self.word({"cmpi": 0x0c00, "andi": 0x0200, "ori": 0,
                       "addi": 0x0600, "subi": 0x0400}[op] | sizebits | ea.mode << 3 | ea.reg)
            self.extension(self.ea(operands[0], size))
            self.extension(ea)
        elif op in ("addq", "subq"):
            count = self.number(operands[0][1:])
            if not 1 <= count <= 8:
                raise ValueError("Quick count outside 1..8")
            ea = self.ea(operands[1], size)
            self.word(0x5000 | (count & 7) << 9 | (0x100 if op == "subq" else 0) |
                      sizebits | ea.mode << 3 | ea.reg)
            self.extension(ea)
        elif op in ("tst", "clr"):
            ea = self.ea(operands[0], size)
            self.word((0x4a00 if op == "tst" else 0x4200) | sizebits | ea.mode << 3 | ea.reg)
            self.extension(ea)
        elif op in ("cmp", "add", "sub"):
            ea = self.ea(operands[0], size)
            reg = int(operands[1][1])
            self.word({"cmp": 0xb000, "add": 0xd000, "sub": 0x9000}[op] |
                      reg << 9 | sizebits | ea.mode << 3 | ea.reg)
            self.extension(ea)
        elif op == "swap":
            self.word(0x4840 | int(operands[0][1]))
        elif op == "asr":
            if size != "w" or len(operands) != 2 or not re.fullmatch(r"d[0-7]", operands[1]):
                raise ValueError("ASR supports only immediate word shifts of a data register")
            count = self.number(operands[0][1:]) if operands[0].startswith("#") else 0
            if not 1 <= count <= 8:
                raise ValueError("ASR immediate count outside 1..8")
            self.word(0xe040 | (count & 7) << 9 | int(operands[1][1]))
        elif op == "dbra":
            self.word(0x51c8 | int(operands[0][1]))
            self.relative(operands[1])
        elif op in ("bra", "bsr", "bne", "beq", "bcs", "bcc", "bmi", "bpl", "bvs", "blt", "bgt"):
            self.word({"bra": 0x6000, "bsr": 0x6100, "bne": 0x6600, "beq": 0x6700,
                       "bcs": 0x6500, "bcc": 0x6400, "bmi": 0x6b00, "bpl": 0x6a00,
                       "bvs": 0x6900, "blt": 0x6d00, "bgt": 0x6e00}[op])
            self.relative(operands[0])
        elif op == "rts":
            self.word(0x4e75)
        elif op == "save":
            self.word(0x48e7)  # MOVEM.L D2-D7/A2-A6,-(SP)
            self.word(0x3f3e)
        elif op == "restore":
            self.word(0x4cdf)  # MOVEM.L (SP)+,D2-D7/A2-A6
            self.word(0x7cfc)
        else:
            raise ValueError("Unsupported instruction: " + mnemonic)
        self.listing.append((start, text))

    def finish(self):
        for offset, name in self.fixups:
            if name not in self.labels:
                raise ValueError("Missing label: " + name)
            distance = self.labels[name] - offset
            if not -32768 <= distance <= 32767:
                raise ValueError("PC-relative reference outside signed word: " + name)
            struct.pack_into(">h", self.code, offset, distance)
        return bytes(self.code)


def hunk(code):
    """One relocatable-position-independent HUNK_CODE, no absolute fixups."""
    code += bytes((-len(code)) % 4)
    size = len(code) // 4
    header = (0x3f3, 0, 1, 0, 0, size, 0x3e9, size)
    return struct.pack(">8I", *header) + code + struct.pack(">I", 0x3f2)
