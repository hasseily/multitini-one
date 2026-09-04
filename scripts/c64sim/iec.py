# SPDX-License-Identifier: GPL-3.0-only
"""IEC serial bus and optional parallel cable between the C64 and the drive.

Line state is kept as "pulled low" booleans per driver; the bus level
is the wired-AND of all drivers. The trace records electrical levels
(1 = released/high, 0 = pulled low) with a nanosecond timestamp.
"""

from __future__ import annotations


class IECBus:
    def __init__(self):
        self.c64_atn = False
        self.c64_clk = False
        self.c64_data = False
        self.drive_clk = False
        self.drive_data = False
        self.atn_low = False
        self.clk_low = False
        self.data_low = False
        self.c64_par = 0xFF
        self.drive_par = 0xFF
        self.par = 0xFF
        self.flag2 = 1
        self.trace = None
        self.time_fn = None
        self.on_atn = None
        self.on_pc2 = None
        self.on_flag2 = None
        self.transitions = 0

    def enable_trace(self, enabled: bool = True) -> None:
        self.trace = [] if enabled else None

    def _log(self, name: str, level: int) -> None:
        self.transitions += 1
        if self.trace is not None:
            self.trace.append((self.time_fn() if self.time_fn else 0, name, level))

    def _recompute(self) -> None:
        atn = self.c64_atn
        clk = self.c64_clk or self.drive_clk
        data = self.c64_data or self.drive_data
        if atn != self.atn_low:
            self.atn_low = atn
            self._log("ATN", 0 if atn else 1)
        if clk != self.clk_low:
            self.clk_low = clk
            self._log("CLK", 0 if clk else 1)
        if data != self.data_low:
            self.data_low = data
            self._log("DATA", 0 if data else 1)

    def update_c64(self, atn: bool, clk: bool, data: bool) -> None:
        atn_changed = atn != self.c64_atn
        self.c64_atn = atn
        self.c64_clk = clk
        self.c64_data = data
        self._recompute()
        if atn_changed and self.on_atn is not None:
            self.on_atn(atn)

    def update_drive(self, clk: bool, data: bool) -> None:
        self.drive_clk = clk
        self.drive_data = data
        self._recompute()

    # Parallel cable ---------------------------------------------------------

    def _recompute_par(self) -> None:
        value = self.c64_par & self.drive_par
        if value != self.par:
            self.par = value
            self._log("PAR", value)

    def set_c64_par(self, value: int) -> None:
        self.c64_par = value & 0xFF
        self._recompute_par()

    def set_drive_par(self, value: int) -> None:
        self.drive_par = value & 0xFF
        self._recompute_par()

    def pulse_pc2(self) -> None:
        """PC2 goes low for one C64 cycle after a CIA2 port B access."""
        self._log("PC2", 0)
        self._log("PC2", 1)
        if self.on_pc2 is not None:
            self.on_pc2()

    def set_flag2(self, level: int) -> None:
        """Drive VIA1 CA2: a level on user-port PA2 and an edge into FLAG2."""
        if level != self.flag2:
            self.flag2 = level
            self._log("FLAG2/PA2", level)
        if self.on_flag2 is not None:
            self.on_flag2(level)

    def format_trace(self, limit: int = 0) -> str:
        entries = self.trace or []
        if limit:
            entries = entries[-limit:]
        return "\n".join("%12.3f us %-9s %d" % (t / 1000.0, name, level) for t, name, level in entries)
